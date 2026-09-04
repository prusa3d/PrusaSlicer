#import "WifiScanner.hpp"
#import "WifiScannerMac.h"

@implementation WifiScannerMac

- (instancetype)init {
    self = [super init];
    if (self) {
        // Create a CWInterface object to work with Wi-Fi interfaces
        self->_wifiInterface = [CWInterface interface];
    }
    return self;
}

- (void)dealloc {
    [super dealloc];
}

- (NSArray *)scan_ssids {
    NSMutableArray *ssids = [NSMutableArray array];
    NSError *error = nil;
    // Retrieve the list of available Wi-Fi networks
    NSSet<CWNetwork *> *networksSet = [self->_wifiInterface scanForNetworksWithName:nil error:&error];
    if (error) {
        return ssids;
    }
    NSArray<CWNetwork *> *availableNetworks = [networksSet allObjects];

    // Loop through the list of available networks and store their SSIDs
    for (CWNetwork *network in availableNetworks) {
        if (network.ssid != nil)
        {
            [ssids addObject:network.ssid];
        } 
    }
    return ssids;
}

- (NSString *)retrieve_password_for_ssid:(NSString *)ssid {
    NSString * psk;
    OSStatus status = CWKeychainFindWiFiPassword(kCWKeychainDomainSystem, [ssid dataUsingEncoding:NSUTF8StringEncoding], &psk);
    if (status == errSecSuccess) {
        return psk;
    } 
    return @""; // Password not found or an error occurred
    
}

- (NSString *)current_ssid
{
    if (self->_wifiInterface && self->_wifiInterface.ssid != nil) {
        return self->_wifiInterface.ssid;
    }
    return @"";
}

@end

namespace Slic3r {

void WifiScanner::get_ssids_mac(std::vector<std::string>& ssids)
{
    if (!m_impl_osx)
        m_impl_osx = [[WifiScannerMac alloc] init];
    if (m_impl_osx) {
        NSArray *arr = [(id)m_impl_osx scan_ssids];
        for (NSString* ssid in arr)
            ssids.push_back(std::string([ssid UTF8String]));
    }
}

std::string WifiScanner::get_psk_mac(const std::string &ssid)
{
    if (!m_impl_osx)
        m_impl_osx = [[WifiScannerMac alloc] init];
    if (m_impl_osx) {
        NSString *ns_ssid = [NSString stringWithCString:ssid.c_str() encoding:[NSString defaultCStringEncoding]];
        NSString *psk = [(id)m_impl_osx retrieve_password_for_ssid:ns_ssid];
        return std::string([psk UTF8String]);
    }
    return {};
}

std::string WifiScanner::get_current_ssid_mac()
{
    if (!m_impl_osx)
        m_impl_osx = [[WifiScannerMac alloc] init];
    if (m_impl_osx) {
        NSString *ssid = [(id)m_impl_osx current_ssid];
        return std::string([ssid UTF8String]);
    }
    return {};
}
}
