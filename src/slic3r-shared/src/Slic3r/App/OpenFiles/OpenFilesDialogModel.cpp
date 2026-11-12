#include "Slic3r/App/OpenFiles/OpenFilesDialogModel.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>

namespace Slic3r::App {

using Biz::FileOpen::ArchiveFile;
using Biz::FileOpen::ArchiveSelection;
using Biz::FileOpen::SkipReason;

std::string format_file_size(std::uint64_t bytes)
{
    static constexpr std::array<const char*, 4> units = {"KB", "MB", "GB", "TB"};
    if (bytes < 1024) {
        return fmt::format("{} B", bytes);
    }
    double size = static_cast<double>(bytes) / 1024.;
    std::size_t unit = 0;
    while (size >= 1024. && unit + 1 < units.size()) {
        size /= 1024.;
        ++unit;
    }
    return fmt::format("{:.1f} {}", size, units[unit]);
}

std::string skip_reason_text(SkipReason reason)
{
    switch (reason) {
    case SkipReason::Unsupported: return Biz::_u8L("Unsupported file type");
    case SkipReason::Encrypted: return Biz::_u8L("Encrypted");
    case SkipReason::UnsupportedCompression: return Biz::_u8L("Unsupported compression");
    case SkipReason::Symlink: return Biz::_u8L("Symbolic link");
    case SkipReason::UnsafePath: return Biz::_u8L("Unsafe path");
    case SkipReason::InvalidName: return Biz::_u8L("Invalid name");
    case SkipReason::NestedArchive: return Biz::_u8L("Archive inside archive");
    case SkipReason::TooLarge: return Biz::_u8L("File too large");
    case SkipReason::SuspiciousCompressionRatio: return Biz::_u8L("Suspicious compression");
    case SkipReason::ArchiveLimitsExceeded: return Biz::_u8L("Archive too large");
    case SkipReason::ExtractionFailed: return Biz::_u8L("Extraction failed");
    }
    return {};
}

OpenFilesDialogModel::OpenFilesDialogModel(std::vector<ArchiveFile> files, Biz::FileOpen::OpenFilesOptions options) :
    m_files(std::move(files)),
    m_models_to_new_project(options.new_project)
{
    m_choices.reserve(m_files.size());
    for (const ArchiveFile& file : m_files) {
        m_choices.push_back({.selected = !file.skip_reason, .as_project = file.is_project && !options.as_geometry});
    }
}

bool OpenFilesDialogModel::selectable(std::size_t index) const
{
    return !m_files.at(index).skip_reason;
}

bool OpenFilesDialogModel::selected(std::size_t index) const
{
    return m_choices.at(index).selected;
}

void OpenFilesDialogModel::set_selected(std::size_t index, bool selected)
{
    if (selectable(index)) {
        m_choices.at(index).selected = selected;
    }
}

void OpenFilesDialogModel::select_all(bool selected)
{
    for (std::size_t index = 0; index < m_choices.size(); ++index) {
        set_selected(index, selected);
    }
}

std::size_t OpenFilesDialogModel::selected_count() const
{
    return std::count_if(m_choices.begin(), m_choices.end(), [](const Choice& choice) {
        return choice.selected;
    });
}

bool OpenFilesDialogModel::as_project(std::size_t index) const
{
    return m_choices.at(index).as_project;
}

void OpenFilesDialogModel::set_as_project(std::size_t index, bool as_project)
{
    if (m_files.at(index).is_project) {
        m_choices.at(index).as_project = as_project;
    }
}

bool OpenFilesDialogModel::imports_models() const
{
    return std::any_of(m_choices.begin(), m_choices.end(), [](const Choice& choice) {
        return choice.selected && !choice.as_project;
    });
}

ArchiveSelection OpenFilesDialogModel::to_selection() const
{
    ArchiveSelection selection;
    selection.models_to_new_project = m_models_to_new_project;
    for (std::size_t index = 0; index < m_files.size(); ++index) {
        const Choice& choice = m_choices[index];
        if (!choice.selected) {
            continue;
        }
        (choice.as_project ? selection.projects : selection.models).push_back(m_files[index].path);
    }
    return selection;
}

} // namespace Slic3r::App
