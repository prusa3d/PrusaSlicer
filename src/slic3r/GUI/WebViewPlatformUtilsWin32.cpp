#include "WebViewPlatformUtils.hpp"

#ifdef __WIN32__
#include "WebView2.h"
#include <wrl.h>
#include <atlbase.h>
#include <unordered_map>

#include "wx/msw/private/comptr.h"

#include "GUI_App.hpp"
#include "format.hpp"
#include "Mainframe.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/log/trivial.hpp>

namespace pt = boost::property_tree;

namespace Slic3r::GUI {

std::unordered_map<ICoreWebView2*,EventRegistrationToken> g_basic_auth_handler_tokens;

void setup_webview_with_credentials(wxWebView* webview, const std::string& username, const std::string& password)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        return;
    }
    wxCOMPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = webView2->QueryInterface(IID_PPV_ARGS(&wv2_10));
    if (FAILED(hr)) {
        return;        
    }

    remove_webview_credentials(webview);

    // should it be stored?
    EventRegistrationToken basicAuthenticationRequestedToken = {};
    if (FAILED(wv2_10->add_BasicAuthenticationRequested(
            Microsoft::WRL::Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
                [username, password](ICoreWebView2 *sender, ICoreWebView2BasicAuthenticationRequestedEventArgs *args) {
                    wxCOMPtr<ICoreWebView2BasicAuthenticationResponse> basicAuthenticationResponse;
                    if (FAILED(args->get_Response(&basicAuthenticationResponse))) {
                        return -1;
                    }
                    if (FAILED(basicAuthenticationResponse->put_UserName(GUI::from_u8(username).c_str()))) {
                        return -1;
                    }
                    if (FAILED(basicAuthenticationResponse->put_Password(GUI::from_u8(password).c_str()))) {
                        return -1;
                    }
                    return 0;
                }
            ).Get(),
            &basicAuthenticationRequestedToken
        ))) {

        BOOST_LOG_TRIVIAL(error) << "WebView: Cannot register authentication request handler";
    } else {
        g_basic_auth_handler_tokens[webView2] = basicAuthenticationRequestedToken;
    }
       
}

void remove_webview_credentials(wxWebView* webview)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        return;
    }
    wxCOMPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = webView2->QueryInterface(IID_PPV_ARGS(&wv2_10));
    if (FAILED(hr)) {
        return;
    }

    if (auto it = g_basic_auth_handler_tokens.find(webView2);
        it != g_basic_auth_handler_tokens.end()) {

        if (FAILED(wv2_10->remove_BasicAuthenticationRequested(it->second))) {
            BOOST_LOG_TRIVIAL(error) << "WebView: Unregistering authentication request handler failed";
        } else {
            g_basic_auth_handler_tokens.erase(it);
        }
    } else {
        BOOST_LOG_TRIVIAL(error) << "WebView: Cannot unregister authentication request handler";
    }

}
void delete_cookies(wxWebView* webview, const std::string& url)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        return;
    }

    /*
    "cookies": [{
		"domain": ".google.com",
		"expires": 1756464458.304917,
		"httpOnly": true,
		"name": "__Secure-1PSIDCC",
		"path": "/",
		"priority": "High",
		"sameParty": false,
		"secure": true,
		"session": false,
		"size": 90,
		"sourcePort": 443,
		"sourceScheme": "Secure",
		"value": "AKEyXzUvV_KBqM4aOlsudROI_VZ-ToIH41LRbYJFtFjmKq_rOmx1owoyUGvQHbwr5be380fKuQ"
    },...]}
    */
    wxString parameters = GUI::format_wxstr(L"{\"urls\": [\"%1%\"]}", url);
    webView2->CallDevToolsProtocolMethod(L"Network.getCookies", parameters.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [webView2, url](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT {
                if (FAILED(errorCode)) {
                    return S_OK;
                }
                // Handle successful call (resultJson contains the list of cookies)
                pt::ptree ptree;
                try {
                    std::stringstream ss(GUI::into_u8(resultJson));
                    pt::read_json(ss, ptree);
                }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to parse cookies json: " << e.what();
                    return S_OK;
                }
                for (const auto& cookie : ptree.get_child("cookies")) {
                    std::string name = cookie.second.get<std::string>("name");
                    std::string domain = cookie.second.get<std::string>("domain");
                    // Delete cookie by name and domain
                    wxString name_and_domain = GUI::format_wxstr(L"{\"name\": \"%1%\", \"domain\": \"%2%\"}", name, domain);
                    webView2->CallDevToolsProtocolMethod(L"Network.deleteCookies", name_and_domain.c_str(),
                        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                            [](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT { return S_OK; }).Get());
                }       
                return S_OK;
            }
        ).Get());
    
}


} // namespace Slic3r::GUI
#endif // WIN32
