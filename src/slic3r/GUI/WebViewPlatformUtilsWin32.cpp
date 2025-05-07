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
        BOOST_LOG_TRIVIAL(error) << "setup_webview_with_credentials Failed: Webview 2 is null.";
        return;
    }
    wxCOMPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = webView2->QueryInterface(IID_PPV_ARGS(&wv2_10));
    if (FAILED(hr)) {
        BOOST_LOG_TRIVIAL(error) << "setup_webview_with_credentials Failed: ICoreWebView2_10 is null.";
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
        BOOST_LOG_TRIVIAL(error) << "remove_webview_credentials Failed: webView2 is null.";
        return;
    }
    wxCOMPtr<ICoreWebView2_10> wv2_10;
    HRESULT hr = webView2->QueryInterface(IID_PPV_ARGS(&wv2_10));
    if (FAILED(hr)) {
        BOOST_LOG_TRIVIAL(error) << "remove_webview_credentials Failed: ICoreWebView2_10 is null.";
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
        BOOST_LOG_TRIVIAL(error) << "delete_cookies Failed: webView2 is null.";
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
                    BOOST_LOG_TRIVIAL(debug) << "Deleting cookie: " << name_and_domain;
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
        BOOST_LOG_TRIVIAL(error) << "delete_cookies Failed: webView2 is null.";
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
            [webView2, url, &counter](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT {
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
                    wxString name_and_domain = GUI::format_wxstr(L"{\"name\": \"%1%\", \"domain\": \"%2%\"}", name, domain);
                    BOOST_LOG_TRIVIAL(debug) << "Deleting cookie: " << name_and_domain;
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
static EventRegistrationToken m_webResourceRequestedTokenForImageBlocking = {};
static wxString filter_patern;
namespace {
void RequestHeadersToLog(ICoreWebView2HttpRequestHeaders* requestHeaders)
{
    wxCOMPtr<ICoreWebView2HttpHeadersCollectionIterator> iterator;
    requestHeaders->GetIterator(&iterator);
    BOOL hasCurrent = FALSE;
     BOOST_LOG_TRIVIAL(info) <<"Logging request headers:";

    while (SUCCEEDED(iterator->get_HasCurrentHeader(&hasCurrent)) && hasCurrent)
    {
        wchar_t* name = nullptr;
        wchar_t* value = nullptr;

        iterator->GetCurrentHeader(&name, &value);
        BOOST_LOG_TRIVIAL(trace) <<"name: " << name << L", value: " << value;
        if (name) {
            CoTaskMemFree(name);
        }
        if (value) {
            CoTaskMemFree(value);
        }

        BOOL hasNext = FALSE;
        iterator->MoveNext(&hasNext);
    }
}
}

void add_request_authorization(wxWebView* webview, const wxString& address, const std::string& token)
{
    // This function adds a filter so when pattern document is being requested, callback is triggered
    // Inside add_WebResourceRequested callback, there is a Authorization header added.
    // The filter needs to be removed to stop adding the auth header
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        BOOST_LOG_TRIVIAL(error) << "Adding request Authorization Failed: Webview 2 is null.";
        return;
    }
    wxCOMPtr<ICoreWebView2_2> wv2_2;
    HRESULT hr = webView2->QueryInterface(IID_PPV_ARGS(&wv2_2));
    if (FAILED(hr)) {
        BOOST_LOG_TRIVIAL(error) << "Adding request Authorization Failed: QueryInterface ICoreWebView2_2 has failed.";
        return;        
    }
    filter_patern =  address + "/*";
    webView2->AddWebResourceRequestedFilter( filter_patern.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT);
    
    if (FAILED(webView2->add_WebResourceRequested(
            Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [token](ICoreWebView2 *sender, ICoreWebView2WebResourceRequestedEventArgs *args) {
                    // Get the web resource request
                    wxCOMPtr<ICoreWebView2WebResourceRequest> request;
                    HRESULT hr = args->get_Request(&request);
                    if (FAILED(hr))
                    {
                        BOOST_LOG_TRIVIAL(error) << "Adding request Authorization: Failed to get_Request.";
                        return S_OK;
                    }
                    // Get the request headers
                    wxCOMPtr<ICoreWebView2HttpRequestHeaders> headers;
                    hr = request->get_Headers(&headers);
                    if (FAILED(hr))
                    {
                         BOOST_LOG_TRIVIAL(error) << "Adding request Authorization: Failed to get_Headers.";
                         return S_OK;
                    }
                    LPWSTR wideUri = nullptr; 
                    request->get_Uri(&wideUri);
                    std::wstring ws(wideUri);

                    std::string val = "External " + token;
                    // Add or modify the Authorization header
                    hr = headers->SetHeader(L"Authorization", GUI::from_u8(val).c_str());
                    BOOST_LOG_TRIVIAL(debug) << "add_WebResourceRequested " << ws;

                    // This function is only needed for debug purpose
                    RequestHeadersToLog(headers.Get());    
                    return S_OK;
                }
            ).Get(), &m_webResourceRequestedTokenForImageBlocking
            ))) {

        BOOST_LOG_TRIVIAL(error) << "Adding request Authorization: Failed to add callback.";
    }
    
    
}

void remove_request_authorization(wxWebView* webview)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(webview->GetNativeBackend());
    if (!webView2) {
        BOOST_LOG_TRIVIAL(error) << "remove_request_authorization Failed: webView2 is null.";
        return;
    }
     BOOST_LOG_TRIVIAL(info) << "remove_request_authorization";
    webView2->RemoveWebResourceRequestedFilter(filter_patern.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT);
    if(FAILED(webView2->remove_WebResourceRequested( m_webResourceRequestedTokenForImageBlocking))) {
        BOOST_LOG_TRIVIAL(error) << "WebView: Failed to remove resources";
    }
}

void load_request(wxWebView* web_view, const std::string& address, const std::string& token)
{
    // This function should create its own GET request and send it (works on linux)
    // For that we would use NavigateWithWebResourceRequest.
    // For that we need ICoreWebView2Environment smart pointer.
    // Such pointer does exists inside wxWebView edge backend. (wxWebViewEdgeImpl::m_webViewEnvironment)
    // But its currently private and not getable. (It wouldn't be such problem to create the getter)
    
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(web_view->GetNativeBackend());
    if (!webView2) {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: webView2 is null.";
        return;
    }
   
    // GetEnviroment does not exists
    wxCOMPtr<ICoreWebView2Environment> webViewEnvironment;
    //webViewEnvironment = static_cast<ICoreWebView2Environment *>(web_view->GetEnviroment());
    if (!webViewEnvironment.Get()) {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: ICoreWebView2Environment is null.";
        return;
    }

    wxCOMPtr<ICoreWebView2Environment2> webViewEnvironment2;
    if (FAILED(webViewEnvironment->QueryInterface(IID_PPV_ARGS(&webViewEnvironment2))))
    {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: ICoreWebView2Environment2 is null.";
        return;
    }
     wxCOMPtr<ICoreWebView2WebResourceRequest> webResourceRequest;
    
    if (FAILED(webViewEnvironment2->CreateWebResourceRequest(
        L"https://www.printables.com/", L"GET", NULL,
        L"Content-Type: application/x-www-form-urlencoded", &webResourceRequest)))
    {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: CreateWebResourceRequest failed.";
        return;
    }
    wxCOMPtr<ICoreWebView2_2> wv2_2;
    if (FAILED(webView2->QueryInterface(IID_PPV_ARGS(&wv2_2)))) {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: ICoreWebView2_2 is null.";
        return;        
    }
    if (FAILED(wv2_2->NavigateWithWebResourceRequest(webResourceRequest.get())))
    {
        BOOST_LOG_TRIVIAL(error) << "load_request Failed: NavigateWithWebResourceRequest failed.";
        return;
    } 
}
} // namespace Slic3r::GUI
#endif // WIN32
