#pragma once

#include <Slic3r/App/WX/DialogManager.hpp>

#include <nlohmann/json.hpp>

#include <wx/modalhook.h>

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class wxDialog;

namespace Slic3r::App::Desktop::Testing {

/**
 * @brief Answers the wx dialogs of a slicer in test mode, so that no wx dialog is ever shown.
 *
 * Tests queue answers through the server, the dialogs take them in order. A dialog without a
 * matching answer is cancelled. Every dialog is logged, so the tests can find the unexpected ones.
 * Used from the main thread, worker threads (dialogs opened by jobs) and the server thread.
 */
class TestDialogs
{
public:
    /**
     * @brief Validates all answers first, then queues them. Throws std::invalid_argument.
     *
     * An answer is `{"kind": "message", "button": "<label or yes|no|ok|cancel|...>", "text":
     * "<optional substring of the title or message, any case>"}` or `{"kind": "file", "paths": [...]}` or
     * `{"kind": "file", "cancel": true}`. Any answer can have `"name"`, the name the dialog must
     * have (wxWindow::SetName).
     */
    void add_answers(const nlohmann::json& answers);
    void clear();
    nlohmann::json to_json() const;

    /**
     * @brief Returns the code ShowModal of the dialog returns.
     */
    int answer_modal(wxDialog& dialog);

    /**
     * @brief Returns the chosen paths, or none when the dialog is cancelled.
     */
    std::optional<std::vector<boost::filesystem::path>>
    answer_file_dialog(FileDialogType type, const std::string& title);

private:
    mutable std::mutex m_mutex;
    std::deque<nlohmann::json> m_answers;
    nlohmann::json m_log = nlohmann::json::array();
};

class ModalHook : public wxModalDialogHook
{
public:
    explicit ModalHook(std::shared_ptr<TestDialogs> dialogs);

protected:
    int Enter(wxDialog* dialog) override;

private:
    std::shared_ptr<TestDialogs> m_dialogs;
};

/**
 * @brief The file dialogs are answered here, not by ModalHook: the paths are read from a dialog
 * after ShowModal, which doesn't work for a native dialog that was never shown.
 */
class TestDialogManager : public WX::DialogManager
{
public:
    explicit TestDialogManager(std::shared_ptr<TestDialogs> dialogs);

    void show_file_dialog(
        FileDialogType dialog_type,
        const std::string& dialog_title,
        const boost::filesystem::path& override_dir,
        const std::string& default_file_name,
        const std::string& wildcards,
        const FileCallback& callback
    ) override;

private:
    std::shared_ptr<TestDialogs> m_dialogs;
};

} // namespace Slic3r::App::Desktop::Testing
