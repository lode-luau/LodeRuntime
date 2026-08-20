// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageArchive.hpp"

#include "PathUtil.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <miniz.h>
#include <system_error>

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathFromUtf8;
using Lode::Detail::PathToUtf8;

void AddError(ArchiveExtractionResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool IsSha256(std::string_view value)
{
    if (value.size() != 64)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

std::string NormalizeEntryName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

bool IsSafeEntryName(std::string_view name, fs::path& relativePath)
{
    if (name.empty() || name.find('\0') != std::string_view::npos || name.front() == '/')
        return false;

    // A drive-qualified or UNC path is absolute on Windows even when the
    // host running this code is different from the archive producer.
    if (name.size() >= 2 && std::isalpha(static_cast<unsigned char>(name[0])) && name[1] == ':')
        return false;
    if (name.starts_with("//"))
        return false;

    for (size_t start = 0; start <= name.size();)
    {
        const size_t separator = name.find('/', start);
        const size_t end = separator == std::string_view::npos ? name.size() : separator;
        const std::string_view component = name.substr(start, end - start);
        if (component == "..")
            return false;
        if (!component.empty() && component != ".")
            relativePath /= PathFromUtf8(component);
        if (separator == std::string_view::npos)
            break;
        start = separator + 1;
    }

    return !relativePath.empty() && !relativePath.is_absolute() &&
           relativePath.root_name().empty() && relativePath.root_directory().empty();
}

bool IsInside(const fs::path& candidate, const fs::path& root)
{
    std::error_code ec;
    const fs::path canonicalRoot = fs::weakly_canonical(root, ec);
    if (ec)
        return false;
    const fs::path canonicalCandidate = fs::weakly_canonical(candidate, ec);
    if (ec)
        return false;
    const fs::path relative = fs::relative(canonicalCandidate, canonicalRoot, ec);
    return !ec && !relative.empty() && relative != "." &&
           *relative.begin() != "..";
}

} // namespace

ArchiveExtractionResult ExtractVerifiedArchive(const fs::path& archivePath,
                                                std::string_view expectedSha256,
                                                const fs::path& extractionRoot)
{
    ArchiveExtractionResult result;
    if (!IsSha256(expectedSha256))
    {
        AddError(result, "Archive SHA-256 must contain exactly 64 hexadecimal characters.");
        return result;
    }
    if (!fs::is_regular_file(archivePath))
    {
        AddError(result, "Archive does not exist: " + PathToUtf8(archivePath));
        return result;
    }
    if (extractionRoot.empty())
    {
        AddError(result, "Archive extraction requires an explicit staging directory.");
        return result;
    }

    try
    {
        const std::string actualSha256 = Lode::Detail::Sha256FileHex(archivePath);
        std::string expected(expectedSha256);
        std::transform(expected.begin(), expected.end(), expected.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (actualSha256 != expected)
        {
            AddError(result, "Archive SHA-256 mismatch for " + PathToUtf8(archivePath) + ".");
            return result;
        }

        std::error_code ec;
        fs::remove_all(extractionRoot, ec);
        if (ec || !fs::create_directories(extractionRoot, ec) || ec)
        {
            AddError(result, "Cannot create archive staging directory: " + PathToUtf8(extractionRoot));
            return result;
        }

        mz_zip_archive zip{};
        const std::string archiveUtf8 = PathToUtf8(archivePath);
        if (!mz_zip_reader_init_file(&zip, archiveUtf8.c_str(), 0))
        {
            AddError(result, "Cannot read ZIP archive: " + PathToUtf8(archivePath));
            fs::remove_all(extractionRoot, ec);
            return result;
        }

        bool valid = true;
        const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
        for (mz_uint index = 0; index < fileCount; ++index)
        {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip, index, &stat) || !stat.m_filename)
            {
                AddError(result, "ZIP archive contains an unreadable entry.");
                valid = false;
                break;
            }

            fs::path relativePath;
            const std::string entryName = NormalizeEntryName(stat.m_filename);
            if (!IsSafeEntryName(entryName, relativePath))
            {
                AddError(result, "ZIP archive contains an unsafe entry: " + entryName);
                valid = false;
                break;
            }

            const fs::path destination = extractionRoot / relativePath;
            if (!IsInside(destination, extractionRoot))
            {
                AddError(result, "ZIP archive entry escapes staging directory: " + entryName);
                valid = false;
                break;
            }
        }

        if (valid)
        {
            for (mz_uint index = 0; index < fileCount; ++index)
            {
                mz_zip_archive_file_stat stat{};
                mz_zip_reader_file_stat(&zip, index, &stat);
                const std::string entryName = NormalizeEntryName(stat.m_filename);
                fs::path relativePath;
                IsSafeEntryName(entryName, relativePath);
                const fs::path destination = extractionRoot / relativePath;
                if (mz_zip_reader_is_file_a_directory(&zip, index))
                {
                    fs::create_directories(destination, ec);
                    if (ec)
                    {
                        AddError(result, "Cannot create ZIP directory: " + entryName);
                        valid = false;
                        break;
                    }
                    continue;
                }

                fs::create_directories(destination.parent_path(), ec);
                if (ec || !mz_zip_reader_extract_to_file(&zip, index,
                        PathToUtf8(destination).c_str(), 0))
                {
                    AddError(result, "Cannot extract ZIP entry: " + entryName);
                    valid = false;
                    break;
                }
            }
        }
        mz_zip_reader_end(&zip);

        if (!valid)
        {
            fs::remove_all(extractionRoot, ec);
            return result;
        }

        result.packageRoot = fs::weakly_canonical(extractionRoot, ec);
        if (ec)
        {
            AddError(result, "Cannot resolve extracted archive staging directory.");
            fs::remove_all(extractionRoot, ec);
        }
    }
    catch (const std::exception& error)
    {
        AddError(result, error.what());
        std::error_code ec;
        fs::remove_all(extractionRoot, ec);
    }
    return result;
}

} // namespace Lode::Package
