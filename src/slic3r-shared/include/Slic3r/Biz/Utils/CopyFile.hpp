#pragma once

#include <string>
#include <system_error>

namespace Slic3r::Biz::Utils {

enum CopyFileResult
{
    Success = 0,
    FailCopyFile,
    FailFilesDifferent,
    FailRenaming,
    FailCheckOriginNotOpened,
    FailCheckTargetNotOpened
};

/**
 * @brief Copy file to a temp file first, then rename it to the final file name.
 * If with_check is true, then the content of the copied file is compared to the content
 * of the source file before renaming.
 * Additional error info is passed in error message.
 */
CopyFileResult copy_file(
    const std::string& from,
    const std::string& to,
    std::string& error_message,
    const bool with_check = false
);

/**
 * @brief Compares two files if identical.
 */
CopyFileResult check_copy(const std::string& origin, const std::string& copy);

/**
 * @brief Safely rename a file even if the target exists.
 * On Windows, the file explorer (or anti-virus or whatever else) often locks the file
 * for a short while, so the file may not be movable. Retry while we see recoverable errors.
 */
std::error_code rename_file(const std::string& from, const std::string& to);
} // namespace Slic3r::Biz::Utils
