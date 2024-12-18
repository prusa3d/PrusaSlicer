#ifndef slic3r_PrusaConnect_hpp_
#define slic3r_PrusaConnect_hpp_

#include "PrintHost.hpp"
#include "ServiceConfig.hpp"
#include "libslic3r/PrintConfig.hpp"
/*
#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>


*/

namespace Slic3r {

class DynamicPrintConfig;
class Http;

class PrusaConnectNew : public PrintHost
{
public:
    PrusaConnectNew(DynamicPrintConfig *config);
    ~PrusaConnectNew() override = default;

    const char* get_name() const override;

    virtual bool test(wxString &curl_msg) const override;
    wxString get_test_ok_msg () const override;
    wxString get_test_failed_msg (wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool has_auto_discovery() const override { return true; }
    bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint | PrintHostPostUploadAction::QueuePrint; }
    std::string get_host() const override { return Utils::ServiceConfig::instance().connect_url(); }
    std::string get_notification_host() const override;
    bool get_storage(wxArrayString& storage_path, wxArrayString& storage_name) const override;
    //const std::string& get_apikey() const { return m_apikey; }
    //const std::string& get_cafile() const { return m_cafile; }

private:
    std::string m_uuid;
    std::string m_team_id;

    bool init_upload(PrintHostUpload upload_data, std::string& out) const;
    std::string get_team_id(const std::string& data) const;
};

}
#endif
