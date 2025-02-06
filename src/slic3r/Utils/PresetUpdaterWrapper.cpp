#include "PresetUpdaterWrapper.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"

using namespace std::chrono;

namespace Slic3r {

wxDEFINE_EVENT(EVT_PRESET_UPDATER_STATUS_END, PresetUpdaterStatusSimpleEvent);
wxDEFINE_EVENT(EVT_PRESET_UPDATER_STATUS_PRINT, PresetUpdaterStatusMessageEvent);
wxDEFINE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);

PresetUpdaterWrapper::PresetUpdaterWrapper()
    : m_preset_updater(std::make_unique<PresetUpdater>())
    , m_preset_archive_database(std::make_unique<PresetArchiveDatabase>())
{
}
PresetUpdaterWrapper::~PresetUpdaterWrapper()
{
    if (m_worker_thread.joinable()) {
        if (m_ui_status) 
            m_ui_status->set_canceled(true);
		m_worker_thread.join();
	}
}

bool PresetUpdaterWrapper::wizard_sync(const PresetBundle* preset_bundle, const Semver& old_slic3r_version, wxWindow* parent, bool full_sync, const wxString& headline)
{
    assert(!m_modal_thread.joinable());
    // Cancel sync before starting wizard to prevent two downloads at same time.
    cancel_worker_thread();
    PresetUpdaterUIStatus* ui_status = new PresetUpdaterUIStatus(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES);
    Slic3r::ScopeGuard sg([ui_status]() { delete ui_status; });
    GUI::ProgressUpdaterDialog* dialog = new GUI::ProgressUpdaterDialog(ui_status, parent, headline);
    ui_status->set_handler(dialog);
    VendorMap vendors_copy = preset_bundle->vendors;
    auto worker_body = [ui_status, this, vendors_copy, full_sync]()
    {
        if (!m_preset_archive_database->sync_blocking(ui_status)) {
            ui_status->end(); 
            return;
        }        
        if (ui_status->get_canceled()) { ui_status->end(); return; }

        if (full_sync) {
            // Since there might be new repos, we need to sync preset updater
            const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
            m_preset_updater->sync_blocking(vendors_copy, repos, ui_status);
            if (ui_status->get_canceled()) { ui_status->end(); return; }
            m_preset_updater->update_index_db();            
        }
        ui_status->end();
    };
    m_modal_thread = std::thread(worker_body);
    // We need to call ShowModal here instead of prompting it from event callback.
    // Otherwise UI thread would freez on job_thread.join();
    dialog->CenterOnParent();
    dialog->ShowModal();
    m_modal_thread.join();
    parent->RemoveChild(dialog);
    dialog->Destroy();
    ui_status->set_handler(nullptr);

    // Only now it is possible to work with ui_status, that was previously used in worker thread.

    if (std::string s = ui_status->get_error(); !s.empty()) {
        std::string err_text = GUI::format(_u8L("Failed to download %1%"), ui_status->get_target());
        GUI::ErrorDialog err_msg(nullptr, err_text + "\n\n" + s, false);
        err_msg.ShowModal();
        return false;
    }

    // Should  m_preset_updater->config_update run even if there is cancel?
    if (ui_status->get_canceled() /*&& !full_sync*/) {
        return false;
    }

    // Offer update installation.  
    if (full_sync) {
        const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
        m_preset_updater->config_update(old_slic3r_version, PresetUpdater::UpdateParams::SHOW_TEXT_BOX_YES_NO, repos, ui_status);
    }
    bool res = !ui_status->get_canceled();
    return res;
}

PresetUpdater::UpdateResult PresetUpdaterWrapper::check_updates_on_user_request(const PresetBundle* preset_bundle, const Semver& old_slic3r_version, wxWindow* parent)
{
    assert(!m_modal_thread.joinable());
    cancel_worker_thread();
    PresetUpdaterUIStatus* ui_status = new PresetUpdaterUIStatus(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES);
    Slic3r::ScopeGuard sg([ui_status]() { delete ui_status; });
    // TRN: Headline of Progress dialog
    GUI::ProgressUpdaterDialog* dialog = new GUI::ProgressUpdaterDialog(ui_status, parent, _L("Checking for Configuration Updates"));
    ui_status->set_handler(dialog);
    VendorMap vendors_copy = preset_bundle->vendors;
    std::string failed_paths;
    PresetUpdater::UpdateResult updater_result = PresetUpdater::UpdateResult::R_ALL_CANCELED;
    auto worker_body = [ui_status, this, vendors_copy, &failed_paths]()
    {
        if (!m_preset_archive_database->sync_blocking(ui_status)) {
            ui_status->end(); 
            return;
        }
        if (ui_status->get_canceled()) { ui_status->end(); return; }
        m_preset_archive_database->extract_archives_with_check(failed_paths);
        const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
        m_preset_updater->sync_blocking(vendors_copy, repos, ui_status);
        if (ui_status->get_canceled()) { ui_status->end(); return; }
        
        m_preset_updater->update_index_db();
        ui_status->end();
    };
    
    m_modal_thread = std::thread(worker_body);
    // We need to call ShowModal here instead of prompting it from event callback.
    // Otherwise UI thread would freez on job_thread.join();
        dialog->CenterOnParent();
        dialog->ShowModal();
    m_modal_thread.join();
    parent->RemoveChild(dialog);
    dialog->Destroy();
    ui_status->set_handler(nullptr);

    // Only now it is possible to work with ui_status, that was previously used in worker thread.

    if (std::string s = ui_status->get_error(); !s.empty()) {
        std::string err_text = GUI::format(_u8L("Failed to download %1%"), ui_status->get_target());
        GUI::ErrorDialog err_msg(nullptr, s, false);
        err_msg.ShowModal();
        return PresetUpdater::UpdateResult::R_ALL_CANCELED; 
    }

    if (ui_status->get_canceled()) {
        return PresetUpdater::UpdateResult::R_ALL_CANCELED;
    }

    if (!failed_paths.empty()) {
         int cnt = std::count(failed_paths.begin(), failed_paths.end(), '\n') + 1;
        // TRN: %1% contains paths from which loading failed. They are separated by \n, there is no \n at the end.
        failed_paths = GUI::format(_L_PLURAL("It was not possible to extract data from %1%. The source will not be updated.",
            "It was not possible to extract data for following local sources. They will not be updated.\n\n %1%", cnt), failed_paths);
        GUI::ErrorDialog err_msg(nullptr, failed_paths, false);
        err_msg.ShowModal();
    }
    // preset_updater::config_update does show wxDialog
    updater_result = m_preset_updater->config_update(old_slic3r_version, PresetUpdater::UpdateParams::SHOW_TEXT_BOX, m_preset_archive_database->get_selected_archive_repositories(), ui_status);
    return updater_result;
}

PresetUpdater::UpdateResult PresetUpdaterWrapper::check_updates_on_startup(const Semver& old_slic3r_version)
{
    if (m_modal_thread.joinable()) {
        return PresetUpdater::UpdateResult::R_ALL_CANCELED;
    }
    PresetUpdaterUIStatus ui_status(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY);
    // preset_updater::config_update does show wxDialog
    m_preset_updater->update_index_db();
    return m_preset_updater->config_update(old_slic3r_version, PresetUpdater::UpdateParams::SHOW_NOTIFICATION, m_preset_archive_database->get_selected_archive_repositories(), &ui_status);
}

void PresetUpdaterWrapper::on_update_notification_confirm()
{
    if (m_modal_thread.joinable()) {
        return;
    }
    PresetUpdaterUIStatus ui_status(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY);
    // preset_updater::on_update_notification_confirm does show wxDialog
    const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
    m_preset_updater->on_update_notification_confirm(repos, &ui_status);
}

bool PresetUpdaterWrapper::install_bundles_rsrc_or_cache_vendor(std::vector<std::string> bundles, bool snapshot/* = true*/) const 
{
     PresetUpdaterUIStatus ui_status(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY);
      const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
    return m_preset_updater->install_bundles_rsrc_or_cache_vendor(bundles, repos, &ui_status, snapshot); 
} 

void PresetUpdaterWrapper::sync_preset_updater(wxEvtHandler* end_evt_handler, const PresetBundle* preset_bundle)
{
    cancel_worker_thread();
    m_ui_status = new PresetUpdaterUIStatus(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY);
    VendorMap vendors_copy = preset_bundle->vendors;

    auto worker_body = [ this, vendors_copy, end_evt_handler]()
    {
        const SharedArchiveRepositoryVector &repos = m_preset_archive_database->get_selected_archive_repositories();
        m_preset_updater->sync_blocking(vendors_copy, repos, this->m_ui_status);
        if (this->m_ui_status->get_canceled()) { return; }
        wxCommandEvent* evt = new wxCommandEvent(EVT_CONFIG_UPDATER_SYNC_DONE);
        wxQueueEvent(end_evt_handler, evt);
    };
    
    m_worker_thread = std::thread(worker_body);
}

void PresetUpdaterWrapper::cancel_worker_thread()
{
    if (m_worker_thread.joinable()) {
        if (m_ui_status) {
            m_ui_status->set_canceled(true);
        } else assert(false);

		m_worker_thread.join();

        if (m_ui_status) {
            delete m_ui_status;
            m_ui_status = nullptr;
        }
	} 
}

const std::map<PresetUpdaterUIStatus::PresetUpdaterRetryPolicy, HttpRetryOpt> PresetUpdaterUIStatus::policy_map = {
    {PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES,     {500ms, 5s, 4}},
    {PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY,    {0ms}}
};
PresetUpdaterUIStatus::PresetUpdaterUIStatus(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy policy)
{
    if (auto it = policy_map.find(policy); it != policy_map.end()) {
        m_retry_policy = it->second;
    } else {
        assert(false);
        m_retry_policy = {0ms};
    }
}
bool PresetUpdaterUIStatus::on_attempt(int attempt, unsigned delay)
{
    if (attempt == 1) {
        // TRN: Text of progress dialog. %1% is a name of file.
        set_status(GUI::format_wxstr(_L("Downloading Resources: %1%"), m_target));
    } else { 
        // TRN: Text of progress dialog. %1% is a name of file. %2% is a number of attept.
        set_status(GUI::format_wxstr(_L("Downloading Resources: %1%. Attempt %2%."), m_target, std::to_string(attempt)));
    }
    return get_canceled();
}
void PresetUpdaterUIStatus::set_target(const std::string& target) 
{
    m_target = target; 
}
void PresetUpdaterUIStatus::set_status(const wxString& status)
{
    if (m_evt_handler) 
        wxQueueEvent(m_evt_handler, new PresetUpdaterStatusMessageEvent(EVT_PRESET_UPDATER_STATUS_PRINT, status));
}

void PresetUpdaterUIStatus::end()
{
    if (m_evt_handler)
        wxQueueEvent(m_evt_handler, new PresetUpdaterStatusSimpleEvent(EVT_PRESET_UPDATER_STATUS_END));
}

namespace GUI {
ProgressUpdaterDialog::ProgressUpdaterDialog(PresetUpdaterUIStatus* ui_status, wxWindow* parent, const wxString first_line)
    // TRN: Text of progress dialog.
    :wxGenericProgressDialog(first_line, _L("Initializing"), 100, parent,  wxPD_AUTO_HIDE|wxPD_APP_MODAL|wxPD_CAN_ABORT)	
    , PresetUpdaterUIStatusCancel(ui_status)
{
    SetMinSize(wxSize(32 * wxGetApp().em_unit(),12 * wxGetApp().em_unit()));
    Bind(EVT_PRESET_UPDATER_STATUS_END, &ProgressUpdaterDialog::on_end, this);
    Bind(EVT_PRESET_UPDATER_STATUS_PRINT, &ProgressUpdaterDialog::on_set_status, this);
}
ProgressUpdaterDialog::~ProgressUpdaterDialog()
{
}
void ProgressUpdaterDialog::on_set_status(const PresetUpdaterStatusMessageEvent& evt)
{
    if (!Pulse(evt.data)) {
        set_cancel(true);
    } 
}
void ProgressUpdaterDialog::on_end(const PresetUpdaterStatusSimpleEvent& evt)
{
    EndModal(0);
}
#if 0  
CommonUpdaterDialog::CommonUpdaterDialog(PresetUpdaterUIStatus* ui_status, wxWindow* parent, const wxString first_line, int milisecond_until_cancel_shown)
    : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxFRAME_FLOAT_ON_PARENT)
    , PresetUpdaterUIStatusCancel(ui_status)
{
    auto* headline = new wxStaticText(this, wxID_ANY, first_line, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_status_text = new wxStaticText(this, wxID_ANY, _L("Initializing") + dots, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_cancel_button = new wxButton(this, wxID_CANCEL, "Cancel");
    // Layout using sizer
    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(m_status_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, 10);
    hsizer->Add(m_cancel_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    vsizer->Add(headline, 0, wxALIGN_CENTER | wxALL, 10);
    vsizer->Add(hsizer, 0, wxEXPAND | wxALL, 5);
    this->SetSizer(vsizer);
    SetMinSize(wxSize(wxGetApp().em_unit() * 40, wxGetApp().em_unit() * 5));
    m_cancel_button->Bind(wxEVT_BUTTON, [this](const wxCommandEvent& event) { 
        set_cancel(true);
        m_status_text->SetLabel("Canceling...");
        Update();
        Fit();
    });

    if (milisecond_until_cancel_shown > 0) {
        m_cancel_button->Show(false);
        m_show_cancel_timer = new wxTimer(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&)
        { 
            m_cancel_button->Show();
            Layout();
            Fit();
        });
        m_show_cancel_timer->StartOnce(milisecond_until_cancel_shown);
    }

    Bind(EVT_PRESET_UPDATER_STATUS_END, &CommonUpdaterDialog::on_end, this);
    Bind(EVT_PRESET_UPDATER_STATUS_PRINT, &CommonUpdaterDialog::on_set_status, this);

    #ifdef _WIN32
        wxGetApp().UpdateDlgDarkUI(this);
    #endif
    Fit();
}
CommonUpdaterDialog::~CommonUpdaterDialog()
{
    if (m_show_cancel_timer) {
        m_show_cancel_timer->Stop();
        delete m_show_cancel_timer;
    }
}
void CommonUpdaterDialog::on_set_status(const PresetUpdaterStatusMessageEvent& evt)
{
    m_status_text->SetLabel(evt.data);
    Update();
    Fit();
}
void CommonUpdaterDialog::on_end(const PresetUpdaterStatusSimpleEvent& evt)
{
    EndModal(0);
}

DummyPresetUpdaterUIStatusHandler::DummyPresetUpdaterUIStatusHandler()
    :wxEvtHandler()
{   
    Bind(EVT_PRESET_UPDATER_STATUS_END, &DummyPresetUpdaterUIStatusHandler::on_end, this);
    Bind(EVT_PRESET_UPDATER_STATUS_PRINT, &DummyPresetUpdaterUIStatusHandler::on_set_status, this);
}
#endif
}
}

