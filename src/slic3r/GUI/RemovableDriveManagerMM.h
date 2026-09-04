///|/ Copyright (c) Prusa Research 2019 David Kocík @kocikdav
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#import <Cocoa/Cocoa.h>

@interface RemovableDriveManagerMM : NSObject

-(instancetype) init;
-(void) add_unmount_observer;
-(void) on_device_unmount: (NSNotification*) notification;
-(NSArray*) list_dev;
-(void)eject_drive:(NSString *)path;
@end
