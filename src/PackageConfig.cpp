// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageConfig.hpp"

#include "PathUtil.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathToUtf8;

void AddError(ConfigAliasUpdateResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool IsIdentifierStart(char character)
{
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool IsIdentifierContinue(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

bool IsSafeRelativeAliasPath(const std::string& value)
{
    const fs::path path = fs::path(value).lexically_normal();
    if (value.empty() || path.empty() || path.is_absolute() ||
        !path.root_name().empty() || !path.root_directory().empty())
        return false;

    const auto first = path.begin();
    return first != path.end() && *first != "..";
}

void SkipWhitespaceAndComments(std::string_view text, size_t& position)
{
    while (position < text.size())
    {
        if (std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
            continue;
        }

        if (position + 1 >= text.size() || text[position] != '-' || text[position + 1] != '-')
            return;

        position += 2;
        if (position + 1 < text.size() && text[position] == '[' && text[position + 1] == '[')
        {
            position = text.find("]]", position + 2);
            if (position == std::string_view::npos)
            {
                position = text.size();
                return;
            }
            position += 2;
        }
        else
        {
            const size_t newline = text.find_first_of("\r\n", position);
            position = newline == std::string_view::npos ? text.size() : newline;
        }
    }
}

bool SkipQuotedString(std::string_view text, size_t& position)
{
    if (position >= text.size() || (text[position] != '\'' && text[position] != '"'))
        return false;

    const char quote = text[position++];
    while (position < text.size())
    {
        if (text[position] == '\\')
        {
            position += std::min<size_t>(2, text.size() - position);
            continue;
        }
        if (text[position++] == quote)
            return true;
    }
    return false;
}

bool ReadQuotedString(std::string_view text, size_t& position, std::string& value)
{
    if (position >= text.size() || (text[position] != '\'' && text[position] != '"'))
        return false;

    const char quote = text[position++];
    value.clear();
    while (position < text.size())
    {
        const char character = text[position++];
        if (character == quote)
            return true;
        if (character != '\\')
        {
            value.push_back(character);
            continue;
        }
        if (position >= text.size())
            return false;
        const char escaped = text[position++];
        switch (escaped)
        {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(escaped); break;
        }
    }
    return false;
}

bool FindAliasesTable(std::string_view text, size_t& open, size_t& close)
{
    size_t position = 0;
    while (position < text.size())
    {
        SkipWhitespaceAndComments(text, position);
        if (position >= text.size())
            break;

        if (text[position] == '\'' || text[position] == '"')
        {
            SkipQuotedString(text, position);
            continue;
        }

        if (!IsIdentifierStart(text[position]))
        {
            ++position;
            continue;
        }

        const size_t identifierStart = position++;
        while (position < text.size() && IsIdentifierContinue(text[position]))
            ++position;
        if (text.substr(identifierStart, position - identifierStart) != "aliases")
            continue;

        size_t valuePosition = position;
        SkipWhitespaceAndComments(text, valuePosition);
        if (valuePosition >= text.size() || text[valuePosition] != '=')
            continue;
        ++valuePosition;
        SkipWhitespaceAndComments(text, valuePosition);
        if (valuePosition >= text.size() || text[valuePosition] != '{')
            continue;

        open = valuePosition;
        ++valuePosition;
        int depth = 1;
        while (valuePosition < text.size() && depth > 0)
        {
            if (text[valuePosition] == '-' && valuePosition + 1 < text.size() &&
                text[valuePosition + 1] == '-')
            {
                SkipWhitespaceAndComments(text, valuePosition);
                continue;
            }
            if (text[valuePosition] == '\'' || text[valuePosition] == '"')
            {
                if (!SkipQuotedString(text, valuePosition))
                    return false;
                continue;
            }
            if (text[valuePosition] == '{')
                ++depth;
            else if (text[valuePosition] == '}' && --depth == 0)
            {
                close = valuePosition;
                return true;
            }
            ++valuePosition;
        }
        return false;
    }
    return false;
}

struct ExistingAlias
{
    std::string value;
    bool valueIsString = false;
};

std::optional<ExistingAlias> FindExistingAlias(std::string_view text,
                                               size_t open,
                                               size_t close,
                                               std::string_view wanted)
{
    size_t position = open + 1;
    while (position < close)
    {
        SkipWhitespaceAndComments(text, position);
        if (position >= close)
            break;

        std::string key;
        const size_t keyStart = position;
        if (IsIdentifierStart(text[position]))
        {
            ++position;
            while (position < close && IsIdentifierContinue(text[position]))
                ++position;
            key = std::string(text.substr(keyStart, position - keyStart));
        }
        else if (text[position] == '[')
        {
            ++position;
            SkipWhitespaceAndComments(text, position);
            if (!ReadQuotedString(text, position, key))
            {
                ++position;
                continue;
            }
            SkipWhitespaceAndComments(text, position);
            if (position >= close || text[position++] != ']')
                continue;
        }
        else
        {
            if (text[position] == '{' || text[position] == '[')
            {
                const char opening = text[position++];
                const char closing = opening == '{' ? '}' : ']';
                int depth = 1;
                while (position < close && depth > 0)
                {
                    if (text[position] == '\'' || text[position] == '"')
                        SkipQuotedString(text, position);
                    else if (text[position] == opening)
                        ++depth, ++position;
                    else if (text[position] == closing)
                        --depth, ++position;
                    else
                        ++position;
                }
            }
            else
                ++position;
            continue;
        }

        SkipWhitespaceAndComments(text, position);
        if (position >= close || text[position++] != '=')
            continue;
        SkipWhitespaceAndComments(text, position);

        ExistingAlias existing;
        if (text[position] == '\'' || text[position] == '"')
            existing.valueIsString = ReadQuotedString(text, position, existing.value);
        else
        {
            existing.valueIsString = false;
            while (position < close && text[position] != ',')
                ++position;
        }

        if (key == wanted)
            return existing;
    }
    return std::nullopt;
}

bool IsSafeAlias(std::string_view alias)
{
    if (alias.empty() || alias == "." || alias == "..")
        return false;
    for (const char character : alias)
    {
        if (!std::isalnum(static_cast<unsigned char>(character)) &&
            character != '-' && character != '_' && character != '.')
            return false;
    }
    return true;
}

std::string EscapeLuaString(std::string_view value)
{
    std::string escaped;
    for (const char character : value)
    {
        if (character == '\\' || character == '"')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

bool ReplaceFileAtomically(const fs::path& temporaryPath,
                           const fs::path& destinationPath,
                           std::string& error)
{
#if defined(_WIN32)
    if (!MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "atomic replacement failed with Windows error " +
            std::to_string(GetLastError());
        return false;
    }
    return true;
#else
    std::error_code ec;
    fs::rename(temporaryPath, destinationPath, ec);
    if (ec)
    {
        error = "atomic replacement failed: " + ec.message();
        return false;
    }
    return true;
#endif
}

bool WriteConfigContent(const fs::path& configPath,
                        std::string_view content,
                        std::vector<std::string>& errors)
{
    std::error_code ec;
    const fs::path absolutePath = fs::absolute(configPath, ec);
    if (ec)
    {
        errors.push_back("Cannot determine .config.luau path: " + ec.message());
        return false;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporaryPath = absolutePath;
    temporaryPath += ".tmp-" + std::to_string(timestamp);
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            errors.push_back("Cannot open temporary .config.luau: " + PathToUtf8(temporaryPath));
            return false;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output.good())
        {
            errors.push_back("Failed to write temporary .config.luau: " + PathToUtf8(temporaryPath));
            output.close();
            fs::remove(temporaryPath, ec);
            return false;
        }
    }

    std::string replacementError;
    if (!ReplaceFileAtomically(temporaryPath, absolutePath, replacementError))
    {
        errors.push_back("Cannot replace .config.luau: " + replacementError);
        fs::remove(temporaryPath, ec);
        return false;
    }
    return true;
}

} // namespace

ConfigAliasUpdateResult UpdateConfigAliases(
    std::string_view configContent,
    const std::vector<std::pair<std::string, std::string>>& aliases)
{
    ConfigAliasUpdateResult result;
    result.content = std::string(configContent);

    if (aliases.empty())
        return result;

    size_t open = 0;
    size_t close = 0;
    if (!FindAliasesTable(configContent, open, close))
    {
        AddError(result, "Cannot update .config.luau: luau.aliases table was not found or is malformed.");
        result.content.clear();
        return result;
    }

    std::vector<std::pair<std::string, std::string>> additions;
    for (const auto& [alias, path] : aliases)
    {
        if (!IsSafeAlias(alias))
        {
            AddError(result, "Cannot update .config.luau: unsafe package alias '" + alias + "'.");
            continue;
        }
        if (!IsSafeRelativeAliasPath(path))
        {
            AddError(result, "Cannot update .config.luau: alias '" + alias +
                "' must use a non-empty relative path.");
            continue;
        }

        const std::optional<ExistingAlias> existing = FindExistingAlias(
            configContent, open, close, alias);
        if (existing)
        {
            if (!existing->valueIsString || existing->value != path)
            {
                AddError(result, "Cannot update .config.luau: alias '" + alias +
                    "' already points to a different path.");
            }
            continue;
        }
        additions.emplace_back(alias, path);
    }

    if (!result.IsValid() || additions.empty())
    {
        if (!result.IsValid())
            result.content.clear();
        return result;
    }

    const std::string newline = configContent.find("\r\n") != std::string_view::npos
        ? "\r\n" : "\n";
    const bool multiline = configContent.find('\n', open) != std::string_view::npos &&
        configContent.find('\n', open) < close;
    if (!multiline)
    {
        size_t lastContent = close;
        bool insertedComma = false;
        while (lastContent > open + 1 &&
               std::isspace(static_cast<unsigned char>(result.content[lastContent - 1])))
            --lastContent;
        if (lastContent > open + 1 && result.content[lastContent - 1] != ',')
        {
            result.content.insert(lastContent, ",");
            insertedComma = true;
        }
        if (insertedComma)
            ++close;

        std::string generated;
        for (const auto& [alias, path] : additions)
        {
            generated += "[\"" + EscapeLuaString(alias) + "\"] = \"" +
                EscapeLuaString(path) + "\", ";
        }
        result.content.insert(close, generated);
        result.changed = true;
        return result;
    }

    size_t closeLineStart = result.content.rfind('\n', close);
    closeLineStart = closeLineStart == std::string::npos ? 0 : closeLineStart + 1;
    size_t indentationEnd = closeLineStart;
    while (indentationEnd < result.content.size() &&
           (result.content[indentationEnd] == ' ' || result.content[indentationEnd] == '\t'))
        ++indentationEnd;
    const std::string closeIndent = result.content.substr(closeLineStart, indentationEnd - closeLineStart);
    const std::string entryIndent = closeIndent + "    ";

    size_t lastContent = close;
    while (lastContent > open + 1 &&
           std::isspace(static_cast<unsigned char>(result.content[lastContent - 1])))
        --lastContent;
    if (lastContent > open + 1 && result.content[lastContent - 1] != ',')
        result.content.insert(lastContent, ",");

    std::string generated;
    for (const auto& [alias, path] : additions)
    {
        generated += entryIndent + "[\"" + EscapeLuaString(alias) + "\"] = \"" +
            EscapeLuaString(path) + "\"," + newline;
    }

    closeLineStart = result.content.rfind('\n', close);
    closeLineStart = closeLineStart == std::string::npos ? 0 : closeLineStart + 1;
    result.content.insert(closeLineStart, generated);
    result.changed = true;
    return result;
}

ConfigAliasUpdateResult WriteConfigAliases(
    const fs::path& configPath,
    const std::vector<std::pair<std::string, std::string>>& aliases)
{
    ConfigAliasUpdateResult result;
    std::ifstream file(configPath, std::ios::binary);
    if (!file.is_open())
    {
        AddError(result, "Cannot open .config.luau: " + PathToUtf8(configPath));
        return result;
    }

    const std::string content(std::istreambuf_iterator<char>(file), {});
    result = UpdateConfigAliases(content, aliases);
    if (!result.IsValid() || !result.changed)
        return result;

    if (!WriteConfigContent(configPath, result.content, result.errors))
        result.content.clear();
    return result;
}

ConfigAliasUpdateResult WriteGeneratedConfigAliases(
    const fs::path& configPath,
    const std::vector<std::pair<std::string, std::string>>& aliases)
{
    ConfigAliasUpdateResult result;
    result.content = "return {\n    luau = {\n        aliases = {\n";
    for (const auto& [alias, path] : aliases)
    {
        if (!IsSafeAlias(alias))
        {
            AddError(result, "Cannot create .config.luau: unsafe package alias '" + alias + "'.");
            continue;
        }
        if (!IsSafeRelativeAliasPath(path))
        {
            AddError(result, "Cannot create .config.luau: alias '" + alias +
                "' must use a non-empty relative path.");
            continue;
        }
        result.content += "            [\"" + EscapeLuaString(alias) + "\"] = \"" +
            EscapeLuaString(path) + "\",\n";
    }
    result.content += "        }\n    }\n}\n";
    if (!result.IsValid())
    {
        result.content.clear();
        return result;
    }
    result.changed = !aliases.empty();
    if (result.changed && !WriteConfigContent(configPath, result.content, result.errors))
        result.content.clear();
    return result;
}

} // namespace Lode::Package
