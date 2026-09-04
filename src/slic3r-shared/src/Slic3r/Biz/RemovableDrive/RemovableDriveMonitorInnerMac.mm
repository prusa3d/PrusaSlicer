#import "RemovableDriveMonitorInnerMac.h"
#import "RemovableDriveMonitorMac.hpp"
#import <AppKit/AppKit.h> 
#import <DiskArbitration/DiskArbitration.h>

#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

@implementation RemovableDriveMonitorMacObserver
{
    std::function<void()> _on_change_callback;
}

-(instancetype) initWithCallback:(std::function<void()>)callback
{
    self = [super init];
    if (self) {
        _on_change_callback = std::move(callback);
    }
    return self;
}

-(void) on_device_unmount:(NSNotification*)notification
{
    if (_on_change_callback) {
        _on_change_callback();
    }
}

-(void) add_unmount_observer
{
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidUnmountNotification object:nil];
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidMountNotification object:nil];
}

@end

@implementation RemovableDriveMonitorMacEnumerator

-(instancetype) init
{
    self = [super init];
    return self;
}

-(NSArray*) list_dev
{
    NSArray *mountedRemovableMedia = [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:nil options:NSVolumeEnumerationSkipHiddenVolumes];
    NSMutableArray *result = [NSMutableArray array];
    for(NSURL *volURL in mountedRemovableMedia)
    {
        int                 err = 0;
        DADiskRef           disk = nullptr;
        CFDictionaryRef     descDict = nullptr;        
        DASessionRef        session = DASessionCreate(nullptr);
        if (session == nullptr)
            err = EINVAL;
        if (err == 0) {
            disk = DADiskCreateFromVolumePath(nullptr,session,(CFURLRef)volURL);
            if (disk == nullptr)
                err = EINVAL;
        }
        if (err == 0) {
            descDict = DADiskCopyDescription(disk);
            if (descDict == nullptr)
                err = EINVAL;
        }
        if (err == 0) {
            CFTypeRef mediaEjectableKey = CFDictionaryGetValue(descDict,kDADiskDescriptionMediaEjectableKey);
            BOOL ejectable = [(id)mediaEjectableKey boolValue];
            CFTypeRef deviceProtocolName = CFDictionaryGetValue(descDict,kDADiskDescriptionDeviceProtocolKey);
            
            CFTypeRef deviceModelKey = CFDictionaryGetValue(descDict, kDADiskDescriptionDeviceModelKey);
            if (mediaEjectableKey != nullptr) {
                BOOL op = ejectable && 
                    ( (deviceProtocolName != nullptr && (CFEqual(deviceProtocolName, CFSTR("USB")) || CFEqual(deviceProtocolName, CFSTR("Secure Digital")))) || 
                      (deviceModelKey     != nullptr && CFEqual(deviceModelKey, CFSTR("SD Card Reader"))) );
                if (op)
                    [result addObject:volURL.path];
            }
        }
        if (descDict != nullptr)
            CFRelease(descDict);
        if (session != nullptr)
            CFRelease(session);
        if (disk != nullptr)
            CFRelease(disk);
    }
    return result;
}

@end

namespace Slic3r::Biz::RemovableDrive {

void RemovableDriveMonitorMac::init_observer(std::function<void()> callback)
{
    m_observer = [[RemovableDriveMonitorMacObserver alloc] initWithCallback:callback];
    if (m_observer)
        [(id)m_observer add_unmount_observer];
}

void RemovableDriveMonitorMac::relase_observer()
{
    if (m_observer) {
        [(id)m_observer release];
        m_observer = nullptr;
    }
}

void RemovableDriveMonitorMac::init_enumerator()
{
    m_enumerator = [[RemovableDriveMonitorMacEnumerator alloc] init];
}

void RemovableDriveMonitorMac::relase_enumerator()
{
    if (m_enumerator) {
        [(id)m_enumerator release];
        m_enumerator = nullptr;
    }
}

namespace  
{
    bool compare_filesystem_id(const std::string &path_a, const std::string &path_b)
    {
        struct stat buf;
        stat(path_a.c_str() ,&buf);
        dev_t id_a = buf.st_dev;
        stat(path_b.c_str() ,&buf);
        dev_t id_b = buf.st_dev;
        return id_a == id_b;
    }

    void inspect_file(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
    {
        //confirms if the file is removable drive and adds it to vector
        if (! compare_filesystem_id(path, parent_path)) {
            //free space
            boost::system::error_code ec;
            boost::filesystem::space_info si = boost::filesystem::space(path, ec);
            if (!ec && si.available != 0) {
                //user id
                struct stat buf;
                stat(path.c_str(), &buf);
                uid_t uid = buf.st_uid;
                if (getuid() == uid)
                    out.emplace_back(DriveData{ boost::filesystem::path(path).stem().string(), path });
            }
        }
    }
}

void RemovableDriveMonitorMac::list_devices(std::vector<DriveData>& out) const
{
    if (m_enumerator) {
        NSArray* devices = [(id)m_enumerator list_dev];
        for (NSString* volumePath in devices)
            inspect_file(std::string([volumePath UTF8String]), "/Volumes", out);
    }
}


} //namespace Slic3r::Biz::RemovableDrive