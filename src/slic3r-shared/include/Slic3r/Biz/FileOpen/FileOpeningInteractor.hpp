#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <boost/filesystem/path.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::FileOpen {

struct OpenFilesOptions
{
    bool new_project = false; ///< Models go to a new project, a project takes over the selected empty one.
    bool as_geometry = false; ///< 3MF files are imported as models, inside an archive the user may still open them as projects.
};

/**
 * @brief Protection against zip bombs.
 */
struct ArchiveLimits
{
    std::size_t max_entries = 1000; ///< More entries reject the whole archive.
    std::uint64_t max_entry_size = 1024ull * 1024 * 1024;
    std::uint64_t max_total_size = 2ull * 1024 * 1024 * 1024;
    std::uint64_t max_compression_ratio = 200;
    std::uint64_t compression_ratio_min_size = 1024 * 1024; ///< Smaller entries skip the ratio check.
};

enum class SkipReason
{
    Unsupported,
    Encrypted,
    UnsupportedCompression,
    Symlink,
    UnsafePath,
    InvalidName,
    NestedArchive,
    TooLarge,
    SuspiciousCompressionRatio,
    ArchiveLimitsExceeded,
    ExtractionFailed,
};

/**
 * @brief File from an archive, extracted unless skipped.
 */
struct ArchiveFile
{
    boost::filesystem::path archive;
    std::string name; ///< Path inside the archive, valid UTF-8.
    boost::filesystem::path path; ///< Extracted file, empty when skipped.
    std::uint64_t size = 0; ///< Uncompressed size.
    bool is_project = false; ///< 3MF file, opens as a project by default.
    bool has_config = false; ///< PrusaSlicer configuration found inside a 3MF.
    std::optional<SkipReason> skip_reason;
};

struct ExtractedArchive
{
    std::vector<ArchiveFile> files; ///< Without directories and OS metadata like __MACOSX.
    std::vector<std::string> errors; ///< Translated.
    std::vector<boost::filesystem::path> directories; ///< Remove once the files are opened.
};

/**
 * @brief Extracts every file which may be opened into a new temporary directory.
 *
 * Blocking, call from a job. A 3MF renamed to .zip is returned as a single 3MF file.
 * Directories left behind, e.g. after a crash, are removed by remove_stale_extractions().
 * @param root System temporary directory when empty.
 */
ExtractedArchive extract_archive(
    const boost::filesystem::path& archive,
    const ArchiveLimits& limits = {},
    const boost::filesystem::path& root = {}
);

/**
 * @brief Removes directories created by extract_archive() older than max_age.
 * @param root System temporary directory when empty.
 */
void remove_stale_extractions(
    std::chrono::hours max_age = std::chrono::hours(24),
    const boost::filesystem::path& root = {}
);

/**
 * @brief Extracted files chosen to be opened.
 */
struct ArchiveSelection
{
    std::vector<boost::filesystem::path> projects;
    std::vector<boost::filesystem::path> models;
    bool models_to_new_project = false;
};

/**
 * @brief Receives the chosen files, std::nullopt when cancelled.
 */
using ArchiveSelectionDone = std::function<void(std::optional<ArchiveSelection>)>;

/**
 * @brief Last path component with characters invalid in file names replaced, never empty.
 */
std::string sanitized_file_name(const std::string& entry_name);

/**
 * @brief Outcomes worth telling the user, any number of listeners.
 */
class IFileOpenEventListener
{
public:
    virtual ~IFileOpenEventListener() = default;

    virtual void on_archive_files_skipped(std::size_t count) {}
    virtual void on_archive_open_failed(const std::string& message) {}
    /**
     * @brief The archive dialog could not be shown, nothing was opened.
     */
    virtual void on_archive_postponed(
        const std::vector<boost::filesystem::path>& archives,
        OpenFilesOptions options
    ) {}
};

class FileOpeningInteractor : public WithListeners<IFileOpenEventListener>
{
public:
    explicit FileOpeningInteractor(ProjectInteractor& project_interactor) :
        m_project_interactor(project_interactor)
    {}

    /**
     * @return false when the dialog can't be shown now.
     */
    using ArchiveDialog = std::function<
        bool(std::vector<ArchiveFile> files, OpenFilesOptions options, ArchiveSelectionDone done)>;

    /**
     * @brief Opens files from any source: a single 3MF as a project, everything else as models.
     * Archives are extracted in a job.
     */
    void open(std::vector<boost::filesystem::path> paths, OpenFilesOptions options = {});

    /**
     * @brief Lets the user choose files when an archive contains more than one, without it all are opened.
     */
    void set_archive_dialog(ArchiveDialog dialog)
    {
        m_archive_dialog = std::move(dialog);
    }

private:
    void open_archives(std::vector<boost::filesystem::path> archives, OpenFilesOptions options);
    /**
     * @brief Projects are opened once the imported models are arranged, opening a project selects it.
     * @param directories Removed once everything is opened.
     */
    void open_archive_selection(
        ArchiveSelection selection,
        bool replace_empty_project,
        std::vector<boost::filesystem::path> directories
    );
    /**
     * @brief Opens projects one after another, a new loading job would wait for the previous one.
     * @param directories Removed once the last project is loaded.
     */
    void open_archive_projects(
        std::vector<boost::filesystem::path> projects,
        bool replace_empty_project,
        std::vector<boost::filesystem::path> directories
    );
    /**
     * @param to_new_project A new project is created unless the selected one is empty.
     */
    void import_models(std::vector<boost::filesystem::path> files, bool to_new_project, bool temporary_files);
    void report_archive_error(const std::string& message);

    ProjectInteractor& m_project_interactor;
    ArchiveDialog m_archive_dialog;
    std::size_t m_extraction_counter = 0;
};

} // namespace Slic3r::Biz::FileOpen
