#import "Slic3r/DirectoriesMacUtils.hpp"

#import <Foundation/Foundation.h>

#include <string>

namespace Slic3r {

std::string get_platform_data_dir()
{
    NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory
                                                 inDomain:NSUserDomainMask
                                                 appropriateForURL:nil create:NO error:nil];

    return std::string([url.path UTF8String]);
}

std::string get_platform_cache_dir()
{
    NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSCachesDirectory
                                                 inDomain:NSUserDomainMask
                                                 appropriateForURL:nil create:NO error:nil];

    return std::string([url.path UTF8String]);
}

} // namespace Slic3r
