#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/ServerTrustStore.hpp"
#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include <webkit2/webkit2.h>
#include <wx/webview.h>
#include <unordered_map>
#include <string>
#include <libsoup/soup.h>


namespace Slic3r::App::WX::WebView {

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
        SPDLOG_ERROR("Error deleting cookies: {}", err->message);
        g_error_free(err);
        return;
    }
}

/**
 * @brief user_data: the name of the cookie to delete (std::string*, owned), nullptr deletes them
 * all.
 */
void get_cookie_callback (GObject* source_object, GAsyncResult* result, void* user_data)
{
    const std::unique_ptr<std::string> name(static_cast<std::string*>(user_data));
    GError* err = nullptr;
    WebKitCookieManager *cookie_manager = WEBKIT_COOKIE_MANAGER(source_object);
    GList * cookies = webkit_cookie_manager_get_cookies_finish(cookie_manager, result, &err);

    if (err) {
        SPDLOG_ERROR("Error retrieving cookies: {}", err->message);
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
        if (!name || *name == soup_cookie_get_name(cookie)) {
            webkit_cookie_manager_delete_cookie(
                cookie_manager,
                cookie,
                nullptr,
                (GAsyncReadyCallback) Slic3r::App::WX::WebView::delete_cookie_callback,
                nullptr
            );
        }
        soup_cookie_free(cookie);
    }
    g_list_free(cookies);
}

WebKitCookieManager* cookie_manager_of(wxWebView* web_view)
{
    // The cookie manager belongs to the web context, which outlives this web view.
    WebKitWebView* native_backend = static_cast<WebKitWebView*>(web_view->GetNativeBackend());
    if (native_backend == nullptr) {
        SPDLOG_ERROR("No native web view.");
        return nullptr;
    }
    return webkit_web_context_get_cookie_manager(webkit_web_view_get_context(native_backend));
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
    webkit_cookie_manager_get_cookies(cookieManager, uri, nullptr, (GAsyncReadyCallback)Slic3r::App::WX::WebView::get_cookie_callback, nullptr);
}

void delete_cookie(wxWebView* web_view, const std::string& url, const std::string& name)
{
    if (WebKitCookieManager* cookie_manager = cookie_manager_of(web_view)) {
        webkit_cookie_manager_get_cookies(
            cookie_manager,
            url.c_str(),
            nullptr,
            (GAsyncReadyCallback) get_cookie_callback,
            new std::string(name)
        );
    }
}

namespace {
void add_cookie_callback(GObject* source_object, GAsyncResult* result, void* user_data)
{
    std::unique_ptr<std::function<void()>> done(static_cast<std::function<void()>*>(user_data));
    GError* err = nullptr;
    webkit_cookie_manager_add_cookie_finish(WEBKIT_COOKIE_MANAGER(source_object), result, &err);
    if (err) {
        SPDLOG_ERROR("Error setting a cookie: {}", err->message);
        g_error_free(err);
    }
    (*done)();
}
} // namespace

void set_cookie(
    wxWebView* web_view,
    const App::Browser::BrowserCookie& cookie,
    std::function<void()> done
)
{
    const std::optional<Biz::Network::UrlParts> url = Biz::Network::parse_url(cookie.url);
    // Without brackets and port: a host-only cookie.
    const std::string host              = url ? url->host : std::string();
    WebKitCookieManager* cookie_manager = cookie_manager_of(web_view);
    if (host.empty() || cookie_manager == nullptr) {
        SPDLOG_ERROR("{}: cannot set a cookie for {}", __FUNCTION__, cookie.url);
        done();
        return;
    }
    // A domain without a leading dot: host-only, like the one the server sets itself. Max age -1:
    // a session cookie.
    SoupCookie* soup_cookie = soup_cookie_new(
        cookie.name.c_str(),
        cookie.value.c_str(),
        host.c_str(),
        cookie.path.empty() ? "/" : cookie.path.c_str(),
        -1
    );
    soup_cookie_set_http_only(soup_cookie, cookie.http_only);
    soup_cookie_set_secure(soup_cookie, cookie.secure);
#if SOUP_CHECK_VERSION(2, 70, 0)
    if (cookie.same_site == "Strict") {
        soup_cookie_set_same_site_policy(soup_cookie, SOUP_SAME_SITE_POLICY_STRICT);
    } else if (cookie.same_site == "Lax") {
        soup_cookie_set_same_site_policy(soup_cookie, SOUP_SAME_SITE_POLICY_LAX);
    } else if (cookie.same_site == "None") {
        soup_cookie_set_same_site_policy(soup_cookie, SOUP_SAME_SITE_POLICY_NONE);
    }
#endif
    webkit_cookie_manager_add_cookie(
        cookie_manager,
        soup_cookie,
        nullptr,
        (GAsyncReadyCallback) add_cookie_callback,
        new std::function<void()>(std::move(done))
    );
    soup_cookie_free(soup_cookie);
}

void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter)
{
    delete_cookies(web_view, url);
    counter++;
}

void load_request(wxWebView* web_view, const std::string& address, const std::string& token)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView *>(web_view->GetNativeBackend());
    WebKitURIRequest* request = webkit_uri_request_new(address.c_str());
    if(!request)
    {
        SPDLOG_ERROR("load_request failed: request is nullptr. address: {}", address);
        return;
    }
    SoupMessageHeaders* soup_headers = webkit_uri_request_get_http_headers(request);
    if (!soup_headers)
    {
        SPDLOG_ERROR("load_request failed: soup_headers is nullptr.");
        return;
    }
    if (!token.empty())
    {
        soup_message_headers_append(soup_headers, "Authorization", ("External " + token).c_str());
    }
    
    // Load the request in the WebView
    webkit_web_view_load_request(native_backend, request);
}

extern "C" {
static void run_script_finished(GObject* source, GAsyncResult* res, gpointer)
{
    GError* err = nullptr;
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    WebKitJavascriptResult* js_result = webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(source), res, &err);
    wxGCC_WARNING_RESTORE(deprecated-declarations)
    if (!js_result) {
        // A JavaScript exception arrives here as WEBKIT_JAVASCRIPT_ERROR carrying the exception text.
        SPDLOG_ERROR("WebView script failed: {}", err ? err->message : "unknown error");
        g_clear_error(&err);
        return;
    }
    webkit_javascript_result_unref(js_result);
}
}

void run_script(wxWebView* web_view, const std::string& javascript)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView*>(web_view->GetNativeBackend());
    if (!native_backend) {
        SPDLOG_ERROR("{} Failed: Native WebKitWebView is null.", __FUNCTION__);
        return;
    }
    // Same call wx makes, minus its eval("...") wrapper. Deprecated since WebKitGTK 2.40
    // in favour of webkit_web_view_evaluate_javascript(); wx still uses this one too.
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    webkit_web_view_run_javascript(native_backend, javascript.c_str(), nullptr, run_script_finished, nullptr);
    wxGCC_WARNING_RESTORE(deprecated-declarations)
}

void setup_webview_devtools(wxWebView* web_view)
{
    web_view->EnableContextMenu();
    web_view->EnableAccessToDevTools();
}

namespace {
constexpr const char* SERVER_TRUST_DATA_KEY = "prusaslicer-server-trust";

/** @brief Hosts with a certificate accepted for the web context, main thread only. */
std::unordered_set<std::string> g_allowed_hosts;

/** @brief PEM certificates from the certificate up its issuers. */
std::vector<std::string> certificate_chain(GTlsCertificate* certificate)
{
    std::vector<std::string> chain;
    for (GTlsCertificate* cert = certificate; cert != nullptr;
         cert                  = g_tls_certificate_get_issuer(cert))
    {
        gchar* pem = nullptr;
        g_object_get(cert, "certificate-pem", &pem, nullptr);
        if (pem != nullptr) {
            chain.emplace_back(pem);
            g_free(pem);
        }
    }
    return chain;
}

/*
 * A certificate accepted below stays accepted for its host by the whole web context: WebKitGTK
 * has no API to revoke webkit_web_context_allow_tls_certificate_for_host() nor one to accept a
 * certificate for a single load. So every committed page is checked again (on_load_changed()):
 * once the trust is forgotten or replaced, a page that reached the server only thanks to the
 * former acceptance is stopped. Limitation: requests of a page loaded before the change (scripts,
 * XHR) still pass until the page is navigated again or the app restarts.
 */
gboolean on_load_failed_with_tls_errors(
    WebKitWebView* web_view,
    gchar* failing_uri,
    GTlsCertificate* certificate,
    GTlsCertificateFlags errors,
    gpointer
)
{
    if (failing_uri == nullptr || certificate == nullptr) {
        return FALSE;
    }
    const std::optional<Biz::Network::UrlParts> url = Biz::Network::parse_url(failing_uri);
    if (!url) {
        return FALSE;
    }
    const std::optional<Biz::Network::ServerTrust> trust =
        AppServices::instance().server_trust_store().trust_for_host(url->host, url->port);
    if (!trust) {
        // Not the Connect Local server: the load fails as before.
        return FALSE;
    }

    if (!trust->accepts_chain(certificate_chain(certificate), url->host)) {
        SPDLOG_WARN(
            "WebView: the certificate of {}:{} does not match the trusted one, refused.",
            url->host,
            url->port
        );
        return FALSE;
    }

    // Allowed for this host (all loads of the context, also the page's own requests), then retried.
    g_allowed_hosts.insert(url->host);
    webkit_web_context_allow_tls_certificate_for_host(
        webkit_web_view_get_context(web_view),
        certificate,
        url->host.c_str()
    );
    webkit_web_view_load_uri(web_view, failing_uri);
    return TRUE;
}

/**
 * @brief Stops a committed page whose certificate failed the system check and is not (or no
 * longer) accepted by the Connect Local trust, see above.
 */
void on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer)
{
    if (load_event != WEBKIT_LOAD_COMMITTED || g_allowed_hosts.empty()) {
        return;
    }
    GTlsCertificate* certificate = nullptr; // Owned by the web view.
    GTlsCertificateFlags errors  = GTlsCertificateFlags(0);
    if (!webkit_web_view_get_tls_info(web_view, &certificate, &errors)
        || errors == 0
        || certificate == nullptr)
    {
        // Not https, or a certificate the system trusts.
        return;
    }
    const gchar* uri = webkit_web_view_get_uri(web_view);
    const std::optional<Biz::Network::UrlParts> url =
        uri != nullptr ? Biz::Network::parse_url(uri) : std::nullopt;
    if (!url || !g_allowed_hosts.contains(url->host)) {
        // Not accepted by on_load_failed_with_tls_errors(): none of this code's business.
        return;
    }
    const std::optional<Biz::Network::ServerTrust> trust =
        AppServices::instance().server_trust_store().trust_for_host(url->host, url->port);
    if (trust && trust->accepts_chain(certificate_chain(certificate), url->host)) {
        return;
    }
    SPDLOG_WARN("WebView: the certificate of {} is no longer trusted, the page is stopped.", uri);
    webkit_web_view_stop_loading(web_view);
    webkit_web_view_load_plain_text(web_view, "The server certificate is not trusted.");
}
} // namespace

void setup_webview_server_trust(wxWebView* web_view)
{
    WebKitWebView* native_backend = static_cast<WebKitWebView*>(web_view->GetNativeBackend());
    if (!native_backend
        || g_object_get_data(G_OBJECT(native_backend), SERVER_TRUST_DATA_KEY) != nullptr)
    {
        return;
    }
    g_object_set_data(G_OBJECT(native_backend), SERVER_TRUST_DATA_KEY, GINT_TO_POINTER(1));
    g_signal_connect(
        native_backend,
        "load-failed-with-tls-errors",
        G_CALLBACK(on_load_failed_with_tls_errors),
        nullptr
    );
    g_signal_connect(native_backend, "load-changed", G_CALLBACK(on_load_changed), nullptr);
}

void webview_server_trust_changed(wxWebView*)
{
    // Nothing to drop: WebKitGTK cannot revoke an accepted certificate, the next committed page
    // is checked instead (on_load_changed()).
}

} // namespace Slic3r::App::WX::WebView
