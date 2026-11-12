

namespace Slic3r::Biz::AppInstance {
class AppInstanceMessageHandlerMac;
}

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>

@interface AppInstanceMessageHandlerMacWrapper : NSObject {
    Slic3r::Biz::AppInstance::AppInstanceMessageHandlerMac* _cppInstance;
}
-(instancetype) initWithCppInstance:(Slic3r::Biz::AppInstance::AppInstanceMessageHandlerMac*)cppInstance; 
-(void) add_observer:(NSString *)version;
-(void) message_update:(NSNotification *)note;
-(void) message_multicast_update:(NSNotification *)note;
-(void) bring_forward;

@end