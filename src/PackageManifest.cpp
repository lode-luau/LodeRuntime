// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageManifest.hpp"

#include "PathUtil.hpp"

#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <utility>

namespace Lode::Package
{

namespace
{

using Lode::Detail::PathToUtf8;

class StaticManifestParser
{
public:
    explicit StaticManifestParser(std::string_view source)
        : source_(source)
    {}

    PackageManifestResult Parse()
    {
        PackageManifestResult result;
        SkipTrivia();
        if (!ConsumeKeyword("return"))
            return Fail("manifest must start with return");

        SkipTrivia();
        if (!Consume('{'))
            return Fail("manifest must return a table");

        result.document = nlohmann::json::object();
        while (true)
        {
            SkipTrivia();
            if (Consume('}'))
                break;

            std::string key;
            if (!ReadKey(key))
                return Fail("manifest table contains an invalid key");

            SkipTrivia();
            if (!Consume('='))
                return Fail("manifest fields must use key = value syntax");

            SkipTrivia();
            nlohmann::json value;
            if (!ReadValue(value))
                return Fail("manifest field '" + key + "' has an invalid value");
            result.document[key] = std::move(value);

            SkipTrivia();
            if (Consume(',') || Consume(';'))
                continue;
            if (Peek() != '}')
                return Fail("manifest fields must be separated by commas");
        }

        SkipTrivia();
        if (position_ != source_.size())
            return Fail("unexpected content after manifest table");
        if (!result.document.contains("name") || !result.document["name"].is_string())
            return Fail("manifest field 'name' must be a string");
        if (!result.document.contains("version") || !result.document["version"].is_string())
            return Fail("manifest field 'version' must be a string");
        result.manifest.name = result.document["name"].get<std::string>();
        result.manifest.version = result.document["version"].get<std::string>();
        if (result.manifest.name.empty())
            return Fail("manifest field 'name' is required");
        if (result.manifest.version.empty())
            return Fail("manifest field 'version' is required");
        if (result.document.contains("implementation"))
        {
            const auto& implementation = result.document["implementation"];
            if (!implementation.is_object())
                return Fail("manifest field 'implementation' must be a table");
            result.manifest.hasImplementation = true;
            if (implementation.contains("artifact"))
            {
                if (!implementation["artifact"].is_string())
                    return Fail("implementation field 'artifact' must be a string");
                result.manifest.implementation.artifact = implementation["artifact"].get<std::string>();
            }
            if (implementation.contains("layout"))
            {
                if (!implementation["layout"].is_string())
                    return Fail("implementation field 'layout' must be a string");
                result.manifest.implementation.layout = implementation["layout"].get<std::string>();
            }
            if (implementation.contains("required"))
            {
                if (!implementation["required"].is_boolean())
                    return Fail("implementation field 'required' must be boolean");
                result.manifest.implementation.required = implementation["required"].get<bool>();
            }
            if (result.manifest.implementation.artifact.empty())
                result.manifest.implementation.artifact = result.manifest.name;
        }
        return result;
    }

private:
    char Peek() const
    {
        return position_ < source_.size() ? source_[position_] : '\0';
    }

    bool Consume(char expected)
    {
        if (Peek() != expected)
            return false;
        ++position_;
        return true;
    }

    bool ConsumeKeyword(std::string_view keyword)
    {
        if (source_.substr(position_, keyword.size()) != keyword)
            return false;
        const size_t end = position_ + keyword.size();
        if (end < source_.size() && IsIdentifierCharacter(source_[end]))
            return false;
        position_ = end;
        return true;
    }

    static bool IsIdentifierStart(char character)
    {
        return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
    }

    static bool IsIdentifierCharacter(char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    }

    void SkipTrivia()
    {
        while (position_ < source_.size())
        {
            if (std::isspace(static_cast<unsigned char>(Peek())))
            {
                ++position_;
                continue;
            }

            if (Peek() != '-' || position_ + 1 >= source_.size() || source_[position_ + 1] != '-')
                break;

            position_ += 2;
            if (position_ + 1 < source_.size() && source_[position_] == '[' && source_[position_ + 1] == '[')
            {
                position_ = source_.find("]]", position_ + 2);
                if (position_ == std::string_view::npos)
                {
                    position_ = source_.size();
                    break;
                }
                position_ += 2;
            }
            else
            {
                const size_t newline = source_.find_first_of("\r\n", position_);
                position_ = newline == std::string_view::npos ? source_.size() : newline;
            }
        }
    }

    bool ReadKey(std::string& key)
    {
        if (IsIdentifierStart(Peek()))
        {
            const size_t begin = position_++;
            while (IsIdentifierCharacter(Peek()))
                ++position_;
            key = std::string(source_.substr(begin, position_ - begin));
            return true;
        }

        if (!Consume('['))
            return false;
        SkipTrivia();
        if (!ReadString(key))
            return false;
        SkipTrivia();
        return Consume(']');
    }

    bool ReadString(std::string& value)
    {
        const char quote = Peek();
        if (quote != '\'' && quote != '"')
            return false;
        ++position_;
        value.clear();
        while (position_ < source_.size())
        {
            const char character = source_[position_++];
            if (character == quote)
                return true;
            if (character != '\\')
            {
                value.push_back(character);
                continue;
            }
            if (position_ >= source_.size())
                return false;
            const char escaped = source_[position_++];
            switch (escaped)
            {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '\'': value.push_back('\''); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(escaped); break;
            }
        }
        return false;
    }

    bool ReadTable(nlohmann::json& value)
    {
        if (!Consume('{'))
            return false;

        nlohmann::json object = nlohmann::json::object();
        nlohmann::json array = nlohmann::json::array();
        bool hasKeyedEntries = false;
        bool hasArrayEntries = false;
        while (true)
        {
            SkipTrivia();
            if (Consume('}'))
                break;

            const size_t entryStart = position_;
            std::string key;
            bool keyed = ReadKey(key);
            if (keyed)
            {
                SkipTrivia();
                keyed = Consume('=');
            }
            position_ = entryStart;

            nlohmann::json entry;
            if (keyed)
            {
                ReadKey(key);
                SkipTrivia();
                Consume('=');
                SkipTrivia();
                if (!ReadValue(entry))
                    return false;
                object[key] = std::move(entry);
                hasKeyedEntries = true;
            }
            else
            {
                if (!ReadValue(entry))
                    return false;
                array.push_back(std::move(entry));
                hasArrayEntries = true;
            }

            SkipTrivia();
            if (Consume(',') || Consume(';'))
                continue;
            if (Peek() != '}')
                return false;
        }

        if (hasKeyedEntries && hasArrayEntries)
            return false;
        value = !hasArrayEntries || hasKeyedEntries ? std::move(object) : std::move(array);
        return true;
    }

    bool ReadValue(nlohmann::json& value)
    {
        if (Peek() == '\'' || Peek() == '"')
        {
            std::string stringValue;
            if (!ReadString(stringValue))
                return false;
            value = std::move(stringValue);
            return true;
        }

        if (Peek() == '{')
            return ReadTable(value);

        bool booleanValue = false;
        if (ReadBoolean(booleanValue))
        {
            value = booleanValue;
            return true;
        }

        if (ConsumeKeyword("nil"))
        {
            value = nullptr;
            return true;
        }

        const size_t begin = position_;
        while (position_ < source_.size())
        {
            const char character = Peek();
            if (std::isspace(static_cast<unsigned char>(character)) ||
                character == ',' || character == ';' || character == '}')
                break;
            ++position_;
        }
        if (position_ == begin)
            return false;

        const std::string token(source_.substr(begin, position_ - begin));
        try
        {
            size_t consumed = 0;
            const long long integer = std::stoll(token, &consumed);
            if (consumed == token.size())
            {
                value = integer;
                return true;
            }
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    bool ReadBoolean(bool& value)
    {
        if (ConsumeKeyword("true"))
        {
            value = true;
            return true;
        }
        if (ConsumeKeyword("false"))
        {
            value = false;
            return true;
        }
        return false;
    }

    bool ReadImplementation(PackageManifest& manifest)
    {
        if (!Consume('{'))
        {
            SetError("manifest field 'implementation' must be a table");
            return false;
        }
        manifest.hasImplementation = true;
        while (true)
        {
            SkipTrivia();
            if (Consume('}'))
                return true;

            std::string key;
            if (!ReadKey(key))
            {
                SetError("implementation table contains an invalid key");
                return false;
            }
            SkipTrivia();
            if (!Consume('='))
            {
                SetError("implementation fields must use key = value syntax");
                return false;
            }
            SkipTrivia();

            if (key == "artifact" || key == "layout")
            {
                std::string value;
                if (!ReadString(value))
                {
                    SetError("implementation field '" + key + "' must be a string");
                    return false;
                }
                if (key == "artifact")
                    manifest.implementation.artifact = std::move(value);
                else
                    manifest.implementation.layout = std::move(value);
            }
            else if (key == "required")
            {
                bool value = false;
                if (!ReadBoolean(value))
                {
                    SetError("implementation field 'required' must be boolean");
                    return false;
                }
                manifest.implementation.required = value;
            }
            else if (!SkipValue())
            {
                SetError("implementation field '" + key + "' has an invalid value");
                return false;
            }

            SkipTrivia();
            if (Consume(',') || Consume(';'))
                continue;
            if (Peek() != '}')
            {
                SetError("implementation fields must be separated by commas");
                return false;
            }
        }
    }

    bool SkipValue()
    {
        if (Peek() == '\'' || Peek() == '"')
        {
            std::string ignored;
            return ReadString(ignored);
        }

        const char opening = Peek();
        if (opening == '{' || opening == '[' || opening == '(')
        {
            const char closing = opening == '{' ? '}' : (opening == '[' ? ']' : ')');
            ++position_;
            int depth = 1;
            while (position_ < source_.size() && depth > 0)
            {
                SkipTrivia();
                if (Peek() == '\'' || Peek() == '"')
                {
                    std::string ignored;
                    if (!ReadString(ignored))
                        return false;
                    continue;
                }
                if (Peek() == opening)
                    ++depth;
                else if (Peek() == closing)
                    --depth;
                ++position_;
            }
            return depth == 0;
        }

        const size_t begin = position_;
        while (position_ < source_.size())
        {
            const char character = Peek();
            if (character == ',' || character == ';' || character == '}')
                break;
            ++position_;
        }
        return position_ > begin;
    }

    PackageManifestResult Fail(std::string message)
    {
        PackageManifestResult result;
        result.errors.push_back("package.luau: " + std::move(message));
        return result;
    }

    void SetError(std::string message)
    {
        if (error_.empty())
            error_ = "package.luau: " + std::move(message);
    }

    std::string_view source_;
    size_t position_ = 0;
    std::string error_;
};

} // namespace

namespace
{

std::string EscapeLuauString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char character : value)
    {
        switch (character)
        {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void SerializeValue(const nlohmann::json& value, std::ostringstream& output, size_t indent)
{
    const std::string padding(indent, ' ');
    if (value.is_object())
    {
        output << "{\n";
        bool first = true;
        for (const auto& [key, child] : value.items())
        {
            if (!first)
                output << ",\n";
            first = false;
            output << std::string(indent + 4, ' ') << "[" << EscapeLuauString(key) << "] = ";
            SerializeValue(child, output, indent + 4);
        }
        output << "\n" << padding << "}";
        return;
    }
    if (value.is_array())
    {
        output << "{\n";
        for (size_t index = 0; index < value.size(); ++index)
        {
            if (index != 0)
                output << ",\n";
            output << std::string(indent + 4, ' ');
            SerializeValue(value[index], output, indent + 4);
        }
        output << "\n" << padding << "}";
        return;
    }
    if (value.is_string())
    {
        output << EscapeLuauString(value.get<std::string>());
        return;
    }
    if (value.is_boolean())
    {
        output << (value.get<bool>() ? "true" : "false");
        return;
    }
    if (value.is_null())
    {
        output << "nil";
        return;
    }
    output << value.dump();
}

} // namespace

std::string SerializePackageManifest(const nlohmann::json& document)
{
    if (!document.is_object())
        return {};
    std::ostringstream output;
    output << "return ";
    SerializeValue(document, output, 0);
    output << "\n";
    return output.str();
}

PackageManifestResult ReadPackageManifest(const std::filesystem::path& manifestPath)
{
    std::ifstream file(manifestPath, std::ios::binary);
    if (!file.is_open())
    {
        PackageManifestResult result;
        result.errors.push_back("Cannot open package manifest: " + PathToUtf8(manifestPath));
        return result;
    }

    const std::string source(std::istreambuf_iterator<char>(file), {});
    PackageManifestResult result = StaticManifestParser(source).Parse();
    for (std::string& error : result.errors)
        error += " (" + PathToUtf8(manifestPath) + ")";
    return result;
}

} // namespace Lode::Package
