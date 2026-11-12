#pragma once

#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <miniz.h>

/**
 * @file
 * @brief Used by extract_archive(), exposed for tests. Kept apart, so miniz.h doesn't spread with FileOpeningInteractor.hpp.
 */

namespace Slic3r::Biz::FileOpen {

struct EntryInfo
{
    int index = -1;
    std::string name; ///< Valid UTF-8.
    std::uint64_t compressed_size   = 0;
    std::uint64_t uncompressed_size = 0;
    bool is_directory        = false;
    bool is_encrypted        = false;
    bool is_supported_method = true;
    bool is_symlink          = false;
};

EntryInfo entry_info_from_stat(const mz_zip_archive_file_stat& stat, const std::string& name);

/**
 * @return std::nullopt if the entry may be opened.
 */
std::optional<SkipReason> check_entry(const EntryInfo& entry, const ArchiveLimits& limits);

} // namespace Slic3r::Biz::FileOpen
