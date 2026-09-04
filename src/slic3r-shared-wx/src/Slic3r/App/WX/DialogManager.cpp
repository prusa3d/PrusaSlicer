#include "Slic3r/App/WX/DialogManager.hpp"

#include "Slic3r/App/WX/WebView/WebViewFactory.hpp"
#include "Slic3r/App/WX/MsgDialog.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/DiffDialog.hpp"
#include "Slic3r/App/WX/UnsavedChangesDialog.hpp"
#include "Slic3r/App/WX/RammingDialog.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/LoadStepDialog.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include <Slic3r/App/WX/I18N.hpp>
#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Log.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/WX/SavePresetDialog.hpp"

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/string.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>
#include <wx/app.h>
#include <wx/utils.h>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

namespace Slic3r::App::WX {

void
DialogManager::show_file_dialog(FileDialogType dialog_type, const std::string& dialog_title, const boost::filesystem::path& override_dir, const std::string& default_file_name, const std::string& wildcards, const FileCallback& callback)
{
    ASSERT(callback);

    int flags = 0;

    switch (dialog_type) {
    case FileDialogType::Open:
        flags |= wxFD_OPEN;
        break;
    case FileDialogType::OpenMultiple:
        flags |= wxFD_OPEN | wxFD_MULTIPLE;
        break;
    case FileDialogType::Save:
        flags |= wxFD_SAVE | wxFD_OVERWRITE_PROMPT;
        break;
    }

    boost::filesystem::path dir;
    boost::system::error_code ec; 
    if (!override_dir.empty() && boost::filesystem::exists(override_dir, ec) && boost::filesystem::is_directory(override_dir, ec)) {
        dir = override_dir;
    } else if (!m_last_dir.empty() && boost::filesystem::exists(m_last_dir, ec) && boost::filesystem::is_directory(m_last_dir, ec)) {
        dir = m_last_dir;
    }
    wxFileDialog dlg(nullptr, from_u8(dialog_title), from_u8(dir.string()), from_u8(default_file_name), from_u8(wildcards), flags);

    if (dlg.ShowModal() != wxID_OK) {
        callback(false, {});
        return;
    }

    std::vector<boost::filesystem::path> out_paths;
    if (dialog_type == FileDialogType::OpenMultiple) {
        wxArrayString paths;
        dlg.GetPaths(paths);

        for (const wxString& path : paths) {
            out_paths.emplace_back(into_path(path));
        }
    } else {
        out_paths.emplace_back(into_path(dlg.GetPath()));
    }

    // Ensure there is an extension when saving file
    if (dialog_type == FileDialogType::Save) {
        auto& path = out_paths.back();
        if (!path.has_extension()) {
            // Selected filter in dialog
            int filter_index = dlg.GetFilterIndex();
            // Split wildcard to tokens
            wxStringTokenizer tokenizer(from_u8(wildcards), L"|");
            wxString ext_token;
            // Find the extension string corresponding to the filter index
            // The extensions are at odd positions (1, 3, 5...)
            for (int i = 0; i <= filter_index * 2 + 1; ++i) {
                ext_token = tokenizer.GetNextToken();
            }
            // Take only first extension
            wxString ext = wxString(ext_token).AfterFirst('*').BeforeFirst(';');
            path.replace_extension(into_u8(ext));
        }
        
        // Store last used extension
        AppServices::instance().app_config().set<std::string>("last_used_extension", path.extension().string());
    }

    if (!out_paths.empty()) {
        if (boost::filesystem::is_directory(out_paths.front(), ec)) {
            m_last_dir = out_paths.front();
        } else if (boost::filesystem::exists(out_paths.front().parent_path(), ec)) {
            m_last_dir = out_paths.front().parent_path();
        } else {
            SPDLOG_ERROR("Failed to resolve new default dialog path: {}", ec.message());
        }
    }
    AppServices::instance().app_config().set<std::string>("last_used_directory", m_last_dir.string());
    callback(true, out_paths);
}


void DialogManager::show_webview_dialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, Biz::ProjectInteractor* project_interactor)
{
    std::unique_ptr<WebView::AbstractWebViewDialog> dlg = WebView::new_web_view_dialog(std::move(logic));
    project_interactor->user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(dlg.get());
    dlg->ShowModal();
    project_interactor->user_account_interactor().remove_listener<Biz::UserAccount::IUserAccountListener>(dlg.get());
}

void DialogManager::show_upload_webview_dialog(std::unique_ptr<App::Browser::AbstractUploadBrowserLogic>&& logic, Biz::ProjectInteractor* project_interactor, const UploadCallback& callback)
{
    App::Browser::AbstractUploadBrowserLogic* logic_ptr = logic.get();
    std::unique_ptr<WebView::AbstractWebViewDialog> dlg = WebView::new_web_view_dialog(std::move(logic));
    project_interactor->user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(dlg.get());
    dlg->ShowModal();
    project_interactor->user_account_interactor().remove_listener<Biz::UserAccount::IUserAccountListener>(dlg.get());
    callback(logic_ptr->success(), logic_ptr->result_data());
}


void DialogManager::show_yesno_dialog(const std::string& title, const std::string& text, const YesNoCallback& callback)
{
    MessageDialog dlg(wxTheApp->GetTopWindow(), from_u8(text), from_u8(title), wxYES_NO);
    dlg.CenterOnParent();
    if (dlg.ShowModal() == wxID_YES)
        callback(true);
    else
        callback(false);
}

void DialogManager::show_yesnocancel_dialog(
    const std::string& title,
    const std::string& text,
    const Button& yes,
    const Button& no,
    const Button& cancel
)
{
    MessageDialog
        dialog{nullptr, from_u8(text), from_u8(title), wxYES_NO | wxCANCEL | wxICON_EXCLAMATION};

    dialog.SetYesNoCancelLabels(from_u8(yes.first), from_u8(no.first), from_u8(cancel.first));

    switch (dialog.ShowModal()) {
    case wxID_YES:
        if (yes.second) {
            yes.second();
        }
        break;
    case wxID_NO:
        if (no.second) {
            no.second();
        }
        break;
    case wxID_CANCEL:
    default:
        if (cancel.second) {
            cancel.second();
        }
        break;
    }
}

void DialogManager::show_rich_yesno_dialog(const std::string& title, const std::string& text, const std::string& check_text, const YesNoCallback& callback, const CheckBoxCheckedCallback& cbc_callback)
{
    RichMessageDialog dlg(wxTheApp->GetTopWindow(), from_u8(text), from_u8(title), wxICON_QUESTION | wxYES_NO);
    dlg.ShowCheckBox(from_u8(check_text));
    if (dlg.ShowModal() == wxID_YES)
        callback(true);
    else
        callback(false);

    if (dlg.IsCheckBoxChecked())
        cbc_callback(true);
}

void DialogManager::show_info_dialog(const std::string& text, const std::string& title, bool is_marked)
{
    // Use CallAfter because most of InfoDialog call we have from the thread.
    wxTheApp->CallAfter([text, title, is_marked] {
        InfoDialog(nullptr, from_u8(title), from_u8(text), is_marked)
            .ShowModal();
        });
}

void DialogManager::show_warning_dialog(const std::string& text, const std::string& title)
{
    MessageDialog(nullptr, from_u8(text), title.empty() ? _L("Warning") : from_u8(title), wxICON_WARNING | wxOK)
        .ShowModal();
}

void DialogManager::show_error_dialog(const std::string& text, const std::string& title)
{
    MessageDialog(nullptr, from_u8(text), title.empty() ? _L("Error") : from_u8(title), wxICON_ERROR | wxOK)
        .ShowModal();
}
std::string DialogManager::show_input_dialog(const std::string& title, const std::string& text, const std::string& default_value)
{
    wxTextEntryDialog dialog(NULL, from_u8(text), from_u8(title), default_value.empty() ? wxString(wxEmptyString) : from_u8(default_value));
    //WX::w_config()->UpdateDlgDarkUI(&dialog);
    if (dialog.ShowModal() == wxID_OK){
        return into_u8(dialog.GetValue());
    }
    return {};
}

void DialogManager::show_input_dialog_with_buttons(
        const std::string& title,
        const std::string& text,
        const std::string& default_value,
        const std::vector<ButtonWithCallback>& buttons
    )
{    
    wxDialog dialog(nullptr, wxID_ANY, from_u8(title), wxDefaultPosition, wxDefaultSize);
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* label = new wxStaticText(&dialog, wxID_ANY, from_u8(text));
    main_sizer->Add(label, 0, wxALL, 10);

    wxTextCtrl* text_ctrl = new wxTextCtrl(&dialog, wxID_ANY, from_u8(default_value));
    text_ctrl->SetMinSize(wxSize(dialog.FromDIP(400), -1));
    main_sizer->Add(text_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    std::function<void(const std::string&)> chosen_callback;
    std::string entered_value;

    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    for (const auto& btn_data : buttons) {
        wxButton* btn = new wxButton(&dialog, wxID_ANY, from_u8(btn_data.text));

        btn->Bind(wxEVT_BUTTON, [&dialog, text_ctrl, &chosen_callback, &entered_value, cb = btn_data.callback](wxCommandEvent&) {
            chosen_callback = cb;
            entered_value   = into_u8(text_ctrl->GetValue());
            dialog.EndModal(wxID_OK);
        });

        button_sizer->Add(btn, 0, wxALL, 5);
    }
    wxButton* cancel_btn = new wxButton(&dialog, wxID_CANCEL);
    button_sizer->Add(cancel_btn, 0, wxALL, 5);
    main_sizer->Add(button_sizer, 0, wxALIGN_RIGHT | wxBOTTOM, 5);

    dialog.SetSizerAndFit(main_sizer);
    dialog.CenterOnParent();
    text_ctrl->SetFocus();
    dialog.ShowModal();

    // Invoked only after the dialog is gone - a callback may open another modal dialog.
    if (chosen_callback) {
        chosen_callback(entered_value);
    }
}

std::string DialogManager::show_combo_dialog(
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& values
    )
{
    wxArrayString choices;
    choices.Alloc(values.size());
    for (const auto& val : values) {
        choices.Add(from_u8(val));
    }

    wxSingleChoiceDialog dialog(
        nullptr, 
        from_u8(text), 
        from_u8(title), 
        choices
    );
    
    if (dialog.ShowModal() == wxID_OK) {
        return into_u8(dialog.GetStringSelection());
    }

    return {};
}

void DialogManager::show_diff_dialog(const Slic3r::Biz::Preset::PresetInteractor& preset_interactor, std::optional<Domain::Preset::PresetKind> kind)
{
    DiffDialog(preset_interactor, kind).ShowModal();
}

Biz::Preset::IPresetDialogManager::PresetsSwitchStates DialogManager::show_unsaved_changes_dialog(
    const std::string& dialog_name,
    const Domain::ConfigPack& config_original,
    const Domain::ConfigPack& config_selected,
    Domain::ConfigPack* config_new_selected,
    const Slic3r::Biz::Preset::PresetSelectionNames& preset_names,
    const Slic3r::Biz::Preset::PresetSelectionNames& preset_names_new,
    const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
    bool new_printer_has_multiple_extruders
)
{
    UnsavedChangesDialog dlg(
        dialog_name,
        config_original,
        config_selected,
        config_new_selected,
        preset_names,
        preset_names_new,
        preset_interactor,
        new_printer_has_multiple_extruders
    );
    dlg.ShowModal();
    return dlg.exit_states();
}

std::string DialogManager::show_save_dialog(
    Domain::Preset::PresetKind kind,
    const std::string& original_name,
    const Biz::Preset::PresetInteractor& preset_interactor
)
{
    const SavePresetDialog::NamesPerKindMap kind_name{{kind, {original_name}}};
    SavePresetDialog save_dlg(nullptr, kind_name, preset_interactor, "");

    if (save_dlg.ShowModal() == wxID_OK)
        return save_dlg.get_name();
    else
        return "";
}

Biz::Preset::IPresetDialogManager::NamesPerKindMap DialogManager::show_save_print_tool_dialog(
    const NamesPerKindMap& original_names_per_kind,
    const Biz::Preset::PresetInteractor& preset_interactor
)
{
    using namespace Domain::Preset;
    ASSERT(
        original_names_per_kind.at(PresetKind::FdmPrint).size() == 1
        && original_names_per_kind.at(PresetKind::FdmToolPrint).size() > 1
    );
    SavePresetDialog save_dlg(nullptr, original_names_per_kind, preset_interactor, "");

    if (save_dlg.ShowModal() == wxID_OK)
        return save_dlg.get_names_per_kind();
    else
        return {};
}

std::string DialogManager::show_ramming_dialog(const std::string& ramming_parameters)
{
    RammingDialog dlg(ramming_parameters);
    if (dlg.ShowModal() == wxID_OK) {
        return dlg.get_parameters();
    }
    return ramming_parameters;
}

std::optional<Biz::StepLoadDialogResult> DialogManager::show_load_step_dialog(
    const std::string& filename,
    double linear_precision,
    double angle_precision,
    bool multiple)
{
    LoadStepDialog dlg(filename, linear_precision, angle_precision, multiple);
    if (dlg.ShowModal() == wxID_OK) {
        Biz::StepLoadDialogResult result;
        result.do_not_show_again = dlg.IsCheckBoxChecked();
        result.linear_precision  = dlg.get_linear_precision();
        result.angle_precision   = dlg.get_angle_precision();
        result.apply_to_all      = dlg.IsApplyToAllClicked();
        return result;
    }
    return std::nullopt;
}

void DialogManager::open_in_browser(const std::string& link, int flag)
{
    wxLaunchDefaultBrowser(from_u8(link), flag);
}

} // namespace Slic3r::App::WX
