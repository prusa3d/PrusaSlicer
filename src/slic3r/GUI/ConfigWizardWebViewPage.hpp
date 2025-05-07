#ifndef slic3r_ConfigWizardWebViewPage_hpp_
#define slic3r_ConfigWizardWebViewPage_hpp_

#include "ConfigWizard_private.hpp"
#include <wx/event.h>

class wxWebView;
class wxWebViewEvent;

wxDECLARE_EVENT(EVT_OPEN_EXTERNAL_LOGIN_WIZARD, wxCommandEvent);

namespace Slic3r { 
namespace GUI {

wxDECLARE_EVENT(EVT_LOGIN_VIA_WIZARD, Event<std::string>);

/*
struct ConfigWizardPage: wxPanel
{
    ConfigWizard *parent;
    const wxString shortname;
    wxBoxSizer *content;
    const unsigned indent;

    ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname, unsigned indent = 0);
    virtual ~ConfigWizardPage();

    template<class T>
    T* append(T *thing, int proportion = 0, int flag = wxEXPAND|wxTOP|wxBOTTOM, int border = 10)
    {
        content->Add(thing, proportion, flag, border);
        return thing;
    }

    wxStaticText* append_text(wxString text);
    void append_spacer(int space);

    ConfigWizard::priv *wizard_p() const { return parent->p.get(); }

    virtual void apply_custom_config(DynamicPrintConfig &config) {}
    virtual void set_run_reason(ConfigWizard::RunReason run_reason) {}
    virtual void on_activate() {}
};
*/

class UserAccount;
class ConfigWizardWebViewPage : public ConfigWizardPage
{
public:
    ConfigWizardWebViewPage( ConfigWizard *parent);
    virtual ~ConfigWizardWebViewPage() {}

    void on_error(wxWebViewEvent &evt);
    void on_navigation_request(wxWebViewEvent &evt);
    void on_idle(wxIdleEvent &evt);
    void on_script_message(wxWebViewEvent& evt);
    void load_error_page();
    // returns true if logged in - wizard needs to update repos
    bool login_changed();

private:
    wxWebView *m_browser{nullptr};
    UserAccount *p_user_account{nullptr};
    wxBoxSizer *m_browser_sizer{nullptr};
    wxStaticText *m_text{nullptr};
    bool m_load_error_page{false};
    bool m_vetoed{false};
    bool m_evt_sent{false};
};

}} // namespace Slic3r::GUI
#endif