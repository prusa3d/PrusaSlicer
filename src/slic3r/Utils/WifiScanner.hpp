#ifndef slic3r_WifiScanner_hpp_
#define slic3r_WifiScanner_hpp_

#include <map>
#include <vector>
#include <string>
#include <wx/string.h>

#ifdef _WIN32
#include <wlanapi.h>
#endif //_WIN32

namespace Slic3r {

using  WifiSsidPskMap = std::map<wxString, std::string>;

class WifiScanner
{
public:
    WifiScanner();
    ~WifiScanner();
    
    bool is_init() const { return m_init; }

    const WifiSsidPskMap& get_map() const { return m_map; }
    // returns psk for given ssid
    // used on APPLE where each psk query requires user to give their password
    std::string get_psk(const std::string& ssid);
    const std::string get_current_ssid() { return m_current_ssid; }
    // fills map with ssid psk couples (or only ssid if no psk)
    // on APPLE only ssid
    void scan();
private:
    WifiSsidPskMap m_map;
    std::string m_current_ssid;

    bool m_init { false };

#ifdef _WIN32
    void fill_wifi_map(Slic3r::WifiSsidPskMap& wifi_map, std::string& connected_ssid);
    HINSTANCE m_wlanapi_handle;
    // Functions of wlanapi used by fill_wifi_map
    using WlanOpenHandleFunc = DWORD(WINAPI*)(DWORD, PVOID, PDWORD, PHANDLE);
    using WlanEnumInterfacesFunc = DWORD(WINAPI*)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST*);
    using WlanQueryInterfaceFunc = DWORD(WINAPI*)(HANDLE, const GUID*, WLAN_INTF_OPCODE, PVOID, PDWORD, PVOID*, PWLAN_OPCODE_VALUE_TYPE);
    using WlanFreeMemoryFunc = VOID(WINAPI*)(PVOID);
    using WlanGetProfileFunc = DWORD(WINAPI*)(HANDLE, const GUID*, LPCWSTR, PVOID, LPWSTR*, DWORD*, DWORD*);
    using WlanGetProfileListFunc = DWORD(WINAPI*)(HANDLE, const GUID*, PVOID, PWLAN_PROFILE_INFO_LIST*);
    using WlanGetAvailableNetworkListFunc = DWORD(WINAPI*)(HANDLE, const GUID*, DWORD, PVOID, PWLAN_AVAILABLE_NETWORK_LIST*);
    using WlanCloseHandleFunc = DWORD(WINAPI*)(HANDLE, PVOID);

    WlanOpenHandleFunc wlanOpenHandleFunc;
    WlanEnumInterfacesFunc wlanEnumInterfacesFunc;
    WlanQueryInterfaceFunc wlanQueryInterfaceFunc;
    WlanFreeMemoryFunc wlanFreeMemoryFunc;
    WlanGetProfileFunc wlanGetProfileFunc;
    WlanGetProfileListFunc wlanGetProfileListFunc;
    WlanGetAvailableNetworkListFunc wlanGetAvailableNetworkListFunc;
    WlanCloseHandleFunc wlanCloseHandleFunc;
#elif __APPLE__
    void get_ssids_mac(std::vector<std::string>& ssids);
    std::string get_psk_mac(const std::string& ssid);
    std::string get_current_ssid_mac();
    void* m_impl_osx { nullptr };
#endif
};

} // Slic3r

#endif