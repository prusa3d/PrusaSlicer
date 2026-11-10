#pragma once

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace Slic3r::Biz::FileOpen {

/**
 * @brief Prefix of the names of all JobManager jobs which load files.
 *
 * Progress of these jobs is shown by the loading progress dialog.
 */
inline constexpr std::string_view FILE_LOAD_JOB_PREFIX{"file_load"};

/**
 * @brief Stage of a file loading job, stored as Domain::ProgressDetail::info.
 */
enum class FileLoadStage
{
    Reading,
    Converting,
    AddingToScene
};

/**
 * @brief Payload of Domain::ProgressDetail of a file loading job.
 */
struct FileLoadProgressPayload
{
    std::string file_name;
    size_t file_index{0};
    size_t file_count{1};
};

/**
 * @brief Called by the loading logic when a new loading stage starts.
 *
 * It may throw FileLoadCancelled to stop the loading.
 */
using FileLoadProgressFn = std::function<void(FileLoadStage, const FileLoadProgressPayload&)>;

/**
 * @brief Thrown from a file loading job when the user cancelled it.
 */
class FileLoadCancelled : public std::exception
{
public:
    const char* what() const noexcept override { return "File loading was cancelled"; }
};

enum class FileLoadResult
{
    Loaded,
    Failed,
    Cancelled
};

/**
 * @brief Called on the main thread when a file loading job ends.
 */
using FileLoadDoneFn = std::function<void(FileLoadResult)>;

} // namespace Slic3r::Biz::FileOpen
