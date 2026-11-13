#include "WebViewPlatformUtils.hpp"

#import <WebKit/WKWebView.h>
#import <WebKit/WKNavigationDelegate.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKWebsiteDataStore.h>
#import <Foundation/Foundation.h>
#import <Foundation/NSURLSession.h>

@interface MyNavigationDelegate: NSObject<WKNavigationDelegate> {
    id<WKNavigationDelegate> _delegate;
    NSString* _username;
    NSString* _passwd;
}

- (id) initWithOriginalDelegate: (id<WKNavigationDelegate>) delegate userName: (const char*) username password: (const char*) password;

- (id<WKNavigationDelegate>) wrapped_delegate;

@end

@implementation MyNavigationDelegate
- (id) initWithOriginalDelegate: (id<WKNavigationDelegate>) delegate userName: (const char*) username password: (const char*) password {
    if (self = [super init]) {
        _delegate = delegate;
        _username = [[NSString alloc] initWithFormat:@"%s", username];
        _passwd = [[NSString alloc] initWithFormat:@"%s", password];
    }
    return self;
}

- (id<WKNavigationDelegate>) wrapped_delegate {
    return _delegate;
}

- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    if ([_delegate respondsToSelector:@selector(webView:decidePolicyForNavigationAction:decisionHandler:)]) {
        [_delegate webView:webView decidePolicyForNavigationAction:navigationAction decisionHandler:decisionHandler];
    } else {
        decisionHandler(WKNavigationActionPolicyAllow);
    }
}

- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction preferences:(WKWebpagePreferences *)preferences decisionHandler:(void (^)(WKNavigationActionPolicy, WKWebpagePreferences *))decisionHandler API_AVAILABLE(macos(10.15), ios(13.0)) {
    if ([_delegate respondsToSelector:@selector(webView:decidePolicyForNavigationAction:preferences:decisionHandler:)]) {
        [_delegate webView:webView decidePolicyForNavigationAction:navigationAction preferences:preferences decisionHandler:decisionHandler];
    } else {
        decisionHandler(WKNavigationActionPolicyAllow, preferences);
    }
}

- (void)webView:(WKWebView *)webView decidePolicyForNavigationResponse:(WKNavigationResponse *)navigationResponse decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
    if ([_delegate respondsToSelector:@selector(webView:decidePolicyForNavigationResponse:decisionHandler:)]) {
        [_delegate webView:webView decidePolicyForNavigationResponse: navigationResponse decisionHandler: decisionHandler];
    } else {
        decisionHandler(WKNavigationResponsePolicyAllow);
    }
}

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(null_unspecified WKNavigation *)navigation {
    if ([_delegate respondsToSelector:@selector(webView:didStartProvisionalNavigation:)]) {
        [_delegate webView:webView didStartProvisionalNavigation:navigation];
    }
}

- (void)webView:(WKWebView *)webView didReceiveServerRedirectForProvisionalNavigation:(null_unspecified WKNavigation *)navigation {
    if ([_delegate respondsToSelector:@selector(webView:didReceiveServerRedirectForProvisionalNavigation:)]) {
        [_delegate webView:webView didReceiveServerRedirectForProvisionalNavigation:navigation];
    }
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(null_unspecified WKNavigation *)navigation withError:(NSError *)error {
    if ([_delegate respondsToSelector:@selector(webView:didFailProvisionalNavigation:withError:)]) {
        [_delegate webView:webView didFailProvisionalNavigation:navigation withError:error];
    }
}

- (void)webView:(WKWebView *)webView didCommitNavigation:(null_unspecified WKNavigation *)navigation {
    if ([_delegate respondsToSelector:@selector(webView:didCommitNavigation:)]) {
        [_delegate webView:webView didCommitNavigation:navigation];
    }
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(null_unspecified WKNavigation *)navigation {
    if ([_delegate respondsToSelector:@selector(webView:didFinishNavigation:)]) {
        [_delegate webView:webView didFinishNavigation:navigation];
    }
}

- (void)webView:(WKWebView *)webView didFailNavigation:(null_unspecified WKNavigation *)navigation withError:(NSError *)error {
    if ([_delegate respondsToSelector:@selector(webView:didFailNavigation:withError:)]) {
        [_delegate webView:webView didFailNavigation:navigation withError:error];
    }
}

- (void)webView:(WKWebView *)webView didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition, NSURLCredential * _Nullable credential))completionHandler {
    //challenge.protectionSpace.realm
    completionHandler(
        NSURLSessionAuthChallengeUseCredential,
        [NSURLCredential credentialWithUser: _username password: _passwd persistence: NSURLCredentialPersistenceForSession]
    );
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView *)webView API_AVAILABLE(macos(10.11), ios(9.0)) {
    if ([_delegate respondsToSelector:@selector(webViewWebContentProcessDidTerminate:)]) {
        [_delegate webViewWebContentProcessDidTerminate:webView];
    }
}

- (void)webView:(WKWebView *)webView authenticationChallenge:(NSURLAuthenticationChallenge *)challenge shouldAllowDeprecatedTLS:(void (^)(BOOL))decisionHandler API_AVAILABLE(macos(11.0), ios(14.0)) {
    if ([_delegate respondsToSelector:@selector(webView:authenticationChallenge:shouldAllowDeprecatedTLS:)]) {
        [_delegate webView:webView authenticationChallenge:challenge shouldAllowDeprecatedTLS:decisionHandler];
    } else {
        decisionHandler(YES);
    }
}
@end

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App::WX::WebView {
void setup_webview_with_credentials(wxWebView* web_view, const std::string& username, const std::string& password)
{
    remove_webview_credentials(web_view);
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    if (![backend.navigationDelegate isKindOfClass:MyNavigationDelegate.class]) {
        backend.navigationDelegate = [[MyNavigationDelegate alloc]
            initWithOriginalDelegate:backend.navigationDelegate
                            userName:username.c_str()
                            password:password.c_str()];
    }
}

void remove_webview_credentials(wxWebView* web_view)
{
    WKWebView* backend = static_cast<WKWebView*>(web_view->GetNativeBackend());
    if ([backend.navigationDelegate isKindOfClass:MyNavigationDelegate.class]) {
        MyNavigationDelegate* my_delegate = backend.navigationDelegate;
        backend.navigationDelegate = my_delegate.wrapped_delegate;
    }
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
}

