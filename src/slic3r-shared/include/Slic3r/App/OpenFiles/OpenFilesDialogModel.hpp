#pragma once

#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r::App {

std::string format_file_size(std::uint64_t bytes);

std::string skip_reason_text(Biz::FileOpen::SkipReason reason);

/**
 * @brief Editable choices of OpenFilesDialog.
 */
class OpenFilesDialogModel
{
public:
    OpenFilesDialogModel(std::vector<Biz::FileOpen::ArchiveFile> files, Biz::FileOpen::OpenFilesOptions options);

    const std::vector<Biz::FileOpen::ArchiveFile>& files() const
    {
        return m_files;
    }

    /**
     * @brief Skipped files can't be selected.
     */
    bool selectable(std::size_t index) const;
    bool selected(std::size_t index) const;
    void set_selected(std::size_t index, bool selected);
    void select_all(bool selected);
    std::size_t selected_count() const;

    /**
     * @brief Project files only, false imports the project as geometry.
     */
    bool as_project(std::size_t index) const;
    void set_as_project(std::size_t index, bool as_project);

    /**
     * @brief True when a selected file is imported as geometry, so the models target matters.
     */
    bool imports_models() const;

    bool models_to_new_project() const
    {
        return m_models_to_new_project;
    }
    void set_models_to_new_project(bool to_new_project)
    {
        m_models_to_new_project = to_new_project;
    }

    Biz::FileOpen::ArchiveSelection to_selection() const;

private:
    struct Choice
    {
        bool selected   = false;
        bool as_project = false;
    };

    std::vector<Biz::FileOpen::ArchiveFile> m_files;
    std::vector<Choice> m_choices;
    bool m_models_to_new_project = false;
};

} // namespace Slic3r::App
