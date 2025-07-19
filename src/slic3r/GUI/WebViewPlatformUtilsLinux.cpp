#include <webkit2/webkit2.h>
#include <wx/webview.h>
#include <unordered_map>
#include <string>

#include "WebViewPlatformUtils.hpp"
#include <boost/log/trivial.hpp>

#include <libsoup/soup.h>

namespace Slic3r::GUI {

struct Credentials {
    std::string username;
    std::string password;
};

std::unordered_map<WebKitWebView*, gulong> g_webview_authorize_handlers;

gboolean webkit_authorize_handler(WebKitWebView *web_view, WebKitAuthenticationRequest *request, gpointer user_data)
{
    const Credentials& creds = *static_cast<const Credentials*>(user_data);
    webkit_authentication_request_authenticate(request, webkit_credential_new(creds.username.c_str(), creds.password.c_str(), WEBKIT_CREDENTIAL_PERSISTENCE_PERMANENT));
    return TRUE;
}

void free_credentials(gpointer user_data, GClosure* closure)
{
    Credentials* creds = static_cast<Credentials*>(user_data);
    delete creds;
}

void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView *>(web_view->GetNativeBackend());
    Credentials* user_data = new Credentials{username, password};

    remove_webview_credentials(web_view);
    auto handler = g_signal_connect_data(
        native_backend,
        "authenticate",
        G_CALLBACK(webkit_authorize_handler),
        user_data,
        &free_credentials,
        static_cast<GConnectFlags>(0)
    );
    g_webview_authorize_handlers[native_backend] = handler;
}

void remove_webview_credentials(wxWebView* web_view)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView *>(web_view->GetNativeBackend());
    if (auto it = g_webview_authorize_handlers.find(native_backend);
        it != g_webview_authorize_handlers.end()) {
        g_signal_handler_disconnect(native_backend, it->second);
        g_webview_authorize_handlers.erase(it);
    }
}
namespace {
void delete_cookie_callback (GObject* source_object, GAsyncResult* result, void* user_data)
{
    WebKitCookieManager *cookie_manager = WEBKIT_COOKIE_MANAGER(source_object);
    GError* err = nullptr;
    webkit_cookie_manager_delete_cookie_finish(cookie_manager, result, &err);
    if (err) {
        BOOST_LOG_TRIVIAL(error) << "Error deleting cookies: " << err->message;
        g_error_free(err);
        return;
    } 
}
void get_cookie_callback (GObject* source_object, GAsyncResult* result, void* user_data)
{
    GError* err = nullptr;
    WebKitCookieManager *cookie_manager = WEBKIT_COOKIE_MANAGER(source_object);
    GList * cookies = webkit_cookie_manager_get_cookies_finish(cookie_manager, result, &err);

    if (err) {
        BOOST_LOG_TRIVIAL(error) << "Error retrieving cookies: " << err->message;
        g_error_free(err);
        return;
    } 
    for (GList *l = cookies; l != nullptr; l = l->next) {
        SoupCookie *cookie = static_cast<SoupCookie *>(l->data);
        /*
        printf("Cookie Name: %s\n", cookie->name);
        printf("Cookie Value: %s\n", cookie->value);
        printf("Domain: %s\n", cookie->domain);
        printf("Path: %s\n", cookie->path);
        printf("Expires: %s\n", soup_date_to_string(cookie->expires, SOUP_DATE_HTTP));
        printf("Secure: %s\n", cookie->secure ? "true" : "false");
        printf("HTTP Only: %s\n", cookie->http_only ? "true" : "false");
        */
        webkit_cookie_manager_delete_cookie(cookie_manager, cookie, nullptr, (GAsyncReadyCallback)Slic3r::GUI::delete_cookie_callback, nullptr);
        soup_cookie_free(cookie);
    }
    g_list_free(cookies);
}
}
void delete_cookies(wxWebView* web_view, const std::string& url)
{
    // Call webkit_cookie_manager_get_cookies
    // set its callback to call webkit_cookie_manager_get_cookies_finish
    // then for each cookie call webkit_cookie_manager_delete_cookie
    // set callback to call webkit_cookie_manager_delete_cookie_finish
    const gchar* uri = url.c_str(); 
    WebKitWebView* native_backend = static_cast<WebKitWebView *>(web_view->GetNativeBackend());
    WebKitWebContext* context= webkit_web_view_get_context(native_backend);
    WebKitCookieManager* cookieManager = webkit_web_context_get_cookie_manager(context);
    webkit_cookie_manager_get_cookies(cookieManager, uri, nullptr, (GAsyncReadyCallback)Slic3r::GUI::get_cookie_callback, nullptr);
}

void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter)
{
    delete_cookies(web_view, url);
    counter++;
}

void add_request_authorization(wxWebView* web_view, const wxString& address, const std::string& token)
{
    // unused on Linux
    assert(true);
}
void remove_request_authorization(wxWebView* web_view)
{
    // unused on Linux
    assert(true);
}

void load_request(wxWebView* web_view, const std::string& address, const std::string& token)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView *>(web_view->GetNativeBackend());
    WebKitURIRequest* request = webkit_uri_request_new(address.c_str());
    if(!request)
    {
        BOOST_LOG_TRIVIAL(error) << "load_request failed: request is nullptr. address: " << address;
        return;
    }
    SoupMessageHeaders* soup_headers = webkit_uri_request_get_http_headers(request);
    if (!soup_headers)
    {
        BOOST_LOG_TRIVIAL(error) << "load_request failed: soup_headers is nullptr.";
        return;
    }
    if (!token.empty())
    {
        soup_message_headers_append(soup_headers, "Authorization", ("External " + token).c_str());
    }
    
    // Load the request in the WebView
    webkit_web_view_load_request(native_backend, request);
}
}