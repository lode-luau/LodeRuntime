// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageManifest.hpp"

#include "PathUtil.hpp"

#include <cctype>
#include <fstream>
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
            if (key == "name" || key == "version")
            {
                std::string value;
                if (!ReadString(value))
                    return Fail("manifest field '" + key + "' must be a string");
                if (key == "name")
                    result.manifest.name = std::move(value);
                else
                    result.manifest.version = std::move(value);
            }
            else if (key == "implementation")
            {
                if (!ReadImplementation(result.manifest))
                {
                    if (!error_.empty())
                        result.errors.push_back(error_);
                    return result;
                }
            }
            else if (!SkipValue())
            {
                return Fail("manifest field '" + key + "' has an invalid value");
            }

            SkipTrivia();
            if (Consume(',') || Consume(';'))
                continue;
            if (Peek() != '}')
                return Fail("manifest fields must be separated by commas");
        }

        SkipTrivia();
        if (position_ != source_.size())
            return Fail("unexpected content after manifest table");
        if (result.manifest.name.empty())
            return Fail("manifest field 'name' is required");
        if (result.manifest.version.empty())
            return Fail("manifest field 'version' is required");
        if (result.manifest.hasImplementation && result.manifest.implementation.artifact.empty())
            result.manifest.implementation.artifact = result.manifest.name;
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
