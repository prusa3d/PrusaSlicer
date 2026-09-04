///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "UpdatesUIManager.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"
#include "slic3r/Utils/PresetUpdaterWrapper.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "format.hpp"

#include "Widgets/CheckBox.hpp"
#include <wx/wupdlock.h>
#include <wx/html/htmlwin.h>

namespace fs = boost::filesystem;

namespace Slic3r { 
namespace GUI {

RepositoryUpdateUIManager::RepositoryUpdateUIManager(wxWindow* parent, Slic3r::PresetUpdaterWrapper* puw, int em) :
    m_parent(parent)
    ,m_puw(puw)
    ,m_main_sizer(new wxBoxSizer(wxVERTICAL))
{
    auto online_label = new wxStaticText(m_parent, wxID_ANY, _L("Online sources"));
    online_label->SetFont(wxGetApp().bold_font().Scaled(1.3f));

    m_main_sizer->Add(online_label, 0, wxTOP | wxLEFT | wxBOTTOM, 2 * em);

    auto online_info = new wxStaticText(m_parent, wxID_ANY, _L("Please, select online sources you want to update profiles from") + ":");
    online_info->SetFont(wxGetApp().normal_font());

    m_main_sizer->Add(online_info, 0, wxLEFT, 3 * em);

    m_online_sizer = new wxFlexGridSizer(4, 0.75 * em, 1.5 * em);
    m_online_sizer->AddGrowableCol(2);
    m_online_sizer->AddGrowableCol(3);
    m_online_sizer->SetFlexibleDirection(wxBOTH);

    m_main_sizer->Add(m_online_sizer, 0, wxALL, 2 * em);

    m_main_sizer->AddSpacer(em);

    auto offline_label = new wxStaticText(m_parent, wxID_ANY, _L("Local sources"));
    offline_label->SetFont(wxGetApp().bold_font().Scaled(1.3f));

    m_main_sizer->Add(offline_label, 0, wxTOP | wxLEFT | wxBOTTOM, 2 * em);

    // append info line with link on printables.com
    {
        wxHtmlWindow* offline_info = new wxHtmlWindow(m_parent, wxID_ANY, wxDefaultPosition, wxSize(60 * em, 5 * em), wxHW_SCROLLBAR_NEVER);
        offline_info->SetBorders(0);

        offline_info->Bind(wxEVT_HTML_LINK_CLICKED, [](wxHtmlLinkEvent& event) {
            wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
            event.Skip(false);
        });

        const auto text_clr = wxGetApp().get_label_clr_default();
        const auto bgr_clr_str = wxGetApp().get_html_bg_color(m_parent->GetParent()->GetParent());
        const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));

        wxString message = format_wxstr(_L("As an alternative to online sources, profiles can also be updated by manually loading files containing the updates. "
           "This is mostly useful on computers that are not connected to the internet. "
           "Files containing the configuration updates can be downloaded from <a href=%1%>our website</a>."), "https://prusa.io/prusaslicer-profiles");

        const wxFont& font = m_parent->GetFont();
        const int fs = font.GetPointSize();
        int size[] = { fs,fs,fs,fs,fs,fs,fs };
        offline_info->SetFonts(font.GetFaceName(), font.GetFaceName(), size);

        offline_info->SetPage(format_wxstr("<html><body bgcolor=%1% link=%2%><font color=%2%>%3%</font></body></html>",
                             bgr_clr_str , text_clr_str , message ));

        m_main_sizer->Add(offline_info, 0, wxLEFT, 3 * em);
    }

    m_offline_sizer = new wxFlexGridSizer(7, 0.75 * em, 1.5 * em);
    m_offline_sizer->AddGrowableCol(1);
    m_offline_sizer->AddGrowableCol(2);
    m_offline_sizer->AddGrowableCol(4);
    m_offline_sizer->SetFlexibleDirection(wxHORIZONTAL);

    m_main_sizer->Add(m_offline_sizer, 0, wxALL, 2 * em);

    fill_entries(true);
    fill_grids();

    m_load_btn = new wxButton(m_parent, wxID_ANY, "  " + _L("Load") + "...  ");
    wxGetApp().UpdateDarkUI(m_load_btn, true);
    m_load_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) { load_offline_repos(); });
    m_main_sizer->Add(m_load_btn, 0, wxLEFT, 2 * em);

    m_main_sizer->Fit(parent);
}

void RepositoryUpdateUIManager::fill_entries(bool init_selection/* = false*/)
{
    m_online_entries.clear();
    m_offline_entries.clear();

    const SharedArchiveRepositoryVector&  archs = m_puw->get_all_archive_repositories();
    for (const auto* archive : archs) {
        const std::string&  uuid   = archive->get_uuid();
        if (init_selection && m_puw->is_selected_repository_by_uuid(uuid))
            m_selected_uuids.emplace(uuid);

        const bool  is_selected = m_selected_uuids.find(uuid) != m_selected_uuids.end();
        const auto& data        = archive->get_manifest();

        if (data.source_path.empty()) {
            // online repo
            m_online_entries.push_back({ is_selected, uuid, data.name, data.description, data.visibility, data.not_in_manifest });
        }
        else {
            // offline repo
            m_offline_entries.push_back({ is_selected, uuid, data.name, data.description, data.source_path.filename().string(), fs::exists(data.source_path), data.source_path });
        }
    }
}


void RepositoryUpdateUIManager::fill_grids()
{
    // clear grids
    m_online_sizer->Clear(true);
    m_offline_sizer->Clear(true);

    // Fill Online repository

    if (!m_online_entries.empty()) {

        auto add = [this](wxWindow* win) { m_online_sizer->Add(win, 0, wxALIGN_CENTER_VERTICAL); };

        // header

        // TRN: This string appears in Configuration Wizard in the 'Configuration Manager' step.
        for (const wxString& l : std::initializer_list<wxString>{ "", "", _L("Name"), _L("Description")}) {
            auto text = new wxStaticText(m_parent, wxID_ANY, l);
            text->SetFont(wxGetApp().bold_font());
            add(text);
        }

        // data

        for (const auto& entry : m_online_entries)
        {
            auto chb = CheckBox::GetNewWin(m_parent, "");
            CheckBox::SetValue(chb, entry.use);
            chb->Bind(wxEVT_CHECKBOX, [this, chb, &entry](wxCommandEvent e) {
                if (CheckBox::GetValue(chb))
                    m_selected_uuids.emplace(entry.id);
                else
                    m_selected_uuids.erase(entry.id);
                check_selection();
                });
            add(chb);

            if (entry.not_in_manifest) {
                wxStaticBitmap* bmp = new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("notification_warning"));
                //wxGetApp().plater()->get_user_account()
                if (wxGetApp().is_account_logged_in()) {
                    // TRN tooltip in Configuration Wizard - Configuration Sources
                    bmp->SetToolTip(_L("Some vendors were installed from this source, but you do not have the rights to receive updates from it.\n"
                        "This source may no longer be active, or your account may no longer be subscribed.\n"
                        "Please consider unsubscribing from this source."));
                } else {
                    // TRN tooltip in Configuration Wizard - Configuration Sources
                    bmp->SetToolTip(_L("Some vendors were installed from this source, but you do not have rights to receive updates from it.\n"
                        "Please log in to restore access to all your subscribed sources or consider unsubscribing from this source."));
                }
                add(bmp);
            } else if (!entry.visibility.empty()) {
                wxStaticBitmap* bmp = new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle("info"));
                bmp->SetToolTip(from_u8(entry.visibility));
                add(bmp);
            } else {
                add(new wxStaticText(m_parent, wxID_ANY, ""));
            }

            add(new wxStaticText(m_parent, wxID_ANY, from_u8(entry.name) + " "));

            add(new wxStaticText(m_parent, wxID_ANY, from_u8(entry.description) + " "));
        }
    }

    if (!m_offline_entries.empty()) {

        auto add = [this](wxWindow* win) { m_offline_sizer->Add(win, 0, wxALIGN_CENTER_VERTICAL); };

        // header

        for (const wxString& l : std::initializer_list<wxString>{ "", _L("Name"), _L("Description"), "", _L("Source file"), "", ""}) {
            auto text = new wxStaticText(m_parent, wxID_ANY, l);
            text->SetFont(wxGetApp().bold_font());
            add(text);
        }

        // data1

        for (const auto& entry : m_offline_entries)
        {
            auto chb = CheckBox::GetNewWin(m_parent, "");
            CheckBox::SetValue(chb, entry.use);
            chb->Bind(wxEVT_CHECKBOX, [this, chb, &entry](wxCommandEvent e) {
                if (CheckBox::GetValue(chb))
                    m_selected_uuids.emplace(entry.id);
                else
                    m_selected_uuids.erase(entry.id);
                check_selection();
                });
            add(chb);

            add(new wxStaticText(m_parent, wxID_ANY, from_u8(entry.name)));

            add(new wxStaticText(m_parent, wxID_ANY, from_u8(entry.description)));

            {
                wxStaticBitmap* bmp = new wxStaticBitmap(m_parent, wxID_ANY, *get_bmp_bundle(entry.is_ok ? "tick_mark" : "exclamation"));
                bmp->SetToolTip(entry.is_ok ? _L("File exists") : _L("File does NOT exist"));
                add(bmp);
            }

            {
                auto path_str = new wxStaticText(m_parent, wxID_ANY, from_u8(entry.source));
                path_str->SetToolTip(from_u8(entry.source_path.string()));
                add(path_str);
            }

            {
                ScalableButton* btn = new ScalableButton(m_parent, wxID_ANY, "open");
                btn->SetToolTip(_L("Open folder"));
                btn->Bind(wxEVT_BUTTON, [&entry](wxCommandEvent& event) {
                    GUI::desktop_open_folder(entry.source_path.parent_path().make_preferred());
                });
                add(btn);
            }

            {
                wxButton* btn = new wxButton(m_parent, wxID_ANY, "  " + _L("Remove") + "  ");
                wxGetApp().UpdateDarkUI(btn, true);
                btn->Bind(wxEVT_BUTTON, [this, &entry](wxCommandEvent& event) { remove_offline_repos(entry.id); });
                add(btn);
            }
        }
    }
}

void RepositoryUpdateUIManager::update()
{
    fill_entries();

    wxWindowUpdateLocker freeze_guard(m_parent);

    fill_grids();

    m_parent->GetSizer()->Layout();

    if (wxDialog* dlg = dynamic_cast<wxDialog*>(m_parent)) {
        m_parent->Layout();
        m_parent->Refresh();
        dlg->Fit();
    }
    else if (wxWindow* top_parent = m_parent->GetParent()) {
        top_parent->Layout();
        top_parent->Refresh();
    }
}

void RepositoryUpdateUIManager::remove_offline_repos(const std::string& id)
{
    m_puw->remove_local_archive(id);
    m_selected_uuids.erase(id);
    check_selection();

    if (wxDialog* dlg = dynamic_cast<wxDialog*>(m_parent)) {
        // Invalidate min_size for correct next Layout()
        dlg->SetMinSize(wxDefaultSize);
    }

    update();
}

void RepositoryUpdateUIManager::load_offline_repos()
{
    wxArrayString input_files;
    wxFileDialog dialog(m_parent, _L("Choose one or more ZIP files") + ":",
        from_u8(wxGetApp().app_config->get_last_dir()), "",
        file_wildcards(FT_ZIP), wxFD_OPEN | /*wxFD_MULTIPLE | */wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);

    if (input_files.IsEmpty())
        return;

    // Iterate through the input files
    for (size_t i = 0; i < input_files.size(); ++i) {
        std::string input_file = into_u8(input_files.Item(i));
        try {
            fs::path input_path = fs::path(input_file);
            std::string msg;
            std::string uuid = m_puw->add_local_archive(input_path, msg);
            if (uuid.empty()) {
                ErrorDialog(m_parent, from_u8(msg), false).ShowModal();
            }
            else {
                m_selected_uuids.emplace(uuid);
                check_selection();
                update();
            }
        }
        catch (fs::filesystem_error const& e) {
            std::cerr << e.what() << '\n';
        }
    }
}

bool RepositoryUpdateUIManager::set_selected_repositories()
{
    std::vector<std::string> used_ids;
    std::copy(m_selected_uuids.begin(), m_selected_uuids.end(), std::back_inserter(used_ids));

    std::string msg;

    if (m_puw->set_selected_repositories(used_ids, msg)) {
        check_selection();
        return true;
    }

    ErrorDialog(m_parent, from_u8(msg), false).ShowModal();
    // update selection on UI
    update();
    check_selection();
    return false;
}

void RepositoryUpdateUIManager::check_selection()
{
    for (const auto& [uuid, is_selected] : m_puw->get_selected_repositories_uuid() )
        if ((is_selected && m_selected_uuids.find(uuid) == m_selected_uuids.end() )||
            (!is_selected && m_selected_uuids.find(uuid) != m_selected_uuids.end())) {
            m_is_selection_changed = true;
            return;
        }

    m_is_selection_changed = false;
}

ManagePresetRepositoriesDialog::ManagePresetRepositoriesDialog(Slic3r::PresetUpdaterWrapper* puw)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY,
        format_wxstr("%1% - %2%", SLIC3R_APP_NAME, _L("Manage Updates")),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    m_manager = std::make_unique<RepositoryUpdateUIManager>(this, puw, em);

    auto sizer = m_manager->get_sizer();

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK | wxCLOSE);
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    wxGetApp().UpdateDlgDarkUI(this, true);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &ManagePresetRepositoriesDialog::onCloseDialog, this, wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &ManagePresetRepositoriesDialog::onOkDialog, this, wxID_OK);
    sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, em);


    buttons->GetAffirmativeButton()->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Enable(m_manager->has_selections());
    });

    SetSizer(sizer);
    sizer->SetSizeHints(this);
}

void ManagePresetRepositoriesDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    SetMinSize(GetBestSize());
    Fit();
    Refresh();
}

void ManagePresetRepositoriesDialog::onCloseDialog(wxEvent &)
{
     this->EndModal(wxID_CLOSE);
}

void ManagePresetRepositoriesDialog::onOkDialog(wxEvent&)
{
    if (m_manager->set_selected_repositories())
        this->EndModal(wxID_OK);
}

} // namespace GUI
} // namespace Slic3r
