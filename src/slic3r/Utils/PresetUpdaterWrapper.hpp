#ifndef slic3r_PresetUpdateWrapper_hpp_
#define slic3r_PresetUpdateWrapper_hpp_

#include "slic3r/GUI/PresetArchiveDatabase.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"
#include "slic3r/Utils/Http.hpp"

#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/progdlg.h>

#include <memory>
#include <functional>
#include <thread>

namespace Slic3r {

using PresetUpdaterStatusSimpleEvent = GUI::SimpleEvent;
using PresetUpdaterStatusMessageEvent = GUI::Event<wxString>;
wxDECLARE_EVENT(EVT_PRESET_UPDATER_STATUS_END, PresetUpdaterStatusSimpleEvent);
wxDECLARE_EVENT(EVT_PRESET_UPDATER_STATUS_PRINT, PresetUpdaterStatusMessageEvent);
wxDECLARE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);
class PresetBundle; 
class Semver;

// class that is passed to inner thread function. 
// Each call from inner thread triggers event, that is handled by wx object on UI thread (m_evt_handler).
// Communication in opposite direction is done via pointer that should use only set_canceled. (PresetUpdaterUIStatusCancel)
class PresetUpdaterUIStatus
{
public:
    enum class PresetUpdaterRetryPolicy
    {
        PURP_5_TRIES,
        PURP_NO_RETRY,
    };
    // called from PresetUpdaterWrapper
    PresetUpdaterUIStatus(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy policy);
    ~PresetUpdaterUIStatus(){}
    void set_handler(wxEvtHandler* evt_handler) {m_evt_handler = evt_handler;}

    // called from worker thread
    bool on_attempt(int attempt, unsigned delay);
    void set_target(const std::string& target);
    void set_status(const wxString& status);
    void end();
    bool get_canceled() const {return m_canceled.load(); }
    HttpRetryOpt get_retry_policy() const { return m_retry_policy; }
    std::string get_error() const { return m_error_msg; }
    std::string get_target() const { return m_target; }

    // called from PresetUpdaterUIStatusCancel (ui thread)
    void set_canceled(bool val) { m_canceled.store(val); }
    void set_error(const std::string& msg) { m_error_msg = msg; }
private:
    wxEvtHandler* m_evt_handler {nullptr};
    std::atomic<bool> m_canceled {false};
    std::string m_error_msg;

    std::string m_target;

    HttpRetryOpt m_retry_policy;
    static const std::map<PresetUpdaterUIStatus::PresetUpdaterRetryPolicy, HttpRetryOpt> policy_map;
};

// Purpose of this class:
// Serves as a hub a entering 2 classes: PresetArchiveDatabase and PresetUpdater
// PresetUpdater:
//      - Does not contain outside getters / setters
//      - Sync function can be run in worker thread
//      ! other functions like config_update does show modal dialogs or notification and must be run in UI tread
// PresetArchiveDatabase:
//      - Many functions gets or sets its inner data
//      ! Including sync function
//      - Does not use direct UI components f.e. dialog
// This class is accessible via wxGetApp().get_preset_updater_wrapper() 
// but it should be done only in certain cases:
// 1) Sync of PresetUpdater
//      - Runs on worker thread
//      - Is called only during startup
//      - Needs to be canceled before other operations
//      - Ends by queueing EVT_CONFIG_UPDATER_SYNC_DONE
// 2) Callbacks of EVT_CONFIG_UPDATER_SYNC_DONE
//      - Needs to be run in UI thread
//      - Might chain (evt -> PresetUpdater function -> notification -> Plater -> PresetUpdater function -> dialog)
// 3) Check of update triggered by user
//      - Runs most of the operations in own thread while having Modal Dialog for user
//      - Used before and inside Config Wizard or when Check for Config Updates
//      - Might use UI thread PresetUpdater functions after its thread is done 
//      - The inner thread is stored as m_modal_thread due to 
// 4) Config Wizard run
//     - Config Wizard often needs to get or set data of PresetArchiveDatabase
//     - Config Wizard runs in UI thread as Modal Dialog
// Possibility of conflicts:
// 1 x 2 - No conflict due 2 being triggered only by end of 1
// 1 x 3 - No conflict due 3 calling Cancel on 1 before runing
// 1 x 4 - No conflict due 4 run after 3
// 2 x 2 - All 2 functions does create modal window and are triggered by ui buttons - ui thread might work on other events but ui should be inaccessible
// 2 x 3 - If 1 finnished (2 starts) and 3 is triggered by user -  both are triggered via events
//       - If order of events is event triggering 3 first and then event queued by 1 is second
//       - 2 would run when inner thread of 3 changes data - Therefor functions of 2 must check if inner thread of 3 (m_modal_thread) is joinable
// 2 x 4 - No conflict due 2 and 4 run on both UI thread
// 3 x 4 - No conflict due either 3 blocking ui or even calling 4 only after it finnishes

class PresetUpdaterWrapper
{
public:
    PresetUpdaterWrapper();
    ~PresetUpdaterWrapper();

    // 1) Sync of PresetUpdater functions
     // runs worker thread and leaves
    void sync_preset_updater(wxEvtHandler* end_evt_handler, const PresetBundle* preset_bundle);

    // 2) Callbacks of EVT_CONFIG_UPDATER_SYNC_DONE
    // Runs on UI thread
    PresetUpdater::UpdateResult check_updates_on_startup(const Semver& old_slic3r_version);
    void on_update_notification_confirm();

    // 3) Check of update triggered by user
    // runs own thread but blocks until its done
    bool wizard_sync(const PresetBundle* preset_bundle, const Semver& old_slic3r_version, wxWindow* parent, bool full_sync, const wxString& headline);
    PresetUpdater::UpdateResult check_updates_on_user_request(const PresetBundle* preset_bundle, const Semver& old_slic3r_version, wxWindow* parent);

    // 4) Config Wizard run
    // These function are either const reading from m_preset_archive_database,
    // Or sets inner data of m_preset_archive_database
    // problem would be if at same time worker thread runs m_preset_archive_database->sync_blocking
    bool is_selected_repository_by_id(const std::string& repo_id) const { return m_preset_archive_database->is_selected_repository_by_id(repo_id); }
    bool is_selected_repository_by_uuid(const std::string& uuid) const { return m_preset_archive_database->is_selected_repository_by_uuid(uuid); }
    SharedArchiveRepositoryVector get_all_archive_repositories() const { return m_preset_archive_database->get_all_archive_repositories(); }
    SharedArchiveRepositoryVector get_selected_archive_repositories() const { return m_preset_archive_database->get_selected_archive_repositories();}
    const std::map<std::string, bool>& get_selected_repositories_uuid() const { return m_preset_archive_database->get_selected_repositories_uuid(); }
    bool set_selected_repositories(const std::vector<std::string>& used_uuids, std::string& msg) { return m_preset_archive_database->set_selected_repositories(used_uuids, msg); }
    void set_installed_printer_repositories(const std::vector<std::string> &used_ids) { m_preset_archive_database->set_installed_printer_repositories(used_ids); }
    void remove_local_archive(const std::string& uuid) { m_preset_archive_database->remove_local_archive(uuid); }
    std::string add_local_archive(const boost::filesystem::path path, std::string& msg) { return m_preset_archive_database->add_local_archive(path, msg); }
    
    bool install_bundles_rsrc_or_cache_vendor(std::vector<std::string> bundles, bool snapshot = true) const ;
    
private:
    void cancel_worker_thread();

    // Do not share these 2 out of PresetUpdaterWrapper
    std::unique_ptr<PresetArchiveDatabase>     m_preset_archive_database;
    std::unique_ptr<PresetUpdater>             m_preset_updater;

    // m_worker_thread runs on background while m_modal_thread runs only when modal window exists.
    std::thread m_worker_thread;
    PresetUpdaterUIStatus* m_ui_status {nullptr};
    std::thread m_modal_thread;
};

namespace GUI {

class PresetUpdaterUIStatusCancel
{
public:
    PresetUpdaterUIStatusCancel(PresetUpdaterUIStatus* ui_status) : p_ui_status(ui_status) {}
    ~PresetUpdaterUIStatusCancel() {}
    void set_cancel(bool c) {p_ui_status->set_canceled(c);}
private:
    PresetUpdaterUIStatus* p_ui_status;
};

class ProgressUpdaterDialog : public wxGenericProgressDialog, public PresetUpdaterUIStatusCancel
{
public:
    ProgressUpdaterDialog(PresetUpdaterUIStatus* ui_status, wxWindow* parent, const wxString first_line);
    ~ProgressUpdaterDialog();
    void on_set_status(const PresetUpdaterStatusMessageEvent& evt);
    void on_end(const PresetUpdaterStatusSimpleEvent& evt);
private: 
};

#if 0 
// basic dialog
class CommonUpdaterDialog : public wxDialog, public PresetUpdaterUIStatusCancel
{
public:
    CommonUpdaterDialog(PresetUpdaterUIStatus* ui_status, wxWindow* parent, const wxString first_line, int milisecond_until_cancel_shown);
    ~CommonUpdaterDialog();
    void on_set_status(const PresetUpdaterStatusMessageEvent& evt);
    void on_end(const PresetUpdaterStatusSimpleEvent& evt);
private: 
    wxStaticText* m_status_text;
    wxButton* m_cancel_button;
    wxTimer* m_show_cancel_timer {nullptr};
};

// testing purpose dummy class
class DummyPresetUpdaterUIStatusHandler : public wxEvtHandler
{
public:
    DummyPresetUpdaterUIStatusHandler();
    ~DummyPresetUpdaterUIStatusHandler() {}
    void on_set_status(const PresetUpdaterStatusMessageEvent& evt) {}
    void on_end(const PresetUpdaterStatusSimpleEvent& evt) 
    {
        if(m_end_callback)
            m_end_callback();
    }
    void set_end_callback(std::function<void(void)> callback) {m_end_callback = callback; }
private:
    std::function<void(void)> m_end_callback;
};
#endif
} // namespace GUI 
} // namespace Slic3r 
#endif //slic3r_PresetUpdateWrapper_hpp_