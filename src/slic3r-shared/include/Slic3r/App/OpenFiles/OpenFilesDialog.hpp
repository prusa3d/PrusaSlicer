#pragma once

#include "Slic3r/App/OpenFiles/OpenFilesDialogModel.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

#include <optional>
#include <vector>

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class OpenFilesSection;

/**
 * @brief Lets the user choose which files from an archive to open and how.
 * @note done is called exactly once, std::nullopt when cancelled or destroyed unanswered.
 */
class OpenFilesDialog : public Yoga::Dialog
{
public:
    using Done = Biz::FileOpen::ArchiveSelectionDone;

    OpenFilesDialog(
        std::vector<Biz::FileOpen::ArchiveFile> files,
        Biz::FileOpen::OpenFilesOptions options,
        Done done,
        Navigator& navigator
    );
    ~OpenFilesDialog() override;

protected:
    void close_action() override;

private:
    void update_controls();
    void answer(std::optional<Biz::FileOpen::ArchiveSelection> selection);

    OpenFilesDialogModel m_model;
    Done m_done;
    Navigator& m_navigator;

    std::vector<OpenFilesSection*> m_sections;
    Yoga::LayoutButton* m_open_button{nullptr};
};

} // namespace Slic3r::App
