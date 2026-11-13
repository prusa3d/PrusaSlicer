#include "WebViewPlatformUtils.hpp"

#import <WebKit/WKWebView.h>
#import <WebKit/WKNavigationDelegate.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKWebsiteDataStore.h>
#import <Foundation/Foundation.h>
#import <Foundation/NSURLSession.h>
#import <Security/Security.h>
#import <objc/runtime.h>

#include "Slic3r/App/ConnectLocalTrust.hpp"
#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Log.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

/**
 * @brief Certificates of a server trust, leaf first, DER.
 */
std::vector<std::string> certificate_chain(SecTrustRef trust)
{
    std::vector<std::string> chain;
    const auto add = [&chain](SecCertificateRef certificate) {
        CFDataRef der = SecCertificateCopyData(certificate);
        if (der != nullptr) {
            const char* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(der));
            chain.emplace_back(bytes, size_t(CFDataGetLength(der)));
            CFRelease(der);
        }
    };
    if (@available(macOS 12.0, *)) {
        CFArrayRef certificates = SecTrustCopyCertificateChain(trust);
        if (certificates != nullptr) {
            for (CFIndex i = 0; i < CFArrayGetCount(certificates); ++i) {
                add((SecCertificateRef) CFArrayGetValueAtIndex(certificates, i));
            }
            CFRelease(certificates);
        }
    } else {
        // Deprecated in macOS 12, the only way before.
        for (CFIndex i = 0; i < SecTrustGetCertificateCount(trust); ++i) {
            add(SecTrustGetCertificateAtIndex(trust, i));
        }
    }
    return chain;
}

} // namespace

/**
 * @brief Navigation delegate in front of wxWidgets' one, installed on a web view with credentials
 * or in the offline mode (see slicer_navigation_delegate()). It answers the authentication
 * challenges:
 * - the server trust of the Connect Local server with its custom trust (ConnectLocalTrust),
 * - any other one with the credentials, when set (physical printers with a password),
 * .
 * and forwards everything else to wxWidgets' delegate as if this one was not here.
 */
@interface SlicerNavigationDelegate: NSObject<WKNavigationDelegate> {
    id<WKNavigationDelegate> _wrapped; // Not retained, owned by wxWebView.
    NSString* _username;
    NSString* _password;
}
- (id) initWithWrappedDelegate: (id<WKNavigationDelegate>) wrapped;
/** @brief nil clears them. */
- (void) setUserName: (NSString*) username password: (NSString*) password;
- (BOOL) hasCredentials;
- (id<WKNavigationDelegate>) wrappedDelegate;
@end

@implementation SlicerNavigationDelegate
- (id) initWithWrappedDelegate: (id<WKNavigationDelegate>) wrapped {
    if (self = [super init]) {
        _wrapped = wrapped;
    }
    return self;
}

- (void) dealloc {
    [_username release];
    [_password release];
    [super dealloc];
}

- (void) setUserName: (NSString*) username password: (NSString*) password {
    [_username release];
    [_password release];
    _username = [username copy];
    _password = [password copy];
}

- (BOOL) hasCredentials {
    return _username != nil;
}

- (id<WKNavigationDelegate>) wrappedDelegate {
    return _wrapped;
}

// WebKit reads which methods the delegate implements when it is set as the delegate.
- (BOOL) respondsToSelector: (SEL) selector {
    if (selector == @selector(webView:authenticationChallenge:shouldAllowDeprecatedTLS:)) {
        // Printers with a password may still run an old TLS.
        return _username != nil || [_wrapped respondsToSelector:selector];
    }
    return [super respondsToSelector:selector]
        || (_wrapped != nil && [_wrapped respondsToSelector:selector]);
}

- (id) forwardingTargetForSelector: (SEL) selector {
    if (_wrapped != nil && [_wrapped respondsToSelector:selector]) {
        return _wrapped;
    }
    return [super forwardingTargetForSelector:selector];
}

- (void) passOn: (WKWebView*) webView
      challenge: (NSURLAuthenticationChallenge*) challenge
      completionHandler: (void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential* _Nullable)) completionHandler {
    const SEL wrapped_handler = @selector(webView:didReceiveAuthenticationChallenge:completionHandler:);
    if (_username != nil) {
        completionHandler(
            NSURLSessionAuthChallengeUseCredential,
            [NSURLCredential credentialWithUser:_username
                                       password:_password
                                    persistence:NSURLCredentialPersistenceForSession]
        );
    } else if (_wrapped != nil && [_wrapped respondsToSelector:wrapped_handler]) {
        [_wrapped webView:webView
            didReceiveAuthenticationChallenge:challenge
                            completionHandler:completionHandler];
    } else {
        // What WebKit does when the delegate does not implement this method.
        completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    }
}

- (void) webView: (WKWebView*) webView
    didReceiveAuthenticationChallenge: (NSURLAuthenticationChallenge*) challenge
    completionHandler: (void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential* _Nullable)) completionHandler {
    NSURLProtectionSpace* space = challenge.protectionSpace;
    if (![space.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust]
        || space.serverTrust == nil)
    {
        [self passOn:webView challenge:challenge completionHandler:completionHandler];
        return;
    }
    const std::string host = space.host != nil ? std::string([space.host UTF8String]) : "";
    const std::optional<Slic3r::Biz::Network::ServerTrust> trust =
        Slic3r::App::ConnectLocalTrust::trust_for_host(host, int(space.port));
    if (!trust) {
        // Not the Connect Local server, or its certificate is checked by the system.
        [self passOn:webView challenge:challenge completionHandler:completionHandler];
        return;
    }
    if (trust->accepts_chain(certificate_chain(space.serverTrust), host)) {
        completionHandler(
            NSURLSessionAuthChallengeUseCredential,
            [NSURLCredential credentialForTrust:space.serverTrust]
        );
    } else {
        SPDLOG_WARN(
            "Web view: the certificate of {}:{} does not match the trusted one, refused.",
            host,
            int(space.port)
        );
        completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    }
}

- (void) webView: (WKWebView*) webView
    authenticationChallenge: (NSURLAuthenticationChallenge*) challenge
    shouldAllowDeprecatedTLS: (void (^)(BOOL)) decisionHandler API_AVAILABLE(macos(11.0)) {
    if ([_wrapped respondsToSelector:_cmd]) {
        [_wrapped webView:webView
            authenticationChallenge:challenge
           shouldAllowDeprecatedTLS:decisionHandler];
    } else {
        decisionHandler(YES);
    }
}
@end

static char SlicerNavigationDelegateKey;

namespace {

/**
 * @brief The SlicerNavigationDelegate of the web view, installed on the first call.
 */
SlicerNavigationDelegate* slicer_navigation_delegate(WKWebView* backend)
{
    SlicerNavigationDelegate* delegate =
        objc_getAssociatedObject(backend, &SlicerNavigationDelegateKey);
    if (delegate == nil) {
        delegate =
            [[SlicerNavigationDelegate alloc] initWithWrappedDelegate:backend.navigationDelegate];
        // navigationDelegate is weak: the web view keeps the delegate alive by the association.
        objc_setAssociatedObject(
            backend, &SlicerNavigationDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN
        );
        [delegate release];
        backend.navigationDelegate = delegate;
    }
    return delegate;
}

/** @brief Puts wxWidgets' delegate back, as if SlicerNavigationDelegate was never installed. */
void remove_slicer_navigation_delegate(WKWebView* backend)
{
    SlicerNavigationDelegate* delegate =
        objc_getAssociatedObject(backend, &SlicerNavigationDelegateKey);
    if (delegate == nil) {
        return;
    }
    backend.navigationDelegate = [delegate wrappedDelegate];
    objc_setAssociatedObject(backend, &SlicerNavigationDelegateKey, nil, OBJC_ASSOCIATION_RETAIN);
}

/**
 * @brief The Connect Local trust is needed in the offline mode only: online, a web view gets the
 * delegate only for credentials, as before Connect Local.
 */
bool needs_server_trust()
{
    return Slic3r::Biz::Network::ServiceConfig::instance().is_offline();
}

} // namespace

#include "Slic3r/Assert.hpp"

namespace Slic3r::App::WX::WebView {
void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password)
{
    WKWebView* backend                 = static_cast<WKWebView*>(web_view->GetNativeBackend());
    SlicerNavigationDelegate* delegate = slicer_navigation_delegate(backend);
    [delegate setUserName:[NSString stringWithFormat:@"%s", username.c_str()]
                 password:[NSString stringWithFormat:@"%s", password.c_str()]];
    // WebKit reads which methods the delegate implements only when it is set.
    backend.navigationDelegate = delegate;
}

void remove_webview_credentials(wxWebView* web_view)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    SlicerNavigationDelegate* delegate =
        objc_getAssociatedObject(backend, &SlicerNavigationDelegateKey);
    if (delegate == nil) {
        return;
    }
    if (!needs_server_trust()) {
        remove_slicer_navigation_delegate(backend);
        return;
    }
    [delegate setUserName:nil password:nil];
    backend.navigationDelegate = delegate;
}

void delete_cookies(wxWebView* web_view, const std::string& url)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    NSString *url_string = [NSString stringWithCString:url.c_str() encoding:[NSString defaultCStringEncoding]];
    WKWebsiteDataStore *data_store = backend.configuration.websiteDataStore;
    NSSet *website_data_types = [NSSet setWithObject:WKWebsiteDataTypeCookies];
    [data_store fetchDataRecordsOfTypes:website_data_types completionHandler:^(NSArray<WKWebsiteDataRecord *> *records) {
        for (WKWebsiteDataRecord *record in records) {
            if ([url_string containsString:record.displayName]) {
                [data_store removeDataOfTypes:website_data_types
                              forDataRecords:@[record]
                           completionHandler:^{
                    //NSLog(@"Deleted cookies for domain: %@", record.displayName);
                }];
            }
        }
    }];
}

void delete_cookie(wxWebView* web_view, const std::string& url, const std::string& name)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    NSURL* ns_url = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    NSString* host = ns_url.host.lowercaseString;
    NSString* cookie_name = [NSString stringWithUTF8String:name.c_str()];
    if (host == nil || cookie_name == nil) {
        return;
    }
    // The cookie store is shared by the web views and retained by the blocks below, so the
    // deletion finishes even if this web view is destroyed meanwhile.
    WKHTTPCookieStore* store = backend.configuration.websiteDataStore.httpCookieStore;
    [store getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
        for (NSHTTPCookie* cookie in cookies) {
            NSString* domain = cookie.domain.lowercaseString;
            if ([domain hasPrefix:@"."]) {
                domain = [domain substringFromIndex:1];
            }
            if ([cookie.name isEqualToString:cookie_name] && [domain isEqualToString:host]) {
                [store deleteCookie:cookie completionHandler:nil];
            }
        }
    }];
}

void set_cookie(
    wxWebView* web_view,
    const App::Browser::BrowserCookie& cookie,
    std::function<void()> done
)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    NSURL* ns_url = [NSURL URLWithString:[NSString stringWithUTF8String:cookie.url.c_str()]];
    NSString* name  = [NSString stringWithUTF8String:cookie.name.c_str()];
    NSString* value = [NSString stringWithUTF8String:cookie.value.c_str()];
    NSString* path  = [NSString stringWithUTF8String:(cookie.path.empty() ? "/" : cookie.path.c_str())];
    if (backend == nil || ns_url == nil || ns_url.host == nil || name == nil || value == nil || path == nil) {
        SPDLOG_ERROR("{}: the cookie cannot be built.", __FUNCTION__);
        done();
        return;
    }
    NSMutableDictionary* properties = [NSMutableDictionary dictionary];
    // The origin, not a domain: a host-only cookie, like the one the server sets itself.
    properties[NSHTTPCookieOriginURL] = ns_url;
    properties[NSHTTPCookieName]      = name;
    properties[NSHTTPCookieValue]     = value;
    properties[NSHTTPCookiePath]      = path;
    if (cookie.secure) {
        properties[NSHTTPCookieSecure] = @"TRUE";
    }
    if (cookie.http_only) {
        // NSHTTPCookie has no public property key for HttpOnly. "HttpOnly" is undocumented but
        // read by NSHTTPCookie (isHTTPOnly is true then). Should it ever be ignored, the cookie
        // would become readable by the page's scripts.
        properties[@"HttpOnly"] = @"TRUE";
    }
    if (@available(macOS 10.15, *)) {
        if (cookie.same_site == "Strict") {
            properties[NSHTTPCookieSameSitePolicy] = NSHTTPCookieSameSiteStrict;
        } else if (cookie.same_site == "Lax") {
            properties[NSHTTPCookieSameSitePolicy] = NSHTTPCookieSameSiteLax;
        }
    }
    NSHTTPCookie* ns_cookie = [NSHTTPCookie cookieWithProperties:properties];
    if (ns_cookie == nil) {
        SPDLOG_ERROR("{}: the cookie was refused.", __FUNCTION__);
        done();
        return;
    }
    WKHTTPCookieStore* store = backend.configuration.websiteDataStore.httpCookieStore;
    [store setCookie:ns_cookie completionHandler:^{
        done();
    }];
}

void delete_cookies_with_counter(wxWebView* web_view, const std::string& url, std::atomic<size_t>& counter)
{
    delete_cookies(web_view, url);
    counter++;
}

void load_request(wxWebView* web_view, const std::string& address, const std::string& token)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    NSString *url_string = [NSString stringWithCString:address.c_str() encoding:[NSString defaultCStringEncoding]];
    NSString *token_string = [NSString stringWithCString:token.c_str() encoding:[NSString defaultCStringEncoding]];
    NSURL *url = [NSURL URLWithString:url_string];
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NSString *auth_value = [NSString stringWithFormat:@"External %@", token_string];
    [request setValue:auth_value forHTTPHeaderField:@"Authorization"];
    [backend loadRequest:request];
}

void run_script(wxWebView* web_view, const std::string& javascript)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    if (!backend) {
        SPDLOG_ERROR("{} Failed: Native WKWebView is null.", __FUNCTION__);
        return;
    }
    // UTF-8 on purpose: the script carries JSON (tokens, plugin ids) that must survive intact.
    NSString *script_string = [NSString stringWithCString:javascript.c_str() encoding:NSUTF8StringEncoding];
    // Runs the string verbatim (no eval wrapper). The completion handler is called on the main thread.
    [backend evaluateJavaScript:script_string completionHandler:^(id result, NSError *error) {
        if (error) {
            SPDLOG_ERROR("WebView script failed: {}", [[error localizedDescription] UTF8String]);
        }
    }];
}

void setup_webview_devtools(wxWebView* web_view)
{
    web_view->EnableContextMenu();
    web_view->EnableAccessToDevTools();
}

void setup_webview_server_trust(wxWebView* web_view)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    if (backend == nil) {
        return;
    }
    if (needs_server_trust()) {
        slicer_navigation_delegate(backend);
        return;
    }
    SlicerNavigationDelegate* delegate =
        objc_getAssociatedObject(backend, &SlicerNavigationDelegateKey);
    if (delegate != nil && ![delegate hasCredentials]) {
        // Switched online: installed for the trust only.
        remove_slicer_navigation_delegate(backend);
    }
}

void webview_server_trust_changed(wxWebView* web_view)
{
    // WebKit keeps no decision about a server trust: every challenge (each new TLS connection) is
    // answered by the delegate with the trust in effect. Connections open already are reused
    // until they close. Only the service mode decides whether the delegate is needed.
    setup_webview_server_trust(web_view);
}
}

