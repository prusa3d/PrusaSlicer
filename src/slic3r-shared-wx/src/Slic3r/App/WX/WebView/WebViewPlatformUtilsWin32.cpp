#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/Format.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <WebView2.h>
#include <wrl.h>
#include <atlbase.h>
#include <unordered_map>

#include <wx/msw/registry.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/webview.h>
#include <wrl/client.h>
#include <string>

namespace pt = boost::property_tree;
using Microsoft::WRL::ComPtr;

namespace Slic3r::App::WX::WebView {

std::unordered_map<ICoreWebView2*,EventRegistrationToken> g_basic_auth_handler_tokens;

void setup_webview_with_credentials(wxWebView* webview, const std::string& username, const std::string& password)
{
    SPDLOG_DEBUG("{}", __FUNCTION__);

    ASSERT(webview);
    ICoreWebView2* webView2_raw = static_cast<ICoreWebView2*>(webview->GetNativeBackend());
    if (!webView2_raw) {
        SPDLOG_ERROR("{} Failed: Native WebView2 is null.", __FUNCTION__);
        return;
    }

    ComPtr<ICoreWebView2> wv2(webView2_raw);
    ComPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = wv2.As(&wv2_10);
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: ICoreWebView2_10 interface not supported by runtime.", __FUNCTION__);
        return;
    }

    remove_webview_credentials(webview);

    EventRegistrationToken token = {};

    hr = wv2_10->add_BasicAuthenticationRequested(
        Microsoft::WRL::Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
            [username, password](ICoreWebView2* sender, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) -> HRESULT {
                
                ComPtr<ICoreWebView2BasicAuthenticationResponse> response;
                HRESULT hr = args->get_Response(&response);
                if (FAILED(hr)) return hr;

                args->put_Cancel(FALSE);

                std::wstring w_username = boost::nowide::widen(username);
                std::wstring w_password = boost::nowide::widen(password);

                response->put_UserName(w_username.c_str());
                response->put_Password(w_password.c_str());
                
                return S_OK;
            }).Get(),
        &token
    );

    if (FAILED(hr)) {
        SPDLOG_ERROR("WebView: Cannot register authentication request handler. HRESULT: {0:x}", (unsigned int)hr);
    } else {
        g_basic_auth_handler_tokens[webView2_raw] = token;
    }
}

void remove_webview_credentials(wxWebView* webview)
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    ASSERT(webview);
    ICoreWebView2* webView2_raw = static_cast<ICoreWebView2*>(webview->GetNativeBackend());
    if (!webView2_raw) {
        SPDLOG_ERROR("{} Failed: Native WebView2 is null.", __FUNCTION__);
        return;
    }

    ComPtr<ICoreWebView2> wv2(webView2_raw);
    ComPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = wv2.As(&wv2_10);
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: ICoreWebView2_10 interface not supported by runtime.", __FUNCTION__);
        return;
    }

    if (auto it = g_basic_auth_handler_tokens.find(webView2_raw);
        it != g_basic_auth_handler_tokens.end()) {

        if (FAILED(wv2_10->remove_BasicAuthenticationRequested(it->second))) {
            SPDLOG_ERROR("WebView: Unregistering authentication request handler failed");
        } else {
            g_basic_auth_handler_tokens.erase(it);
        }
    } else {
        SPDLOG_WARN("{}: Cannot unregister authentication request handler", __FUNCTION__);
    }

}
void delete_cookies(wxWebView* webview, const std::string& url)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        SPDLOG_ERROR("{} Failed: webView2 is null.", __FUNCTION__);
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
    wxString parameters = format_wxstr(L"{\"urls\": [\"%1%\"]}", url);
    webView2->CallDevToolsProtocolMethod(L"Network.getCookies", parameters.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [webView2, url](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT {
                if (FAILED(errorCode)) {
                    return S_OK;
                }
                // Handle successful call (resultJson contains the list of cookies)
                pt::ptree ptree;
                try {
                    std::stringstream ss(into_u8(resultJson));
                    pt::read_json(ss, ptree);
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("{}: Failed to parse cookies json: {}", __FUNCTION__, e.what());
                    return S_OK;
                }
                for (const auto& cookie : ptree.get_child("cookies")) {
                    std::string name = cookie.second.get<std::string>("name");
                    std::string domain = cookie.second.get<std::string>("domain");
                    // Delete cookie by name and domain
                    wxString name_and_domain = format_wxstr(L"{\"name\": \"%1%\", \"domain\": \"%2%\"}", name, domain);
                    //SPDLOG_DEBUG("Deleting cookie: {}", into_u8(name_and_domain));
                    webView2->CallDevToolsProtocolMethod(L"Network.deleteCookies", name_and_domain.c_str(),
                        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                            [](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT { return S_OK; }).Get());
                }       
                return S_OK;
            }
        ).Get());
    
}
void delete_cookies_with_counter(wxWebView* webview, const std::string& url, std::atomic<size_t>& counter)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        SPDLOG_ERROR("{} Failed: webView2 is null.", __FUNCTION__);
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
    wxString parameters = format_wxstr(L"{\"urls\": [\"%1%\"]}", url);
    webView2->CallDevToolsProtocolMethod(L"Network.getCookies", parameters.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [webView2, url, &counter](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT {
                if (FAILED(errorCode)) {
                    return S_OK;
                }
                // Handle successful call (resultJson contains the list of cookies)
                pt::ptree ptree;
                try {
                    std::stringstream ss(into_u8(resultJson));
                    pt::read_json(ss, ptree);
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Failed to parse cookies json: {}", e.what());
                    return S_OK;
                }
                auto& cookies = ptree.get_child("cookies");
                if (cookies.size() == 0) {
                    counter++;
                    return S_OK;
                }
                for (auto it = cookies.begin(); it != cookies.end(); ++it) {
                    const auto& cookie = *it;
                    std::string name = cookie.second.get<std::string>("name");
                    std::string domain = cookie.second.get<std::string>("domain");
                    // Delete cookie by name and domain
                    wxString name_and_domain = format_wxstr(L"{\"name\": \"%1%\", \"domain\": \"%2%\"}", name, domain);
                    //SPDLOG_DEBUG("Deleting cookie: {}", into_u8(name_and_domain));
                    bool last = false;
                    if (std::next(it) == cookies.end()) {
                        last = true;
                    }
                    webView2->CallDevToolsProtocolMethod(L"Network.deleteCookies", name_and_domain.c_str(),
                        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                            [last, &counter](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT { 
                                if (last) {
                                    counter++;
                                }
                                return S_OK; 
                            }).Get());
                }       
                return S_OK;
            }
        ).Get());   
}

void load_request(wxWebView* web_view, const std::string& address, const std::string& token)
{
    if (!web_view) {
        SPDLOG_ERROR("{} Failed: web_view is null.", __FUNCTION__);
        return;
    }

    ICoreWebView2* native_backend = static_cast<ICoreWebView2*>(web_view->GetNativeBackend());
    if (!native_backend) {
        SPDLOG_ERROR("{} Failed: Native WebView2 is null.", __FUNCTION__);
        return;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2_2> webview2_2;
    HRESULT hr = native_backend->QueryInterface(IID_PPV_ARGS(&webview2_2));
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: ICoreWebView2_2 interface not supported. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
        return;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2_9> webview2_9;
    hr = native_backend->QueryInterface(IID_PPV_ARGS(&webview2_9));
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: ICoreWebView2_9 interface not supported. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
        return;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env;
    hr = webview2_9->get_Environment(&env);
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: Could not retrieve WebView2 Environment. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
        return;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2Environment2> env2;
    hr = env.As(&env2);
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: ICoreWebView2Environment2 interface not supported. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
        return;
    }

    std::wstring w_url = wxString::FromUTF8(address).ToStdWstring();
    // WebView2 requires HTTP headers to be \r\n separated
    std::wstring headers = L"Authorization: External " + wxString::FromUTF8(token).ToStdWstring() + L"\r\n";

    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
    hr = env2->CreateWebResourceRequest(
        w_url.c_str(),
        L"GET",
        nullptr, // IStream* postData
        headers.c_str(),
        &request);

    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: CreateWebResourceRequest failed. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
        return;
    }

    hr = webview2_2->NavigateWithWebResourceRequest(request.Get());
    if (FAILED(hr)) {
        SPDLOG_ERROR("{} Failed: NavigateWithWebResourceRequest failed. HRESULT: {:x}", __FUNCTION__, (unsigned int)hr);
    }
}

void register_prusaslicer_url()
{
    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    // the path to binary needs to be correctly saved in string with respect to localized characters
    wxString key_wstring = L"\"" + from_u8(binary_path.string()) + L"\" \"--single-instance\" \"%1\"";
    SPDLOG_INFO("Downloader registration: Path of binary: {}", into_u8(key_wstring));
    wxRegKey key_first(wxRegKey::HKCU, L"Software\\Classes\\prusaslicer");
    wxRegKey key_full(wxRegKey::HKCU, L"Software\\Classes\\prusaslicer\\shell\\open\\command");
    if (!key_first.Exists()) {
        key_first.Create(false);
    }
    key_first.SetValue(L"URL Protocol", L"");
    if (!key_full.Exists()) {
        key_full.Create(false);
    }
    bool success = key_full.SetValue(L"", key_wstring);

    if (!success) {
        SPDLOG_ERROR("Failed to write registry value.");
    }
}

void setup_webview_devtools(wxWebView* web_view)
{
    web_view->EnableContextMenu();
    web_view->EnableAccessToDevTools();
}

} // Slic3r::App::WX::WebView
