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

namespace Slic3r::GUI {
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
void add_request_authorization(wxWebView* web_view, const wxString& address, const std::string& token)
{
    // unused on MacOS
    assert(true);
}
void remove_request_authorization(wxWebView* web_view)
{
    // unused on MacOS
    assert(true);
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
}

