#import <Cocoa/Cocoa.h>
#include <functional>

@interface RemovableDriveMonitorMacObserver : NSObject

-(instancetype) init NS_UNAVAILABLE;
-(instancetype) initWithCallback:(std::function<void()>)callback;

-(void) add_unmount_observer;
-(NSArray*) list_dev;

@end

@interface RemovableDriveMonitorMacEnumerator : NSObject

-(instancetype) init;

-(NSArray*) list_dev;

@end