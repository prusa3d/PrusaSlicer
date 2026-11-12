#import "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerMacWrapper.h"

#include "AppInstanceMessageHandlerMac.hpp"

@implementation AppInstanceMessageHandlerMacWrapper

-(instancetype) init
{
    self = [super init];
    return self;
}
-(instancetype) initWithCppInstance:(Slic3r::Biz::AppInstance ::AppInstanceMessageHandlerMac*)cppInstance {
    self = [super init];
    if (self) {
        _cppInstance = cppInstance;
    }
    return self;
}

-(void)add_observer:(NSString *)version_hash
{
    NSString *nsver = [NSString stringWithFormat: @"%@%@", @"OtherPrusaSlicerInstanceMessage", version_hash];
    [[NSDistributedNotificationCenter defaultCenter] addObserver:self selector:@selector(message_update:) name:nsver object:nil suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
    NSString *nsnover = [NSString stringWithFormat: @"%@", @"OtherPrusaSlicerMulticastMessage"];
    [[NSDistributedNotificationCenter defaultCenter] addObserver:self selector:@selector(message_multicast_update:) name:nsnover object:nil suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
}

-(void)message_update:(NSNotification *)msg
{
    [self bring_forward];
    //pass message  
    _cppInstance->handle_message(std::string([msg.userInfo[@"data"] UTF8String]));
}

-(void)message_multicast_update:(NSNotification *)msg
{
    //pass message  
    _cppInstance->handle_message(std::string([msg.userInfo[@"data"] UTF8String]));
}

-(void) bring_forward
{
    //demiaturize all windows
    for(NSWindow* win in [NSApp windows])
    {
        if([win isMiniaturized])
        {
            [win deminiaturize:self];
        }
    }
    //bring window to front 
    [[NSApplication sharedApplication] activateIgnoringOtherApps : YES];
}

@end

#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include "Slic3r/LegacyFormat.hpp"

namespace Slic3r::Biz::AppInstance {

namespace {
std::string compose_message_json(const std::string& type, const std::string& data)
{
    return format("{ \"type\" : \"%1%\" , \"data\" : \"%2%\"}", type, data);
}
} // namespace
void AppInstanceMessageSenderMac::multicast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle)
{
    std::string msg = compose_message_json(message_type, message_data);
    NSString *nsmsg = [NSString stringWithCString:msg.c_str() encoding:[NSString defaultCStringEncoding]];
    //NSLog(@"multicast_message %@", nsmsg);
    NSString *notifname = [NSString stringWithFormat: @"%@", @"OtherPrusaSlicerMulticastMessage"];
    [[NSDistributedNotificationCenter defaultCenter] postNotificationName:notifname object:nil userInfo:[NSDictionary dictionaryWithObject:nsmsg forKey:@"data"] deliverImmediately:YES]; 
}

void AppInstanceMessageSenderMac::broadcast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle)
{
    std::string version = std::to_string(instance_hash);
    std::string msg = compose_message_json(message_type, message_data);
    NSString *nsmsg = [NSString stringWithCString:msg.c_str() encoding:[NSString defaultCStringEncoding]];
    //NSLog(@"broadcast_message %@", nsmsg);
    NSString *nsver = [NSString stringWithCString:version.c_str() encoding:[NSString defaultCStringEncoding]];
    NSString *notifname = [NSString stringWithFormat: @"%@%@", @"OtherPrusaSlicerInstanceMessage", nsver];
    [[NSDistributedNotificationCenter defaultCenter] postNotificationName:notifname object:nil userInfo:[NSDictionary dictionaryWithObject:nsmsg forKey:@"data"] deliverImmediately:YES];
}

AppInstanceMessageHandlerMac::AppInstanceMessageHandlerMac(Platform::IMainThreadDispatcher& dispatcher)
: AbstractAppInstanceMessageHandler(dispatcher)
{
    std::string version_hash = std::to_string(Biz::Platform::PlatformServices::instance().app_hash());
    m_impl_osx = [[AppInstanceMessageHandlerMacWrapper alloc] initWithCppInstance:this];
    if(m_impl_osx) {
        NSString *nsver = [NSString stringWithCString:version_hash.c_str() encoding:[NSString defaultCStringEncoding]];
        [(id)m_impl_osx add_observer:nsver];
    }else {
        NSLog(@"Error: Failed to create AppInstanceMessageHandlerMacWrapper");
    }
}

AppInstanceMessageHandlerMac::~AppInstanceMessageHandlerMac()
{
    if (m_impl_osx) {
        [(id)m_impl_osx release];
        m_impl_osx = nullptr;
    } else {
        NSLog(@"warning: unregister instance InstanceCheck notifications not required");
    }
}

} // namespace Slic3r::Biz::AppInstance
