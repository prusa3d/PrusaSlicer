#include "TestDialogs.hpp"

#include <Slic3r/App/WX/StringConversions.hpp>
#include <Slic3r/Log.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/core/demangle.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/html/htmlwin.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/stockitem.h>

#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace Slic3r::App::Desktop::Testing {

namespace {

struct Button
{
    int id{wxID_NONE};
    std::string label;
};

struct DialogContent
{
    std::string title;
    std::string message;
    std::vector<Button> buttons;
};

const std::vector<std::pair<std::string, int>> standard_buttons{
    {"ok", wxID_OK},
    {"cancel", wxID_CANCEL},
    {"yes", wxID_YES},
    {"no", wxID_NO},
    {"close", wxID_CLOSE},
    {"apply", wxID_APPLY},
    {"help", wxID_HELP},
};

// What an unexpected dialog gets, in order of preference.
const std::vector<int> fallback_buttons{wxID_CANCEL, wxID_CLOSE, wxID_NO, wxID_OK};

void append_line(std::string& text, const std::string& line)
{
    if (line.empty()) {
        return;
    }
    if (!text.empty()) {
        text += "\n";
    }
    text += line;
}

std::string button_label(int id, const wxString& label)
{
    // wxGTK message dialogs report their default labels as GTK stock ids, e.g. "gtk-yes".
    if (label.empty() || label.StartsWith(WX::from_u8("gtk-"))) {
        return WX::into_u8(wxGetStockLabel(id, wxSTOCK_NOFLAGS));
    }
    return WX::into_u8(wxStripMenuCodes(label, wxStrip_Mnemonics));
}

std::string message_button_label(int id, const wxString& label)
{
    std::string result = button_label(id, label);
#ifdef __WXGTK__
    // Custom labels of wxGTK message dialogs have GTK mnemonics.
    std::erase(result, '_');
#endif
    return result;
}

void collect(wxWindow& window, DialogContent& content)
{
    for (wxWindow* child : window.GetChildren()) {
        if (auto* button = dynamic_cast<wxButton*>(child)) {
            content.buttons.push_back(
                {button->GetId(), button_label(button->GetId(), button->GetLabel())}
            );
        } else if (auto* text = dynamic_cast<wxStaticText*>(child)) {
            append_line(content.message, WX::into_u8(text->GetLabel()));
        } else if (auto* html = dynamic_cast<wxHtmlWindow*>(child)) {
            append_line(content.message, WX::into_u8(html->ToText()));
        }
        collect(*child, content);
    }
}

DialogContent describe(wxDialog& dialog)
{
    DialogContent content;
    content.title = WX::into_u8(dialog.GetTitle());

    // Native message dialogs (all of them on Linux and macOS) have no wx children to look at.
    if (auto* message = dynamic_cast<wxMessageDialogBase*>(&dialog)) {
        content.message = WX::into_u8(message->GetMessage());
        append_line(content.message, WX::into_u8(message->GetExtendedMessage()));
        const long style = message->GetMessageDialogStyle();
        if (style & wxYES) {
            content.buttons.push_back(
                {wxID_YES, message_button_label(wxID_YES, message->GetYesLabel())}
            );
        }
        if (style & wxNO) {
            content.buttons.push_back(
                {wxID_NO, message_button_label(wxID_NO, message->GetNoLabel())}
            );
        }
        if (style & wxOK) {
            content.buttons.push_back(
                {wxID_OK, message_button_label(wxID_OK, message->GetOKLabel())}
            );
        }
        if (style & wxCANCEL) {
            content.buttons.push_back(
                {wxID_CANCEL, message_button_label(wxID_CANCEL, message->GetCancelLabel())}
            );
        }
        if (style & wxHELP) {
            content.buttons.push_back(
                {wxID_HELP, message_button_label(wxID_HELP, message->GetHelpLabel())}
            );
        }
    } else {
        collect(dialog, content);
    }
    return content;
}

bool has_button(const DialogContent& content, int id)
{
    for (const Button& button : content.buttons) {
        if (button.id == id) {
            return true;
        }
    }
    return false;
}

int find_button(const DialogContent& content, const std::string& name)
{
    const std::string wanted = boost::algorithm::to_lower_copy(name);
    for (const Button& button : content.buttons) {
        if (boost::algorithm::to_lower_copy(button.label) == wanted) {
            return button.id;
        }
    }
    for (const auto& [standard_name, id] : standard_buttons) {
        if (standard_name == wanted && has_button(content, id)) {
            return id;
        }
    }
    return wxID_NONE;
}

int fallback_button(const DialogContent& content)
{
    for (const int id : fallback_buttons) {
        if (has_button(content, id)) {
            return id;
        }
    }
    return wxID_CANCEL;
}

std::string label_of(const DialogContent& content, int id)
{
    for (const Button& button : content.buttons) {
        if (button.id == id) {
            return button.label;
        }
    }
    for (const auto& [standard_name, standard_id] : standard_buttons) {
        if (standard_id == id) {
            return standard_name;
        }
    }
    return std::to_string(id);
}

bool contains_ignoring_case(const std::string& text, const std::string& part)
{
    return WX::from_u8(text).Lower().Find(WX::from_u8(part).Lower()) != wxNOT_FOUND;
}

// Dialogs nobody named have the wx default name, or none at all.
nlohmann::json dialog_name(const wxString& name)
{
    if (name.empty() || name == WX::from_u8(wxDialogNameStr)) {
        return nullptr;
    }
    return WX::into_u8(name);
}

std::string name_error(const nlohmann::json& answer, const nlohmann::json& name)
{
    if (answer.contains("name") && answer["name"] != name) {
        return "The next answer expects the name '" + answer["name"].get<std::string>() + "'";
    }
    return "";
}

std::string file_dialog_mode(FileDialogType type)
{
    switch (type) {
    case FileDialogType::Open:
        return "open";
    case FileDialogType::OpenMultiple:
        return "open_multiple";
    case FileDialogType::Save:
        return "save";
    }
    return "unknown";
}

void validate_answer(const nlohmann::json& answer)
{
    if (!answer.is_object()) {
        throw std::invalid_argument("Every answer must be a JSON object");
    }
    if (!answer.contains("kind") || !answer["kind"].is_string()) {
        throw std::invalid_argument("Field 'kind' must be a string");
    }
    if (answer.contains("name")
        && (!answer["name"].is_string() || answer["name"].get_ref<const std::string&>().empty()))
    {
        throw std::invalid_argument("Field 'name' must be a non-empty string");
    }
    const std::string& kind = answer["kind"].get_ref<const std::string&>();
    if (kind == "message") {
        if (!answer.contains("button")
            || !answer["button"].is_string()
            || answer["button"].get_ref<const std::string&>().empty())
        {
            throw std::invalid_argument("Field 'button' must be a non-empty string");
        }
        if (answer.contains("text") && !answer["text"].is_string()) {
            throw std::invalid_argument("Field 'text' must be a string");
        }
    } else if (kind == "file") {
        const bool cancel = answer.contains("cancel");
        if (cancel == answer.contains("paths")) {
            throw std::invalid_argument("A file answer needs exactly one of 'paths' and 'cancel'");
        }
        if (cancel && answer["cancel"] != true) {
            throw std::invalid_argument("Field 'cancel' must be true");
        }
        if (!cancel) {
            const nlohmann::json& paths = answer["paths"];
            if (!paths.is_array() || paths.empty()) {
                throw std::invalid_argument("Field 'paths' must be a non-empty array");
            }
            for (const nlohmann::json& path : paths) {
                if (!path.is_string()) {
                    throw std::invalid_argument("Field 'paths' must contain strings");
                }
            }
        }
    } else {
        throw std::invalid_argument("Unknown answer kind '" + kind + "'");
    }
}

} // namespace

void TestDialogs::add_answers(const nlohmann::json& answers)
{
    if (!answers.is_array()) {
        throw std::invalid_argument("Field 'answers' must be an array");
    }
    for (const nlohmann::json& answer : answers) {
        validate_answer(answer);
    }
    std::lock_guard lock{m_mutex};
    for (const nlohmann::json& answer : answers) {
        m_answers.push_back(answer);
    }
}

void TestDialogs::clear()
{
    std::lock_guard lock{m_mutex};
    m_answers.clear();
    m_log = nlohmann::json::array();
}

nlohmann::json TestDialogs::to_json() const
{
    std::lock_guard lock{m_mutex};
    nlohmann::json pending = nlohmann::json::array();
    for (const nlohmann::json& answer : m_answers) {
        pending.push_back(answer);
    }
    return {{"log", m_log}, {"pending", pending}};
}

int TestDialogs::answer_modal(wxDialog& dialog)
{
    const DialogContent content = describe(dialog);
    nlohmann::json buttons      = nlohmann::json::array();
    for (const Button& button : content.buttons) {
        buttons.push_back(button.label);
    }
    nlohmann::json record{
        {"kind", "message"},
        {"name", dialog_name(dialog.GetName())},
        {"class", boost::core::demangle(typeid(dialog).name())},
        {"title", content.title},
        {"message", content.message},
        {"buttons", buttons},
    };

    std::lock_guard lock{m_mutex};
    std::string error;
    int result{wxID_NONE};
    if (m_answers.empty()) {
        error = "No answer was prepared";
    } else if (m_answers.front()["kind"] != "message") {
        error = "The next answer is for a file dialog";
    } else if (!name_error(m_answers.front(), record["name"]).empty()) {
        error = name_error(m_answers.front(), record["name"]);
    } else if (
        m_answers.front().contains("text")
        && !contains_ignoring_case(
            content.title + "\n" + content.message,
            m_answers.front()["text"].get<std::string>()
        )
    )
    {
        error = "The next answer expects the text '"
            + m_answers.front()["text"].get<std::string>()
            + "'";
    } else {
        const std::string button = m_answers.front()["button"].get<std::string>();
        m_answers.pop_front();
        result = find_button(content, button);
        if (result == wxID_NONE) {
            error = "The dialog has no button '" + button + "'";
        }
    }

    if (!error.empty()) {
        result          = fallback_button(content);
        record["error"] = error;
    }
    record["answer"]   = label_of(content, result);
    record["expected"] = error.empty();
    m_log.push_back(record);

    SPDLOG_INFO(
        "TESTING MODE: dialog '{}' answered '{}'{}",
        content.title,
        record["answer"].get<std::string>(),
        error.empty() ? "" : " (" + error + ")"
    );
    return result;
}

std::optional<std::vector<boost::filesystem::path>>
TestDialogs::answer_file_dialog(FileDialogType type, const std::string& title)
{
    nlohmann::json record{
        {"kind", "file"},
        {"name", WX::file_dialog_name(type)},
        {"mode", file_dialog_mode(type)},
        {"title", title},
    };

    std::lock_guard lock{m_mutex};
    std::string error;
    std::optional<std::vector<boost::filesystem::path>> result;
    if (m_answers.empty()) {
        error = "No answer was prepared";
    } else if (m_answers.front()["kind"] != "file") {
        error = "The next answer is for a message dialog";
    } else if (!name_error(m_answers.front(), record["name"]).empty()) {
        error = name_error(m_answers.front(), record["name"]);
    } else {
        const nlohmann::json answer = m_answers.front();
        m_answers.pop_front();
        if (answer.contains("paths")) {
            if (type != FileDialogType::OpenMultiple && answer["paths"].size() != 1) {
                error = "This file dialog takes exactly one path";
            } else {
                result.emplace();
                for (const nlohmann::json& path : answer["paths"]) {
                    result->push_back(WX::into_path(WX::from_u8(path.get<std::string>())));
                }
                record["paths"] = answer["paths"];
            }
        }
    }

    if (!error.empty()) {
        record["error"] = error;
    }
    record["answer"]   = result ? "ok" : "cancel";
    record["expected"] = error.empty();
    m_log.push_back(record);

    SPDLOG_INFO(
        "TESTING MODE: file dialog '{}' answered '{}'{}",
        title,
        record["answer"].get<std::string>(),
        error.empty() ? "" : " (" + error + ")"
    );
    return result;
}

ModalHook::ModalHook(std::shared_ptr<TestDialogs> dialogs) : m_dialogs(std::move(dialogs)) {}

int ModalHook::Enter(wxDialog* dialog)
{
    if (dialog == nullptr) {
        return wxID_CANCEL;
    }
    return m_dialogs->answer_modal(*dialog);
}

TestDialogManager::TestDialogManager(std::shared_ptr<TestDialogs> dialogs) :
    m_dialogs(std::move(dialogs))
{}

void TestDialogManager::show_file_dialog(
    FileDialogType dialog_type,
    const std::string& dialog_title,
    const boost::filesystem::path&,
    const std::string&,
    const std::string&,
    const FileCallback& callback
)
{
    const std::optional<std::vector<boost::filesystem::path>> paths =
        m_dialogs->answer_file_dialog(dialog_type, dialog_title);
    if (paths) {
        callback(true, *paths);
    } else {
        callback(false, {});
    }
}

} // namespace Slic3r::App::Desktop::Testing
