#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"

#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/FileOpen/ArchiveEntry.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/MiniZErrorTranslation.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>

#include <fmt/format.h>
#include <tl/expected.hpp>

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <set>
#include <string_view>

namespace Slic3r::Biz::FileOpen {

namespace fs = boost::filesystem;
using Algorithms::ZipArchive;

namespace {

constexpr std::string_view DIRECTORY_PREFIX = "PrusaSlicer-archive-";

constexpr std::uint16_t HOST_UNIX   = 3;
constexpr std::uint16_t HOST_DARWIN = 19;
constexpr std::uint32_t UNIX_FILE_TYPE_MASK = 0170000;
constexpr std::uint32_t UNIX_SYMLINK        = 0120000;
constexpr std::uint16_t FLAG_ENCRYPTED      = 1;

fs::path root_or_temp(const fs::path& root)
{
    return root.empty() ? Slic3r::temp_dir() : root;
}

std::string normalized_separators(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

std::vector<std::string> split_path(const std::string& name)
{
    std::vector<std::string> segments;
    boost::algorithm::split(segments, name, [](char c) { return c == '/'; });
    return segments;
}

std::string last_segment(const std::string& name)
{
    const std::string normalized = normalized_separators(name);
    const std::size_t pos        = normalized.find_last_of('/');
    return pos == std::string::npos ? normalized : normalized.substr(pos + 1);
}

bool is_unsafe_path(const std::string& name)
{
    if (name.empty()) {
        return false;
    }
    if (name.front() == '/') {
        return true;
    }
    if (name.size() >= 2 && name[1] == ':') {
        return true;
    }
    for (const std::string& segment : split_path(name)) {
        if (segment == "..") {
            return true;
        }
    }
    return false;
}

bool has_control_characters(const std::string& name)
{
    return std::any_of(name.begin(), name.end(), [](char c) {
        const auto uc = static_cast<unsigned char>(c);
        return uc < 0x20 || uc == 0x7f;
    });
}

/**
 * @brief Replaces invalid UTF-8 sequences with '_'.
 */
std::string make_valid_utf8(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t min_code_point = 0;
        if (c < 0x80) {
            length = 1;
        } else if ((c & 0xE0) == 0xC0) {
            length = 2;
            min_code_point = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            length = 3;
            min_code_point = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            length = 4;
            min_code_point = 0x10000;
        }

        bool valid = length > 0 && i + length <= text.size();
        std::uint32_t code_point = length == 1 ? c : (c & (0x7F >> length));
        for (std::size_t j = 1; valid && j < length; ++j) {
            const auto cc = static_cast<unsigned char>(text[i + j]);
            if ((cc & 0xC0) != 0x80) {
                valid = false;
            } else {
                code_point = (code_point << 6) | (cc & 0x3F);
            }
        }
        if (valid && length > 1
            && (code_point < min_code_point || code_point > 0x10FFFF
                || (code_point >= 0xD800 && code_point <= 0xDFFF)))
        {
            valid = false;
        }

        if (valid) {
            result.append(text, i, length);
            i += length;
        } else {
            result.push_back('_');
            ++i;
        }
    }
    return result;
}

/**
 * @brief OS or archiver metadata, e.g. __MACOSX, .DS_Store.
 */
bool is_junk_entry(const std::string& entry_name)
{
    const std::string normalized = normalized_separators(entry_name);
    for (const std::string& segment : split_path(normalized)) {
        if (segment == "__MACOSX") {
            return true;
        }
    }

    const std::string file_name = last_segment(normalized);
    return boost::algorithm::starts_with(file_name, ".")
        || boost::algorithm::iequals(file_name, "Thumbs.db")
        || boost::algorithm::iequals(file_name, "desktop.ini");
}

/**
 * @brief Checks for PrusaSlicer configuration, current or 2.x format.
 */
bool is_project_3mf(const fs::path& file_3mf)
{
    ZipArchive archive = ZipArchive::open(file_3mf.string(), ZipArchive::Mode::Read);
    if (!archive.is_open()) {
        return false;
    }
    return archive.locate_file(std::string(Format::ProjectFileConstants::PRUSA_PROJECT_FILEPATH)) >= 0
        || archive.locate_file("Metadata/Slic3r_PE.config") >= 0;
}

tl::expected<fs::path, std::string> create_extraction_directory(const fs::path& root)
{
    const fs::path directory =
        root_or_temp(root) / (std::string(DIRECTORY_PREFIX) + fs::unique_path("%%%%-%%%%-%%%%-%%%%").string());

    boost::system::error_code ec;
    if (!fs::create_directories(directory, ec) || ec) {
        return tl::make_unexpected(fmt::format(
            fmt::runtime(_u8L("Failed to create temporary directory {}: {}")),
            directory.string(),
            ec.message()
        ));
    }
    return directory;
}

/**
 * @brief file_name made unique by " (1)", " (2)", ... compared case insensitively.
 */
fs::path unique_target(const fs::path& directory, const std::string& file_name, std::set<std::string>& used_names)
{
    const fs::path name{file_name};
    const std::string stem      = name.stem().string();
    const std::string extension = name.extension().string();

    std::string candidate = file_name;
    for (int i = 1; used_names.count(boost::algorithm::to_lower_copy(candidate)) != 0; ++i) {
        candidate = fmt::format("{} ({}){}", stem, i, extension);
    }
    used_names.insert(boost::algorithm::to_lower_copy(candidate));
    return directory / candidate;
}

void remove_directories(const std::vector<fs::path>& directories)
{
    for (const fs::path& directory : directories) {
        boost::system::error_code ec;
        fs::remove_all(directory, ec);
        if (ec) {
            SPDLOG_WARN("Failed to remove temporary directory {}: {}", directory.string(), ec.message());
        }
    }
}

} // namespace

EntryInfo entry_info_from_stat(const mz_zip_archive_file_stat& stat, const std::string& name)
{
    const std::uint16_t host = static_cast<std::uint16_t>(stat.m_version_made_by >> 8);
    const std::uint32_t unix_mode = stat.m_external_attr >> 16;

    EntryInfo info;
    info.index               = static_cast<int>(stat.m_file_index);
    info.name                = make_valid_utf8(name);
    info.compressed_size     = stat.m_comp_size;
    info.uncompressed_size   = stat.m_uncomp_size;
    info.is_directory        = stat.m_is_directory != 0;
    info.is_encrypted        = stat.m_is_encrypted != 0 || (stat.m_bit_flag & FLAG_ENCRYPTED) != 0;
    info.is_supported_method = stat.m_is_supported != 0
        && (stat.m_method == 0 || stat.m_method == MZ_DEFLATED);
    info.is_symlink          = (host == HOST_UNIX || host == HOST_DARWIN)
        && (unix_mode & UNIX_FILE_TYPE_MASK) == UNIX_SYMLINK;
    return info;
}

std::optional<SkipReason> check_entry(const EntryInfo& entry, const ArchiveLimits& limits)
{
    const std::string name = normalized_separators(entry.name);

    if (is_unsafe_path(name)) {
        return SkipReason::UnsafePath;
    }
    if (name.empty() || last_segment(name).empty() || has_control_characters(name)) {
        return SkipReason::InvalidName;
    }
    if (entry.is_symlink) {
        return SkipReason::Symlink;
    }
    if (entry.is_encrypted) {
        return SkipReason::Encrypted;
    }
    if (!entry.is_supported_method) {
        return SkipReason::UnsupportedCompression;
    }
    if (FileLoadingLogic::is_archive_file(name)) {
        return SkipReason::NestedArchive;
    }
    if (!FileLoadingLogic::is_supported_file(name)) {
        return SkipReason::Unsupported;
    }
    if (entry.uncompressed_size > limits.max_entry_size) {
        return SkipReason::TooLarge;
    }
    if (entry.uncompressed_size > limits.compression_ratio_min_size
        && (entry.compressed_size == 0
            || entry.uncompressed_size / entry.compressed_size > limits.max_compression_ratio))
    {
        return SkipReason::SuspiciousCompressionRatio;
    }
    return std::nullopt;
}

std::string sanitized_file_name(const std::string& entry_name)
{
    constexpr std::string_view forbidden = "<>:\"/\\|?*";
    constexpr std::size_t max_length     = 200;

    std::string name = make_valid_utf8(last_segment(entry_name));
    for (char& c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f || forbidden.find(c) != std::string_view::npos) {
            c = '_';
        }
    }

    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    if (name.empty() || name == "..") {
        name = "file";
    }

    static constexpr std::array<std::string_view, 22> reserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    const std::string stem = name.substr(0, name.find('.'));
    for (std::string_view device : reserved) {
        if (boost::algorithm::iequals(stem, device)) {
            name.insert(0, "_");
            break;
        }
    }

    if (name.size() > max_length) {
        const std::size_t dot = name.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string() : name.substr(dot);
        if (extension.size() > max_length / 2) {
            extension.clear();
        }
        std::size_t cut = max_length - extension.size();
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        name = name.substr(0, cut) + extension;
    }
    return name;
}

ExtractedArchive extract_archive(const fs::path& archive_path, const ArchiveLimits& limits, const fs::path& root)
{
    ExtractedArchive result;
    const std::string archive_name = archive_path.filename().string();

    ZipArchive archive = ZipArchive::open(archive_path.string(), ZipArchive::Mode::Read);
    if (!archive.is_open()) {
        result.errors.push_back(fmt::format(
            fmt::runtime(_u8L("Failed to open archive {}: {}")),
            archive_name,
            translate_miniz_error(archive.last_error())
        ));
        return result;
    }

    const int num_files = archive.get_num_files();
    if (num_files < 0 || static_cast<std::size_t>(num_files) > limits.max_entries) {
        result.errors.push_back(fmt::format(
            fmt::runtime(_u8L("Archive {} contains too many files.")),
            archive_name
        ));
        return result;
    }

    const tl::expected<fs::path, std::string> directory = create_extraction_directory(root);
    if (!directory) {
        result.errors.push_back(directory.error());
        return result;
    }
    result.directories.push_back(*directory);
    std::set<std::string> used_names;

    if (archive.locate_file("3D/3dmodel.model") >= 0) {
        archive.close();

        ArchiveFile file;
        file.archive    = archive_path;
        file.name       = archive_name;
        file.is_project = true;
        const fs::path target = unique_target(*directory, sanitized_file_name(archive_path.stem().string() + ".3mf"), used_names);
        boost::system::error_code ec;
        fs::copy_file(archive_path, target, ec);
        if (ec) {
            result.errors.push_back(fmt::format(
                fmt::runtime(_u8L("Failed to copy {}: {}")),
                archive_name,
                ec.message()
            ));
            file.skip_reason = SkipReason::ExtractionFailed;
        } else {
            file.path       = target;
            file.size       = fs::file_size(target, ec);
            file.has_config = is_project_3mf(target);
        }
        result.files.push_back(std::move(file));
        return result;
    }

    std::vector<int> indices;
    std::uint64_t total_size = 0;
    for (int index = 0; index < num_files; ++index) {
        mz_zip_archive_file_stat stat;
        if (!archive.stat_file(index, stat)) {
            ArchiveFile unreadable;
            unreadable.archive     = archive_path;
            unreadable.skip_reason = SkipReason::InvalidName;
            result.files.push_back(std::move(unreadable));
            indices.push_back(index);
            continue;
        }

        std::string name;
        if (!archive.get_filename_from_extra(index, name)) {
            name = stat.m_filename;
        }
        const EntryInfo info = entry_info_from_stat(stat, name);
        if (info.is_directory || is_junk_entry(info.name)) {
            continue;
        }

        ArchiveFile file;
        file.archive     = archive_path;
        file.name        = info.name;
        file.size        = info.uncompressed_size;
        file.is_project  = FileLoadingLogic::is_project_file(info.name);
        file.skip_reason = check_entry(info, limits);
        if (!file.skip_reason) {
            if (total_size + file.size > limits.max_total_size) {
                file.skip_reason = SkipReason::ArchiveLimitsExceeded;
            } else {
                total_size += file.size;
            }
        }
        result.files.push_back(std::move(file));
        indices.push_back(index);
    }

    boost::system::error_code ec;
    const fs::space_info space = fs::space(*directory, ec);
    const bool enough_space    = ec || space.available >= total_size;
    if (!enough_space) {
        result.errors.push_back(_u8L("There is not enough disk space to extract files from the archive."));
    }

    for (std::size_t i = 0; i < result.files.size(); ++i) {
        ArchiveFile& file = result.files[i];
        if (file.skip_reason) {
            continue;
        }
        if (!enough_space) {
            file.skip_reason = SkipReason::ExtractionFailed;
            continue;
        }

        const fs::path target = unique_target(*directory, sanitized_file_name(file.name), used_names);
        if (!archive.extract_to_file(indices[i], target.string())) {
            fs::remove(target, ec);
            result.errors.push_back(fmt::format(
                fmt::runtime(_u8L("Failed to extract {}: {}")),
                file.name,
                translate_miniz_error(archive.last_error())
            ));
            file.skip_reason = SkipReason::ExtractionFailed;
            continue;
        }
        file.path       = target;
        file.has_config = file.is_project && is_project_3mf(target);
    }
    return result;
}

void remove_stale_extractions(std::chrono::hours max_age, const fs::path& root)
{
    const fs::path directory = root_or_temp(root);
    const std::time_t now    = std::time(nullptr);
    const auto max_age_s     = std::chrono::duration_cast<std::chrono::seconds>(max_age).count();

    boost::system::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path path = it->path();
        if (!boost::algorithm::starts_with(path.filename().string(), DIRECTORY_PREFIX)) {
            continue;
        }

        boost::system::error_code entry_ec;
        if (!fs::is_directory(path, entry_ec)) {
            continue;
        }
        const std::time_t modified = fs::last_write_time(path, entry_ec);
        if (entry_ec || now - modified < max_age_s) {
            continue;
        }

        fs::remove_all(path, entry_ec);
        if (entry_ec) {
            SPDLOG_WARN("Failed to remove stale temporary directory {}: {}", path.string(), entry_ec.message());
        } else {
            SPDLOG_INFO("Removed stale temporary directory {}", path.string());
        }
    }
}

void FileOpeningInteractor::open(std::vector<fs::path> paths, OpenFilesOptions options)
{
    std::vector<fs::path> archives;
    std::erase_if(paths, [&archives](const fs::path& path) {
        if (!FileLoadingLogic::is_archive_file(path.string())) {
            return false;
        }
        archives.push_back(path);
        return true;
    });
    if (!archives.empty()) {
        open_archives(std::move(archives), options);
    }
    if (paths.empty()) {
        return;
    }

    if (!options.as_geometry && paths.size() == 1 && FileLoadingLogic::is_project_file(paths.front().string())) {
        m_project_interactor.load_project(
            paths.front(),
            options.new_project ? EmptyProjectAction::Replace : EmptyProjectAction::Keep
        );
        return;
    }

    import_models(std::move(paths), options.new_project, false);
}

void FileOpeningInteractor::open_archives(std::vector<fs::path> archives, OpenFilesOptions options)
{
    Platform::PlatformServices::instance()
        .job_manager()
        .create_job(
            "archive_extraction_" + std::to_string(++m_extraction_counter),
            [archives](JThread::StopToken)
            {
                ExtractedArchive result;
                for (const fs::path& archive : archives) {
                    ExtractedArchive extracted = extract_archive(archive);
                    result.files.insert(result.files.end(), extracted.files.begin(), extracted.files.end());
                    result.errors.insert(result.errors.end(), extracted.errors.begin(), extracted.errors.end());
                    result.directories.insert(
                        result.directories.end(),
                        extracted.directories.begin(),
                        extracted.directories.end()
                    );
                }
                return result;
            }
        )
        .on_result(
            [this, archives, options](ExtractedArchive extracted)
            {
                if (!extracted.errors.empty()) {
                    std::string message;
                    for (const std::string& error : extracted.errors) {
                        message += (message.empty() ? "" : "\n") + error;
                    }
                    report_archive_error(message);
                }

                const auto openable = std::count_if(
                    extracted.files.begin(),
                    extracted.files.end(),
                    [](const ArchiveFile& file) { return !file.skip_reason; }
                );
                if (openable > 1 && m_archive_dialog) {
                    const bool shown = m_archive_dialog(
                        std::move(extracted.files),
                        options,
                        [this, options, directories = extracted.directories](std::optional<ArchiveSelection> selection)
                        {
                            if (selection) {
                                open_archive_selection(std::move(*selection), options.new_project, directories);
                            } else {
                                remove_directories(directories);
                            }
                        }
                    );
                    if (!shown) {
                        remove_directories(extracted.directories);
                        invoke_listeners<IFileOpenEventListener>([&archives, &options](IFileOpenEventListener* l) {
                            l->on_archive_postponed(archives, options);
                        });
                    }
                    return;
                }

                const auto skipped = std::count_if(
                    extracted.files.begin(),
                    extracted.files.end(),
                    [](const ArchiveFile& file)
                    { return file.skip_reason && file.skip_reason != SkipReason::ExtractionFailed; }
                );
                if (skipped > 0) {
                    invoke_listeners<IFileOpenEventListener>([skipped](IFileOpenEventListener* l) {
                        l->on_archive_files_skipped(static_cast<std::size_t>(skipped));
                    });
                }
                if (openable == 0) {
                    remove_directories(extracted.directories);
                    if (extracted.errors.empty()) {
                        report_archive_error(_u8L("The archive does not contain any files which could be opened."));
                    }
                    return;
                }

                ArchiveSelection selection;
                selection.models_to_new_project = options.new_project;
                for (const ArchiveFile& file : extracted.files) {
                    if (!file.skip_reason) {
                        (file.is_project && !options.as_geometry ? selection.projects : selection.models)
                            .push_back(file.path);
                    }
                }
                open_archive_selection(std::move(selection), options.new_project, std::move(extracted.directories));
            }
        )
        .on_exception(
            [this](std::exception_ptr eptr)
            {
                std::string description = "Unknown error";
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    description = e.what();
                } catch (...) {
                }
                report_archive_error(fmt::format(fmt::runtime(_u8L("Failed to open archive: {}")), description));
            }
        )
        .start();
}

void FileOpeningInteractor::open_archive_selection(
    ArchiveSelection selection,
    bool replace_empty_project,
    std::vector<fs::path> directories
)
{
    if (!selection.models.empty()) {
        import_models(std::move(selection.models), selection.models_to_new_project, true);
    }
    if (selection.projects.empty()) {
        remove_directories(directories);
        return;
    }
    m_project_interactor.arrange_interactor().run_when_added_arranged(
        [this, projects = std::move(selection.projects), replace_empty_project, directories = std::move(directories)]()
        { open_archive_projects(projects, replace_empty_project, directories); }
    );
}

void FileOpeningInteractor::open_archive_projects(
    std::vector<fs::path> projects,
    bool replace_empty_project,
    std::vector<fs::path> directories
)
{
    if (projects.empty()) {
        remove_directories(directories);
        return;
    }
    const fs::path file = projects.front();
    projects.erase(projects.begin());
    m_project_interactor.load_project(
        file,
        replace_empty_project ? EmptyProjectAction::Replace : EmptyProjectAction::Keep,
        true,
        [this, projects = std::move(projects), directories = std::move(directories)]()
        { open_archive_projects(projects, false, directories); }
    );
}

void FileOpeningInteractor::import_models(std::vector<fs::path> files, bool to_new_project, bool temporary_files)
{
    if (to_new_project && !m_project_interactor.selected_project_is_empty()) {
        m_project_interactor.new_project();
    }
    m_project_interactor.load_models_to_project(std::move(files), temporary_files);
}

void FileOpeningInteractor::report_archive_error(const std::string& message)
{
    SPDLOG_WARN(message);
    invoke_listeners<IFileOpenEventListener>([&message](IFileOpenEventListener* l) {
        l->on_archive_open_failed(message);
    });
}

} // namespace Slic3r::Biz::FileOpen
