#include "WifiScanner.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <boost/log/trivial.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/filesystem.hpp>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wtypes.h>

#include "libslic3r//Utils.hpp"

#elif __APPLE_
#include "WifiScannerMac.h"
#endif 

#if __linux__
#include <dbus/dbus.h> /* Pull in all of D-Bus headers. */
#endif //__linux__

namespace {

#ifdef _WIN32
bool ptree_get_value(const boost::property_tree::ptree& pt, const std::string& target, std::string& result)
{
    // Check if the current node has the target element
    if (pt.find(target) != pt.not_found()) {
        result = pt.get<std::string>(target);
        return true;
    }

    // Recursively search child nodes
    for (const auto& child : pt) {
        if (ptree_get_value(child.second, target, result)) {
            return true;
        }
    }

    return false;  // Element not found in this subtree
}

#elif __APPLE__
void get_connected_ssid(std::string& connected_ssid)
{
    std::string program = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -I";
    std::string regexpstr = "[ \t]+SSID: (.+)";

    std::ostringstream output;
    std::string line;

    // Run the process and capture its output
    FILE* pipe = popen(program.c_str(), "r");
    if (!pipe) {
        BOOST_LOG_TRIVIAL(error) << "Error executing airport command." << std::endl;
        return;
    }

    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        line = buffer;
        output << line;
    }
    BOOST_LOG_TRIVIAL(error) << output.str();
    pclose(pipe);
    
    // Process the captured output using regular expressions
    std::regex rx(regexpstr);
    std::smatch match;

    std::istringstream outputStream(output.str());
    while (std::getline(outputStream, line)) {
        BOOST_LOG_TRIVIAL(error) << line;
        if (std::regex_search(line, match, rx)) {
            connected_ssid = match[1].str();
            // airport -I gets data only about current connection, so only 1 network.
            return;
        }
    }
}
#else 

DBusMessage* dbus_query(DBusConnection* connection, const char* service, const char* object, const char* interface, const char* method)
{
    DBusError error;
    dbus_error_init(&error);

    DBusMessage* msg = dbus_message_new_method_call(service, object, interface, method);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, -1, &error);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&error)) {
        // todo (debug)
        BOOST_LOG_TRIVIAL(debug) << "D-Bus method call error: " << error.message << std::endl;
        dbus_error_free(&error);
        return nullptr;
    }

    return reply;
}

// On each access point call method Get on interface org.freedesktop.DBus.Properties to get the ssid
void iter_access_points(DBusConnection* connection, DBusMessage* access_points, Slic3r::WifiSsidPskMap& wifi_map)
{
    DBusError error;
    dbus_error_init(&error);

    DBusMessageIter iter;
    dbus_message_iter_init(access_points, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        BOOST_LOG_TRIVIAL(debug) << "Error iterating access points reply - not an array.";
        return;
    }
    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_OBJECT_PATH) {
        const char* object_path;
        dbus_message_iter_get_basic(&array_iter, &object_path);

        DBusMessage* msg = dbus_message_new_method_call(
            "org.freedesktop.NetworkManager",
            object_path,
            "org.freedesktop.DBus.Properties",
            "Get");

        const char* arg1 = "org.freedesktop.NetworkManager.AccessPoint";
        const char* arg2 = "Ssid";
        dbus_message_append_args(
            msg, 
            DBUS_TYPE_STRING, &arg1,
            DBUS_TYPE_STRING, &arg2,
            DBUS_TYPE_INVALID 
        );

        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, -1, &error);
        dbus_message_unref(msg);

        if (dbus_error_is_set(&error)) {
            BOOST_LOG_TRIVIAL(debug) << "D-Bus method call error: " << error.message << std::endl;
            dbus_error_free(&error);
            dbus_message_iter_next(&array_iter);
            continue;
        }

        // process reply
        DBusMessageIter rep_iter;
        dbus_message_iter_init(reply, &rep_iter);
        dbus_message_unref(reply);
        if (dbus_message_iter_get_arg_type(&rep_iter) != DBUS_TYPE_VARIANT) {
            BOOST_LOG_TRIVIAL(debug) <<  "Reply does not contain a Variant";
            dbus_message_iter_next(&array_iter);
            continue;
        }
        
        DBusMessageIter variant_iter;
        dbus_message_iter_recurse(&rep_iter, &variant_iter);

        if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY) {
            BOOST_LOG_TRIVIAL(debug) << "Variant does not contain an array";
            dbus_message_iter_next(&array_iter);
            continue;
        }

        DBusMessageIter var_array_iter;
        dbus_message_iter_recurse(&variant_iter, &var_array_iter);

        if (dbus_message_iter_get_arg_type(&var_array_iter) != DBUS_TYPE_BYTE) {
            BOOST_LOG_TRIVIAL(debug) << "Array does not contain bytes";
            dbus_message_iter_next(&array_iter);
            continue;
        }

        unsigned char *result;
        int result_len;

        // Get the array of bytes and its length
        dbus_message_iter_get_fixed_array(&var_array_iter, &result, &result_len);

        wifi_map[result] = std::string(); 
        
        dbus_message_iter_next(&array_iter);
    }  
}

// For each device call method GetAllAccessPoints to get object path to all Access Point objects
void iter_devices(DBusConnection* connection, DBusMessage* devices, Slic3r::WifiSsidPskMap& wifi_map)
{
    DBusError error;
    dbus_error_init(&error);

    DBusMessageIter iter;
    dbus_message_iter_init(devices, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        BOOST_LOG_TRIVIAL(debug) << "Error iterating devices reply - not an array.";
        return;
    }
    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_OBJECT_PATH) {
       const char* object_path;
        dbus_message_iter_get_basic(&array_iter, &object_path);

        // Create a new message to get all access points for this device
        DBusMessage* reply = dbus_query(
            connection,
            "org.freedesktop.NetworkManager",      // Service name
            object_path,                           // Object path (device path)
            "org.freedesktop.NetworkManager.Device.Wireless",  // Interface for Wi-Fi devices
            "GetAllAccessPoints"                   // Method name
        );
        if (reply) {
            iter_access_points(connection, reply, wifi_map);
            dbus_message_unref(reply);
        }

        dbus_message_iter_next(&array_iter);
    }   
}

// Query NetworkManager for available Wi-Fi.
// On org.freedesktop.NetworkManager call method GetAllDevices to get object paths to all device objects.
// For each device call method GetAllAccessPoints to get object path to all Access Point objects (iter_devices function here).
// On each access point call method Get on interface org.freedesktop.DBus.Properties to get the ssid (iter_access_points function here).
void fill_wifi_map(Slic3r::WifiSsidPskMap& wifi_map)
{
    DBusConnection* connection;
    DBusError error;
    dbus_error_init(&error);

    // Connect to the system bus
    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);

    if (dbus_error_is_set(&error)) {
        BOOST_LOG_TRIVIAL(debug) << "D-Bus connection error: " << error.message << std::endl;
        dbus_error_free(&error);
    }

    // 
    DBusMessage* reply = dbus_query(
        connection,
        "org.freedesktop.NetworkManager",      // Service name
        "/org/freedesktop/NetworkManager",     // Object path
        "org.freedesktop.NetworkManager",      // Interface
        "GetAllDevices"                        // Method name
    );
    if (reply) {
        iter_devices(connection, reply, wifi_map);
        dbus_message_unref(reply);
    }

    dbus_connection_unref(connection);
}
#endif //__linux__
}
namespace Slic3r
{
WifiScanner::WifiScanner()
{
#ifdef _WIN32
    m_wlanapi_handle = LoadLibrary(L"wlanapi.dll");
    if (m_wlanapi_handle == NULL)
        return;

    wlanOpenHandleFunc = reinterpret_cast<WlanOpenHandleFunc>(GetProcAddress(m_wlanapi_handle, "WlanOpenHandle"));
    wlanEnumInterfacesFunc = reinterpret_cast<WlanEnumInterfacesFunc>(GetProcAddress(m_wlanapi_handle, "WlanEnumInterfaces"));
    wlanQueryInterfaceFunc = reinterpret_cast<WlanQueryInterfaceFunc>(GetProcAddress(m_wlanapi_handle, "WlanQueryInterface"));
    wlanFreeMemoryFunc = reinterpret_cast<WlanFreeMemoryFunc>(GetProcAddress(m_wlanapi_handle, "WlanFreeMemory"));
    wlanGetProfileFunc = reinterpret_cast<WlanGetProfileFunc>(GetProcAddress(m_wlanapi_handle, "WlanGetProfile"));
    wlanGetProfileListFunc = reinterpret_cast<WlanGetProfileListFunc>(GetProcAddress(m_wlanapi_handle, "WlanGetProfileList"));
    wlanGetAvailableNetworkListFunc = reinterpret_cast<WlanGetAvailableNetworkListFunc>(GetProcAddress(m_wlanapi_handle, "WlanGetAvailableNetworkList"));
    wlanCloseHandleFunc = reinterpret_cast<WlanCloseHandleFunc>(GetProcAddress(m_wlanapi_handle, "WlanCloseHandle"));

    if (!wlanOpenHandleFunc || !wlanEnumInterfacesFunc || !wlanQueryInterfaceFunc || !wlanFreeMemoryFunc
        || !wlanGetProfileFunc || !wlanGetProfileListFunc || !wlanGetAvailableNetworkListFunc || !wlanCloseHandleFunc)
        return;
#endif // _WIN32
    m_init = true;
}
WifiScanner::~WifiScanner()
{
#ifdef _WIN32
    if (m_wlanapi_handle)
        FreeLibrary(m_wlanapi_handle);
#endif // _WIN32

}
void WifiScanner::scan()
{
    if (!m_init)
        return;
    m_map.clear();
#ifdef _WIN32
    fill_wifi_map(m_map, m_current_ssid);
#elif __APPLE__
    std::vector<std::string> ssids;
    try
    {
        // Objective-c implementation using CoreWLAN library
        // This failed to get data at ARM Sonoma
        get_ssids_mac(ssids);
    }
    catch (const std::exception&)
    {
         BOOST_LOG_TRIVIAL(error) << "Exception caught: Getting SSIDs failed.";   
    }
    
    for ( const std::string& ssid : ssids)
    {
        if (!ssid.empty())
            m_map[boost::nowide::widen(ssid)] = {};
    }
    if (m_map.empty()) {
        try
        {
            // Second implementation calling "airport" system command
            get_connected_ssid(m_current_ssid);
            if (!m_current_ssid.empty())
                m_map[m_current_ssid] = std::string();
        }
        catch (const std::exception&)
        {
            BOOST_LOG_TRIVIAL(error) << "Exception caught: get_connected_ssid failed.";
        }
    } else {
        try
        {
            m_current_ssid = get_current_ssid_mac();
        }
        catch (const std::exception&)
        {
            BOOST_LOG_TRIVIAL(error) << "Exception caught: Getting current SSID failed.";
        }
    }
   
#else 
    fill_wifi_map(m_map);
#endif 
}
std::string WifiScanner::get_psk(const std::string& ssid)
{
#ifdef __APPLE__
    return get_psk_mac(ssid);
#endif
    if (m_map.find(ssid) != m_map.end())
    {
        return m_map[ssid];
    }
    return {};
}

#ifdef _WIN32
// Fill SSID map. Implementation from Raspberry Pi imager and Win32 Api examples.
// https://github.com/raspberrypi/rpi-imager/blob/qml/src/windows/winwlancredentials.cpp
// https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlangetavailablenetworklist
void WifiScanner::fill_wifi_map(Slic3r::WifiSsidPskMap& wifi_map, std::string& connected_ssid)
{
    HANDLE handle;
    DWORD supported_version = 0;
    DWORD client_version = 2;
    PWLAN_INTERFACE_INFO_LIST interface_list = NULL;

    if (!m_init)
        return;
   
    if (wlanOpenHandleFunc(client_version, NULL, &supported_version, &handle) != ERROR_SUCCESS)
        return;
   
    Slic3r::ScopeGuard guard([this, &handle] { wlanCloseHandleFunc(handle, NULL); });

    if (wlanEnumInterfacesFunc(handle, NULL, &interface_list) != ERROR_SUCCESS)
        return;

    for (DWORD i = 0; i < interface_list->dwNumberOfItems; i++)
    {
        if (interface_list->InterfaceInfo[i].isState == wlan_interface_state_connected)
        {
            PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
            DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
            WLAN_OPCODE_VALUE_TYPE opCode = wlan_opcode_value_type_invalid;

            if (wlanQueryInterfaceFunc(handle, &interface_list->InterfaceInfo[i].InterfaceGuid,
                wlan_intf_opcode_current_connection, NULL,
                &connectInfoSize, (PVOID*)&pConnectInfo, &opCode) == ERROR_SUCCESS && pConnectInfo && pConnectInfo->wlanAssociationAttributes.dot11Ssid.uSSIDLength)
            {
                connected_ssid = std::string((const char*)pConnectInfo->wlanAssociationAttributes.dot11Ssid.ucSSID,
                    pConnectInfo->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                wlanFreeMemoryFunc(pConnectInfo);
            }
        }

        PWLAN_PROFILE_INFO_LIST profile_list = NULL;
        PWLAN_INTERFACE_INFO interface_info_entry = NULL;
        PWLAN_AVAILABLE_NETWORK_LIST available_network_list = NULL;
        WCHAR guid[39] = { 0 };

        // Get all available networks.
        interface_info_entry = (WLAN_INTERFACE_INFO*)&interface_list->InterfaceInfo[i];
        int iRet = StringFromGUID2(interface_info_entry->InterfaceGuid, (LPOLESTR)&guid,
            sizeof(guid) / sizeof(*guid));

        if (wlanGetAvailableNetworkListFunc(handle,
            &interface_info_entry->InterfaceGuid,
            0,
            NULL,
            &available_network_list)
            != ERROR_SUCCESS)
        {
            continue;
        }

        for (unsigned int j = 0; j < available_network_list->dwNumberOfItems; j++)
        {
            PWLAN_AVAILABLE_NETWORK available_network_entry = NULL;
            wxString ssid;

            // Store SSID into the map.
            available_network_entry =
                (WLAN_AVAILABLE_NETWORK*)&available_network_list->Network[j];

            if (available_network_entry->dot11Ssid.uSSIDLength != 0)
                ssid = wxString(available_network_entry->dot11Ssid.ucSSID,
                    available_network_entry->dot11Ssid.uSSIDLength);

            if (ssid.empty())
                continue;

            if (wifi_map.find(ssid) != wifi_map.end())
                continue;

            wifi_map[ssid] = std::string();

            if (wlanGetProfileListFunc(handle, &interface_list->InterfaceInfo[i].InterfaceGuid,
                NULL, &profile_list) != ERROR_SUCCESS)
            {
                continue;
            }
            // enmurate all stored profiles, take password from matching one.
            for (DWORD k = 0; k < profile_list->dwNumberOfItems; k++)
            {
                DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                DWORD access = 0;
                DWORD ret = 0;
                LPWSTR xmlstr = NULL;
                wxString s(profile_list->ProfileInfo[k].strProfileName);

                BOOST_LOG_TRIVIAL(debug) << "Enumerating wlan profiles, SSID found:" << s << " looking for:" << ssid;

                if (s != ssid)
                    continue;

                if ((ret = wlanGetProfileFunc(handle, &interface_list->InterfaceInfo[i].InterfaceGuid, profile_list->ProfileInfo[k].strProfileName,
                    NULL, &xmlstr, &flags, &access)) == ERROR_SUCCESS && xmlstr)
                {
                    wxString xml(xmlstr);
                    boost::property_tree::ptree pt;
                    std::stringstream ss(Slic3r::GUI::into_u8(xml));
                    boost::property_tree::read_xml(ss, pt);
                    std::string password;
                    std::string psk_protected;

                    BOOST_LOG_TRIVIAL(debug) << "XML wlan profile:" << xml;

                    // break if password is not readable
                    // TODO: what if there is other line "protected" in the XML?
                    if (ptree_get_value(pt, "protected", psk_protected) && psk_protected != "false")
                        break;

                    if (ptree_get_value(pt, "keyMaterial", password))
                        wifi_map[ssid] = password;

                    wlanFreeMemoryFunc(xmlstr);
                    break;
                }
            }

            if (profile_list) {
                wlanFreeMemoryFunc(profile_list);
            }
        }
    }

    if (interface_list)
        wlanFreeMemoryFunc(interface_list);
}
#endif // _WIN32
} // Slic3r
