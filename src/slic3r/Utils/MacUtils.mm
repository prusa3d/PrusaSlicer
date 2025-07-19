#import "AppUpdater.hpp"

#import <Foundation/Foundation.h>

namespace Slic3r {

// AppUpdater.hpp
std::string get_downloads_path_mac()
{
	// 1)
	NSArray * paths = NSSearchPathForDirectoriesInDomains (NSDownloadsDirectory, NSUserDomainMask, YES);
	NSString * desktopPath = [paths objectAtIndex:0];
    return std::string([desktopPath UTF8String]);
	// 2)
	//[NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:@"Downloads"]];
	//return std::string();
}

}
