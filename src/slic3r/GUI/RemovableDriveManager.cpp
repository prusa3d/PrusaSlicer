///|/ Copyright (c) Prusa Research 2019 - 2023 David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Roman Beránek @zavorka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "RemovableDriveManager.hpp"
#include "libslic3r/Platform.hpp"
#include <libslic3r/libslic3r.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>

#if _WIN32
#include <windows.h>
#include <tchar.h>
#include <winioctl.h>
#include <shlwapi.h>
#include <Dbt.h>
#include <Setupapi.h>
#include <cfgmgr32.h>

#include <initguid.h>   // include before devpropdef.h
#include <devpropdef.h>
#include <devpkey.h>
#include <usbioctl.h>

#include <atlbase.h>
#include <atlcom.h>
#include <shldisp.h>
#else
// unix, linux & OSX includes
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <pwd.h>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/process.hpp>
#endif

namespace Slic3r {
namespace GUI { 

wxDEFINE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);
wxDEFINE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);
wxDEFINE_EVENT(EVT_REMOVABLE_DRIVE_ADDED, wxCommandEvent);


#if _WIN32
std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	// Get logical drives flags by letter in alphabetical order.
	DWORD drives_mask = ::GetLogicalDrives();

	// Allocate the buffers before the loop.
	std::wstring volume_name;
	std::wstring file_system_name;
	// Iterate the Windows drives from 'C' to 'Z'
	std::vector<DriveData> current_drives;
	// Skip A and B drives.
	drives_mask >>= 2;
	for (char drive = 'C'; drive <= 'Z'; ++ drive, drives_mask >>= 1)
		if (drives_mask & 1) {
			std::string path { drive, ':' };
			UINT drive_type = ::GetDriveTypeA(path.c_str());
			// DRIVE_REMOVABLE on W are sd cards and usb thumbnails (not usb harddrives)
			if (drive_type ==  DRIVE_REMOVABLE) {
				// get name of drive
				std::wstring wpath = boost::nowide::widen(path);
				volume_name.resize(MAX_PATH + 1);
				file_system_name.resize(MAX_PATH + 1);
				BOOL error = ::GetVolumeInformationW(wpath.c_str(), volume_name.data(), sizeof(volume_name), nullptr, nullptr, nullptr, file_system_name.data(), sizeof(file_system_name));
				if (error != 0) {
					volume_name.erase(volume_name.begin() + wcslen(volume_name.c_str()), volume_name.end());
					if (! file_system_name.empty()) {
						ULARGE_INTEGER free_space;
						::GetDiskFreeSpaceExW(wpath.c_str(), &free_space, nullptr, nullptr);
						if (free_space.QuadPart > 0) {
                            path += "\\";
                            current_drives.emplace_back(DriveData{ boost::nowide::narrow(volume_name), path });
						}
					}
				}
			}
		}
	return current_drives;
}

namespace {


#if 0
// From https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview
typedef struct _STRING_DESCRIPTOR_NODE
{
	struct _STRING_DESCRIPTOR_NODE* Next;
	UCHAR                           DescriptorIndex;
	USHORT                          LanguageID;
	USB_STRING_DESCRIPTOR           StringDescriptor[1];
} STRING_DESCRIPTOR_NODE, * PSTRING_DESCRIPTOR_NODE;

// Based at https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview
PSTRING_DESCRIPTOR_NODE GetStringDescriptor(
	HANDLE  handle_hub_device,
	ULONG   connection_index,
	UCHAR   descriptor_index,
	USHORT  language_ID
)
{
	BOOL					success = 0;
	ULONG					nbytes = 0;
	ULONG					nbytes_returned = 0;
	UCHAR					string_desc_req_buf[sizeof(USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];
	PUSB_DESCRIPTOR_REQUEST string_desc_req = NULL;
	PUSB_STRING_DESCRIPTOR  string_desc = NULL;
	PSTRING_DESCRIPTOR_NODE string_desc_node = NULL;

	nbytes			= sizeof(string_desc_req_buf);
	string_desc_req = (PUSB_DESCRIPTOR_REQUEST)string_desc_req_buf;
	string_desc		= (PUSB_STRING_DESCRIPTOR)(string_desc_req + 1);

	// Zero fill the entire request structure
	memset(string_desc_req, 0, nbytes);

	// Indicate the port from which the descriptor will be requested
	string_desc_req->ConnectionIndex = connection_index;

	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	//
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	//
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	string_desc_req->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8)
		| descriptor_index;
	string_desc_req->SetupPacket.wIndex = language_ID;
	string_desc_req->SetupPacket.wLength = (USHORT)(nbytes - sizeof(USB_DESCRIPTOR_REQUEST));

	// Now issue the get descriptor request.
	success = DeviceIoControl(handle_hub_device,
		IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
		string_desc_req,
		nbytes,
		string_desc_req,
		nbytes,
		&nbytes_returned,
		NULL);

	// Do some sanity checks on the return from the get descriptor request.
	if (!success) {
		return NULL;
	}
	if (nbytes_returned < 2) {
		return NULL;
	}
	if (string_desc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE) {
		return NULL;
	}
	if (string_desc->bLength != nbytes_returned - sizeof(USB_DESCRIPTOR_REQUEST)) {
		return NULL;
	}
	if (string_desc->bLength % 2 != 0) {
		return NULL;
	}

	// Looks good, allocate some (zero filled) space for the string descriptor
	// node and copy the string descriptor to it.
	string_desc_node = (PSTRING_DESCRIPTOR_NODE)malloc(sizeof(STRING_DESCRIPTOR_NODE) + string_desc->bLength * sizeof(DWORD));
	if (string_desc_node == NULL) {
		return NULL;
	}
	string_desc_node->Next = NULL;
	string_desc_node->DescriptorIndex = descriptor_index;
	string_desc_node->LanguageID = language_ID;

	memcpy(string_desc_node->StringDescriptor,
		string_desc,
		string_desc->bLength);

	return string_desc_node;
}

// Based at https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview
HRESULT GetStringDescriptors(
	_In_ HANDLE								handle_hub_device,
	_In_ ULONG								connection_index,
	_In_ UCHAR								descriptor_index,
	_In_ ULONG								num_language_IDs,
	_In_reads_(num_language_IDs) USHORT*	language_IDs,
	_In_ PSTRING_DESCRIPTOR_NODE			string_desc_node_head,
	std::wstring&							result
)
{
	PSTRING_DESCRIPTOR_NODE tail = NULL;
	PSTRING_DESCRIPTOR_NODE trailing = NULL;
	ULONG					i = 0;

	// Go to the end of the linked list, searching for the requested index to
	// see if we've already retrieved it
	for (tail = string_desc_node_head; tail != NULL; tail = tail->Next) {
		if (tail->DescriptorIndex == descriptor_index) {
			// copy string descriptor to result
			for(int i = 0; i < tail->StringDescriptor->bLength / 2 - 1; i++) {
				result += tail->StringDescriptor->bString[i];
			}
			return S_OK;
		}
		trailing = tail;
	}
	tail = trailing;

	// Get the next String Descriptor. If this is NULL, then we're done (return)
	// Otherwise, loop through all Language IDs
	for (i = 0; (tail != NULL) && (i < num_language_IDs); i++) {
		tail->Next = GetStringDescriptor(handle_hub_device,
			connection_index,
			descriptor_index,
			language_IDs[i]);
		tail = tail->Next;
	}

	if (tail == NULL) {
		return E_FAIL;
	} else {
		// copy string descriptor to result
		for (int i = 0; i < tail->StringDescriptor->bLength / 2 - 1; i++) {
			result += tail->StringDescriptor->bString[i];
		}
		return S_OK;
	}
}

bool get_handle_from_devinst(DEVINST devinst, HANDLE& handle)
{
	// create path consisting of device id and guid
	wchar_t			device_id[MAX_PATH];
	CM_Get_Device_ID(devinst, device_id, MAX_PATH, 0);

	//convert device id string to device path - https://stackoverflow.com/a/32641140/981766
	std::wstring	dev_id_wstr(device_id);
	dev_id_wstr = std::regex_replace(dev_id_wstr, std::wregex(LR"(\\)"), L"#"); // '\' is special for regex
	dev_id_wstr = std::regex_replace(dev_id_wstr, std::wregex(L"^"), LR"(\\?\)", std::regex_constants::format_first_only);
	dev_id_wstr = std::regex_replace(dev_id_wstr, std::wregex(L"$"), L"#", std::regex_constants::format_first_only);
	
	// guid
	wchar_t			guid_wchar[64];//guid is 32 chars+4 hyphens+2 paranthesis+null => 64 should be more than enough
	StringFromGUID2(GUID_DEVINTERFACE_USB_HUB, guid_wchar, 64);
	dev_id_wstr.append(guid_wchar);

	// get handle
	std::wstring&	usb_hub_path = dev_id_wstr; 
	handle = CreateFileW(usb_hub_path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		// Sometimes device is not GUID_DEVINTERFACE_USB_HUB, than we need to check parent recursively
		DEVINST parent_devinst = 0;
		if (CM_Get_Parent(&parent_devinst, devinst, 0) != CR_SUCCESS)
			return false;
		return get_handle_from_devinst(parent_devinst, handle);
	}
	return true;
}

// Read Configuration Descriptor - configuration string indexed by iConfiguration and decide if card reader
bool is_card_reader(HDEVINFO h_dev_info, SP_DEVINFO_DATA& spdd)
{
	// First get port number of device.

	DEVINST		parent_devinst = 0;
	HANDLE		handle; // usb hub handle
	DWORD		usb_port_number = 0;
	DWORD		required_size = 0;
	// First we need handle for GUID_DEVINTERFACE_USB_HUB device.
	if (CM_Get_Parent(&parent_devinst, spdd.DevInst, 0) != CR_SUCCESS) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get parent DEVINST.";
		return false;
	}
	if(!get_handle_from_devinst(parent_devinst, handle) || handle == INVALID_HANDLE_VALUE) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get HANDLE for parent DEVINST.";
		return false;
	}
	// Get port number to which the usb device is attached on the hub.
	if (SetupDiGetDeviceRegistryProperty(h_dev_info, &spdd, SPDRP_ADDRESS, nullptr, (PBYTE)&usb_port_number, sizeof(usb_port_number), &required_size) == 0) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get port number.";
		return false;
	}

	// Fill USB request packet to get iConfiguration value.

	int								buffer_size = sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_CONFIGURATION_DESCRIPTOR);
	BYTE*							buffer = new BYTE[buffer_size];
	USB_DESCRIPTOR_REQUEST*			request_packet = (USB_DESCRIPTOR_REQUEST*)buffer;
	USB_CONFIGURATION_DESCRIPTOR*	configuration_descriptor = (USB_CONFIGURATION_DESCRIPTOR*)((BYTE*)buffer + sizeof(USB_DESCRIPTOR_REQUEST));
	DWORD							bytes_returned = 0;
	// Fill information in packet.
	request_packet->SetupPacket.bmRequest = 0x80;
	request_packet->SetupPacket.bRequest = USB_REQUEST_GET_CONFIGURATION;
	request_packet->ConnectionIndex = usb_port_number;
	request_packet->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8 | 0 /*Since only 1 device descriptor => index : 0*/);
	request_packet->SetupPacket.wLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
	// Issue ioctl.
	if (DeviceIoControl(handle, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, buffer, buffer_size, buffer, buffer_size, &bytes_returned, nullptr) == 0) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get Configuration Descriptor.";
		return false;
	}
	// Nothing to read.
	if (configuration_descriptor->iConfiguration == 0) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: iConfiguration value is 0.";
		return false;
	}

	// Get string descriptor and read string on address given by iConfiguration index .
	// Based at https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview

	PSTRING_DESCRIPTOR_NODE		supported_languages_string = NULL;
	ULONG						num_language_IDs = 0;
	USHORT*						language_IDs = NULL;
	std::wstring				configuration_string;
	// Get languages.
	supported_languages_string	= GetStringDescriptor(handle, usb_port_number, 0, 0);
	if (supported_languages_string == NULL) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get language string descriptor.";
		return false;
	}
	num_language_IDs			= (supported_languages_string->StringDescriptor->bLength - 2) / 2;
	language_IDs				= (USHORT*)&supported_languages_string->StringDescriptor->bString[0];
	// Get configration string.
	if (GetStringDescriptors(handle, usb_port_number, configuration_descriptor->iConfiguration, num_language_IDs, language_IDs, supported_languages_string, configuration_string) == E_FAIL) {
		BOOST_LOG_TRIVIAL(warning) << "is_card_reader failed: Couldn't get configuration string descriptor.";
		return false;
	}
	
	// Final compare.
	BOOST_LOG_TRIVIAL(error) << "Ejecting information: Retrieved configuration string: " << configuration_string;
	if (configuration_string.find(L"CARD READER") != std::wstring::npos) {
		BOOST_LOG_TRIVIAL(info) << "Detected external reader.";
		return true;
	}
	return false;
}

// returns the device instance handle of a storage volume or 0 on error
// called from eject_inner, based on https://stackoverflow.com/a/58848961
DEVINST get_dev_inst_by_device_number(long device_number, UINT drive_type, WCHAR* dos_device_name)
{
	bool is_floppy = (wcsstr(dos_device_name, L"\\Floppy") != NULL); // TODO: could be tested better?
	
	if (drive_type != DRIVE_REMOVABLE || is_floppy) {
		BOOST_LOG_TRIVIAL(warning) << "get_dev_inst_by_device_number failed: Drive is not removable.";
		return 0;
	}

	GUID* guid = (GUID*)& GUID_DEVINTERFACE_DISK;
	// Get device interface info set handle for all devices attached to system
	HDEVINFO h_dev_info = SetupDiGetClassDevs(guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (h_dev_info == INVALID_HANDLE_VALUE) {
		BOOST_LOG_TRIVIAL(warning) << "get_dev_inst_by_device_number failed: Invalid dev info handle.";
		return 0;
	}

	// Retrieve a context structure for a device interface of a device information set
	BYTE							 buf[1024];
	PSP_DEVICE_INTERFACE_DETAIL_DATA pspdidd = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buf;
	SP_DEVICE_INTERFACE_DATA         spdid;
	SP_DEVINFO_DATA                  spdd;
	DWORD                            size;

	spdid.cbSize = sizeof(spdid);

	// Loop through devices and compare device numbers
	for (DWORD index = 0; SetupDiEnumDeviceInterfaces(h_dev_info, NULL, guid, index, &spdid); ++index) {
		SetupDiGetDeviceInterfaceDetail(h_dev_info, &spdid, NULL, 0, &size, NULL);
		// check the buffer size 
		if (size == 0 || size > sizeof(buf)) {
			continue;
		}
		// prepare structures
		pspdidd->cbSize = sizeof(*pspdidd);
		ZeroMemory(&spdd, sizeof(spdd));
		spdd.cbSize = sizeof(spdd);
		// fill structures
		long res = SetupDiGetDeviceInterfaceDetail(h_dev_info, &spdid, pspdidd, size, &size, &spdd);
		if (!res) {
			continue;
		}
		// open the drive with pspdidd->DevicePath to compare device numbers
		HANDLE drive_handle = CreateFile(pspdidd->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (drive_handle == INVALID_HANDLE_VALUE) {
			continue;
		}
		// get its device number
		STORAGE_DEVICE_NUMBER sdn;
		DWORD			      bytes_returned = 0;
		res = DeviceIoControl(drive_handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytes_returned, NULL);
		CloseHandle(drive_handle);
		if (!res) {
			continue;
		}
		//compare
		if (device_number != (long)sdn.DeviceNumber) {
			continue;
		}

		// check if is sd card reader - if yes, indicate by returning invalid value.
		bool reader = is_card_reader(h_dev_info, spdd);

		SetupDiDestroyDeviceInfoList(h_dev_info);
		return !reader ? spdd.DevInst : 0;
	}

	SetupDiDestroyDeviceInfoList(h_dev_info);
	BOOST_LOG_TRIVIAL(warning) << "get_dev_inst_by_device_number failed: Enmurating couldn't find the drive.";
	return 0;
}

// Perform eject using CM_Request_Device_EjectW.
// Returns 0 if success.
int eject_inner(const std::string& path)
{
	// Following implementation is based on https://stackoverflow.com/a/58848961
	assert(path.size() > 0);
	std::wstring wpath = std::wstring();
	wpath += boost::nowide::widen(path)[0]; // drive letter wide
	wpath[0] &= ~0x20; // make sure drive letter is uppercase
	assert(wpath[0] >= 'A' && wpath[0] <= 'Z');
	std::wstring root_path			= wpath + L":\\"; // for GetDriveType
	std::wstring device_path		= wpath + L":"; //for QueryDosDevice
	std::wstring volume_access_path = L"\\\\.\\" + wpath + L":"; // for CreateFile
	long	     device_number		= -1;

	// open the storage volume
	HANDLE volume_handle = CreateFileW(volume_access_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
	if (volume_handle == INVALID_HANDLE_VALUE) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Invalid value of file handle.", path);
		return 1;
	}

	// get the volume's device number
	STORAGE_DEVICE_NUMBER sdn;
	DWORD bytes_returned = 0;
	long res = DeviceIoControl(volume_handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytes_returned, NULL);
	if (res) {
		device_number = sdn.DeviceNumber;
	}
	CloseHandle(volume_handle);

	if (device_number == -1) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Invalid device number.", path);
		return 1;
	}

	// get the drive type which is required to match the device numbers correctely
	UINT drive_type = GetDriveTypeW(root_path.c_str());

	// get the dos device name (like \device\floppy0) to decide if it's a floppy or not
	WCHAR dos_device_name[MAX_PATH];
	res = QueryDosDeviceW(device_path.c_str(), dos_device_name, MAX_PATH);
	if (!res) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Invalid dos device name.", path);
		return 1;
	}

	// get the device instance handle of the storage volume by means of a SetupDi enum and matching the device number
	DEVINST dev_inst = get_dev_inst_by_device_number(device_number, drive_type, dos_device_name);
	if (dev_inst == 0) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1%: Invalid device instance handle. Going to try alternative ejecting method.", path);
		return 1;
	}

	PNP_VETO_TYPE veto_type = PNP_VetoTypeUnknown;
	WCHAR		  veto_name[MAX_PATH];
	veto_name[0] = 0;

	// get drives's parent, e.g. the USB bridge, the SATA port, an IDE channel with two drives!
	DEVINST dev_inst_parent = 0;
	res = CM_Get_Parent(&dev_inst_parent, dev_inst, 0);
	if (res != CR_SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Failed to get drive parent. Code: %2%", path, res);
		return 1;
	}

#if 0
	// loop with several tries and sleep (this is running on main UI thread)
	for (int i = 0; i < 3; ++i) { 
		veto_name[0] = 0;
		// CM_Query_And_Remove_SubTree doesn't work for restricted users
		//res = CM_Query_And_Remove_SubTreeW(DevInstParent, &VetoType, VetoNameW, MAX_PATH, CM_REMOVE_NO_RESTART); // CM_Query_And_Remove_SubTreeA is not implemented under W2K!
		//res = CM_Query_And_Remove_SubTreeW(DevInstParent, NULL, NULL, 0, CM_REMOVE_NO_RESTART);  // with messagebox (W2K, Vista) or balloon (XP)
		res = CM_Request_Device_EjectW(dev_inst_parent, &veto_type, veto_name, MAX_PATH, 0);
		//res = CM_Request_Device_EjectW(DevInstParent, NULL, NULL, 0, 0); // with messagebox (W2K, Vista) or balloon (XP)
		if (res == CR_SUCCESS && veto_type == PNP_VetoTypeUnknown) {
			return 0;
		}
		// Wait for next try. 
		// This is main thread!
		Sleep(500);
	}
#endif // 0

	// Perform eject over parent dev_inst. This works for usb drives and some SD cards.
	res = CM_Request_Device_EjectW(dev_inst_parent, &veto_type, veto_name, MAX_PATH, 0);
	//res = CM_Query_And_Remove_SubTreeW(dev_inst_parent, &veto_type, veto_name, MAX_PATH, CM_REMOVE_UI_OK);
	if (res == CR_SUCCESS && veto_type == PNP_VetoTypeUnknown) {
		return 0;
	}
	BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Request to eject device has failed. Another request will follow. Veto type: %2%", path, veto_type);

	// But on some PC, SD cards ejects only with its own dev_inst. 
	res = CM_Request_Device_EjectW(dev_inst, &veto_type, veto_name, MAX_PATH, 0);
	//res = CM_Query_And_Remove_SubTreeW(dev_inst_parent, &veto_type, veto_name, MAX_PATH, CM_REMOVE_UI_OK);
	if (res == CR_SUCCESS && veto_type == PNP_VetoTypeUnknown) {
		return 0;
	}

	BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Request to eject device has failed. Veto type: %2%", path, veto_type);
	return 1;
}

// this method should be called in worker thread. It does SLEEP.
// Alternative ejecting to eject_inner method.
// Success or Fail is passed directly to UI by events
void eject_alt(std::string path, wxEvtHandler* callback_evt_handler, DriveData drive_data)
{
	// Transform path to correct form
	std::wstring wpath = std::wstring();
	wpath += boost::nowide::widen(path)[0]; // drive letter wide
	wpath[0] &= ~0x20; // make sure drive letter is uppercase
	std::wstring volume_access_path = L"\\\\.\\" + wpath + L":"; // for CreateFile			

	HANDLE handle = CreateFileW(volume_access_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		BOOST_LOG_TRIVIAL(error) << "Alt ejecting " << volume_access_path << " failed (handle == INVALID_HANDLE_VALUE): " << GetLastError();
		assert(callback_evt_handler);
		if (callback_evt_handler)
			wxPostEvent(callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(std::move(drive_data), false)));
		return;
	}
	DWORD deviceControlRetVal(0);
	//these 3 commands should eject device safely but they dont, the device does disappear from file explorer but the "device was safely remove" notification doesnt trigger.
	//sd cards does  trigger WM_DEVICECHANGE messege, usb drives dont
	BOOL e1;
	for (int i = 0; i < 10; i++) {
		e1 = DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		if (e1)
			break;
		BOOST_LOG_TRIVIAL(warning) << "Alt Ejecting: FSCTL_LOCK_VOLUME failed. Try " << i << ". " << GetLastError();
		Sleep(500);
	}
	if (e1 == 0) {
		CloseHandle(handle);
		BOOST_LOG_TRIVIAL(error) << "Alt Ejecting " << volume_access_path << " failed to Lock the device. Ejecting has failed. " << GetLastError();
		assert(callback_evt_handler);
		if (callback_evt_handler)
			wxPostEvent(callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(std::move(drive_data), false)));
		return;
	}
	BOOST_LOG_TRIVIAL(info) << "Alt Ejecting: FSCTL_LOCK_VOLUME success.";

	BOOL e2 = DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
	if (e2 == 0) {
		CloseHandle(handle);
		BOOST_LOG_TRIVIAL(error) << "Alt Ejecting " << volume_access_path << " failed to dismount the volume. Ejecting has failed. " << GetLastError();
		assert(callback_evt_handler);
		if (callback_evt_handler)
			wxPostEvent(callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(std::move(drive_data), false)));
		return;
	}
	BOOST_LOG_TRIVIAL(info) << "Alt Ejecting: FSCTL_DISMOUNT_VOLUME success.";

	// some implemenatations also calls IOCTL_STORAGE_MEDIA_REMOVAL here with FALSE as third parameter, which should set PreventMediaRemoval 
	BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
	if (error == 0) {
		CloseHandle(handle);
		BOOST_LOG_TRIVIAL(error) << "Alt Ejecting " << volume_access_path << " failed (IOCTL_STORAGE_EJECT_MEDIA)" << deviceControlRetVal << " " << GetLastError();
		assert(callback_evt_handler);
		if (callback_evt_handler)
			wxPostEvent(callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(std::move(drive_data), false)));

		return;
	}
	CloseHandle(handle);

	BOOST_LOG_TRIVIAL(info) << "Alt Ejecting finished";
	assert(callback_evt_handler);
	if (callback_evt_handler)
		wxPostEvent(callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair< DriveData, bool >(std::move(drive_data), true)));
}
#endif // 0

// C++ equivavalent of PowerShell script:
// $driveEject = New - Object - comObject Shell.Application
// $driveEject.Namespace(17).ParseName("E:").InvokeVerb("Eject")
// from https://superuser.com/a/1750403
bool eject_inner(const std::string& path)
{
	std::wstring wpath = boost::nowide::widen(path);
	CoInitialize(nullptr);
	CComPtr<IShellDispatch> pShellDisp;
	HRESULT hr = pShellDisp.CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER);
	if (!SUCCEEDED(hr)) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Attempt to get Shell pointer has failed.", path);
		CoUninitialize();
		return false;
	}
	CComPtr<Folder> pFolder;
	VARIANT vtDrives;
	VariantInit(&vtDrives);
	vtDrives.vt = VT_I4;
	vtDrives.lVal = ssfDRIVES;
	hr = pShellDisp->NameSpace(vtDrives, &pFolder);
	if (!SUCCEEDED(hr)) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Attempt to create Namespace has failed.", path);
		CoUninitialize();
		return false;
	}
	CComPtr<FolderItem> pItem;
	hr = pFolder->ParseName(static_cast<BSTR>(const_cast<wchar_t*>(wpath.c_str())), &pItem);
	if (!SUCCEEDED(hr)) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Attempt to Parse name has failed.", path);
		CoUninitialize();
		return false;
	}
	VARIANT vtEject;
	VariantInit(&vtEject);
	vtEject.vt = VT_BSTR;
	vtEject.bstrVal = SysAllocString(L"Eject");
	hr = pItem->InvokeVerb(vtEject);
	if (!SUCCEEDED(hr)) {
		BOOST_LOG_TRIVIAL(error) << GUI::format("Ejecting of %1% has failed: Attempt to Invoke Verb has failed.", path);
		VariantClear(&vtEject);
		CoUninitialize();
		return false;
	}
	BOOST_LOG_TRIVIAL(debug) << "Ejecting via InvokeVerb has succeeded.";
	VariantClear(&vtEject);
	CoUninitialize();
	return true;
}

} // namespace
// Called from UI therefore it blocks the UI thread.
// It also blocks updates at the worker thread.
// Win32 implementation.
void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	BOOST_LOG_TRIVIAL(info) << "Ejecting started"; 
	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	auto it_drive_data = this->find_last_save_path_drive_data();
	if (it_drive_data != m_current_drives.end()) {
		if (eject_inner(m_last_save_path)) {
		// success
			BOOST_LOG_TRIVIAL(info) << "Ejecting has succeeded.";
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair< DriveData, bool >(std::move(*it_drive_data), true)));
		} else {
			// failed to eject
			BOOST_LOG_TRIVIAL(error) << "Ejecting has failed.";
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
		}
	} else {
		// drive not found in m_current_drives
		BOOST_LOG_TRIVIAL(error) << "Ejecting has failed. Drive not found in m_current_drives.";
		assert(m_callback_evt_handler);
		if (m_callback_evt_handler)
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>({"",""}, false)));
	}
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	if (m_current_drives.empty())
		return std::string();
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());
	for (const DriveData &drive_data : m_current_drives) {
		char drive = drive_data.path[0];
		if (drive == 'A' + letter)
			return path;	
	}
	return m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());	
	for (const DriveData &drive_data : m_current_drives) {
		assert(! drive_data.path.empty());
		if (drive_data.path.front() == 'A' + letter)
			return drive_data.path;
	}
	return std::string();
}

// Called by Win32 Volume arrived / detached callback.
void RemovableDriveManager::volumes_changed()
{
	if (m_initialized) {
		// Signal the worker thread to wake up and enumerate removable drives.
	    m_wakeup = true;
		m_thread_stop_condition.notify_all();
	}
}

#else

namespace search_for_drives_internal 
{
	static bool compare_filesystem_id(const std::string &path_a, const std::string &path_b)
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

		if (
#ifdef __linux__
			// Chromium mounts removable drives in a way that produces the same device ID.
			platform_flavor() == PlatformFlavor::LinuxOnChromium ||
#endif
			// If not same file system - could be removable drive.
			! compare_filesystem_id(path, parent_path)) {
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

#if ! __APPLE__
	static void search_path(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
	{
	    glob_t globbuf;
		globbuf.gl_offs = 2;
		int error = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
		if (error == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc; ++ i)
				inspect_file(globbuf.gl_pathv[i], parent_path, out);
		} else {
			//if error - path probably doesnt exists so function just exits
			//std::cout<<"glob error "<< error<< "\n";
		}
		globfree(&globbuf);
	}
#endif // ! __APPLE__
}

std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	std::vector<DriveData> current_drives;

#if __APPLE__

	this->list_devices(current_drives);

#else

   	if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
	    // ChromeOS specific: search /mnt/chromeos/removable/* folder
		search_for_drives_internal::search_path("/mnt/chromeos/removable/*", "/mnt/chromeos/removable", current_drives);
   	} else {
	    //search /media/* folder
		search_for_drives_internal::search_path("/media/*", "/media", current_drives);

		//search_path("/Volumes/*", "/Volumes");
		std::string path = wxGetUserId().ToUTF8().data();
		std::string pp(path);

		//search /media/USERNAME/* folder
		pp = "/media/"+pp;
		path = "/media/" + path + "/*";
		search_for_drives_internal::search_path(path, pp, current_drives);

		//search /run/media/USERNAME/* folder
		path = "/run" + path;
		pp = "/run"+pp;
		search_for_drives_internal::search_path(path, pp, current_drives);
	}

#endif

	return current_drives;
}

// Called from UI therefore it blocks the UI thread.
// It also blocks updates at the worker thread.
// Unix & OSX implementation.
void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
#if __APPLE__
	// If eject is still pending on the eject thread, wait until it finishes.
	//FIXME while waiting for the eject thread to finish, the main thread is not pumping Cocoa messages, which may lead 
	// to blocking by the diskutil tool for a couple (up to 10) seconds. This is likely not critical, as the eject normally
	// finishes quickly.
	this->eject_thread_finish();
#endif

	BOOST_LOG_TRIVIAL(info) << "Ejecting started";

	DriveData drive_data;
	{
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		auto it_drive_data = this->find_last_save_path_drive_data();
		if (it_drive_data == m_current_drives.end())
			return;
		drive_data = *it_drive_data;
	}

	std::string correct_path(m_last_save_path);
#if __APPLE__
	// On Apple, run the eject asynchronously on a worker thread, see the discussion at GH issue #4844.
	m_eject_thread = new boost::thread([this, correct_path, drive_data]()
#endif
	{
		//std::cout<<"Ejecting "<<(*it).name<<" from "<< correct_path<<"\n";
		// there is no usable command in c++ so terminal command is used instead
		// but neither triggers "succesful safe removal messege"
		
		BOOST_LOG_TRIVIAL(info) << "Ejecting started";
		boost::process::ipstream istd_err;
    	boost::process::child child(
#if __APPLE__		
			boost::process::search_path("diskutil"), "eject", correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
		//Another option how to eject at mac. Currently not working.
		//used insted of system() command;
		//this->eject_device(correct_path);
#else
    		boost::process::search_path("umount"), correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
#endif
		std::string line;
		while (child.running() && std::getline(istd_err, line)) {
			BOOST_LOG_TRIVIAL(trace) << line;
		}
		// wait for command to finnish (blocks ui thread)
		std::error_code ec;
		child.wait(ec);
		bool success = false;
		if (ec) {
            // The wait call can fail, as it did in https://github.com/prusa3d/PrusaSlicer/issues/5507
            // It can happen even in cases where the eject is sucessful, but better report it as failed.
            // We did not find a way to reliably retrieve the exit code of the process.
			BOOST_LOG_TRIVIAL(error) << "boost::process::child::wait() failed during Ejection. State of Ejection is unknown. Error code: " << ec.value();
		} else {
			int err = child.exit_code();
	    	if (err) {
	    		BOOST_LOG_TRIVIAL(error) << "Ejecting failed. Exit code: " << err;
	    	} else {
				BOOST_LOG_TRIVIAL(info) << "Ejecting finished";
				success = true;
			}
		}
		assert(m_callback_evt_handler);
		if (m_callback_evt_handler) 
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(drive_data, success)));
		if (success) {
			// Remove the drive_data from m_current drives, searching by value, not by pointer, as m_current_drives may get modified during
			// asynchronous execution on m_eject_thread.
			std::scoped_lock<std::mutex> lock(m_drives_mutex);
			auto it = std::find(m_current_drives.begin(), m_current_drives.end(), drive_data);
			if (it != m_current_drives.end())
				m_current_drives.erase(it);
		}
	}
#if __APPLE__
	);
#endif // __APPLE__
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	std::size_t found = path.find_last_of("/");
	std::string new_path = found == path.size() - 1 ? path.substr(0, found) : path;

	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	for (const DriveData &data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, data.path))
			return path;
	return m_current_drives.empty() ? std::string() : m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	std::size_t found = path.find_last_of("/");
	std::string new_path = found == path.size() - 1 ? path.substr(0, found) : path;
    // trim the filename
    found = new_path.find_last_of("/");
    new_path = new_path.substr(0, found);
    
	// check if same filesystem
	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	for (const DriveData &drive_data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, drive_data.path))
			return drive_data.path;
	return std::string();
}
#endif

void RemovableDriveManager::init(wxEvtHandler *callback_evt_handler)
{
	assert(! m_initialized);
	assert(m_callback_evt_handler == nullptr);

	if (m_initialized)
		return;

	m_initialized = true;
	m_callback_evt_handler = callback_evt_handler;

#if __APPLE__
    this->register_window_osx();
#endif

#ifdef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#else // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	// Don't call update() manually, as the UI triggered APIs call this->update() anyways.
	m_thread = boost::thread((boost::bind(&RemovableDriveManager::thread_proc, this)));
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
}

void RemovableDriveManager::shutdown()
{
#if __APPLE__
	// If eject is still pending on the eject thread, wait until it finishes.
	//FIXME while waiting for the eject thread to finish, the main thread is not pumping Cocoa messages, which may lead 
	// to blocking by the diskutil tool for a couple (up to 10) seconds. This is likely not critical, as the eject normally
	// finishes quickly.
	this->eject_thread_finish();
#endif

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
    if (m_thread.joinable()) {
    	// Stop the worker thread, if running.
		{
			// Notify the worker thread to cancel wait on detection polling.
			std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
			m_stop = true;
		}
		m_thread_stop_condition.notify_all();
		// Wait for the worker thread to stop.
		m_thread.join();
		m_stop = false;
	}
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	m_initialized = false;
	m_callback_evt_handler = nullptr;
}

bool RemovableDriveManager::set_and_verify_last_save_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	m_last_save_path = this->get_removable_drive_from_path(path);
	m_exporting_finished = false;
	return ! m_last_save_path.empty();
}

RemovableDriveManager::RemovableDrivesStatus RemovableDriveManager::status()
{

	RemovableDriveManager::RemovableDrivesStatus out;
	{
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		out.has_eject = 
			// Cannot control eject on Chromium.
			platform_flavor() != PlatformFlavor::LinuxOnChromium &&
			this->find_last_save_path_drive_data() != m_current_drives.end();
		out.has_removable_drives = ! m_current_drives.empty();
	}
	if (! out.has_eject) 
		m_last_save_path.clear();
	out.has_eject = out.has_eject && m_exporting_finished;
	return out;
}

// Update is called from thread_proc() and from most of the public methods on demand.
void RemovableDriveManager::update()
{
	std::unique_lock<std::mutex> inside_update_lock(m_inside_update_mutex, std::defer_lock);
#ifdef _WIN32
	// All wake up calls up to now are now consumed when the drive enumeration starts.
	m_wakeup = false;
#endif // _WIN32
	if (inside_update_lock.try_lock()) {
		// Got the lock without waiting. That means, the update was not running.
		// Run the update.
		std::vector<DriveData> current_drives = this->search_for_removable_drives();
		// Post update events.
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		std::sort(current_drives.begin(), current_drives.end());
		if (current_drives != m_current_drives) {
			// event for writing / ejecting functions
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDrivesChangedEvent(EVT_REMOVABLE_DRIVES_CHANGED));
			
			// event for printer config file
			std::vector<DriveData> new_drives;
			std::set_difference(current_drives.begin(), current_drives.end(), m_current_drives.begin(), m_current_drives.end(),
				std::inserter(new_drives, new_drives.begin()));
			
			for (const DriveData& data : new_drives) { 
				if (data.path.empty())
					continue;
				wxCommandEvent* evt = new wxCommandEvent(EVT_REMOVABLE_DRIVE_ADDED);
				evt->SetString(boost::nowide::widen(data.path));
				evt->SetInt((int)m_first_update);
				m_callback_evt_handler->QueueEvent(evt);
			}
			
		}
		m_current_drives = std::move(current_drives);
		m_first_update = false;
	} else {
		// Acquiring the m_iniside_update lock failed, therefore another update is running.
		// Just block until the other instance of update() finishes.
		inside_update_lock.lock();
	}
}

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
void RemovableDriveManager::thread_proc()
{
	// Signal the worker thread to update initially.
#ifdef _WIN32
    m_wakeup = true;
#endif // _WIN32

	for (;;) {
		// Wait for 2 seconds before running the disk enumeration.
		// Cancellable.
		{
			std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
#ifdef _WIN32
			// Reacting to updates by WM_DEVICECHANGE and WM_USER_MEDIACHANGED
			m_thread_stop_condition.wait(lck, [this]{ return m_stop || m_wakeup; });
#else
			m_thread_stop_condition.wait_for(lck, std::chrono::seconds(2), [this]{ return m_stop; });
#endif
		}
		if (m_stop)
			// Stop the worker thread.
			break;
		// Update m_current drives and send out update events.
		this->update();
	}
}
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

std::vector<DriveData>::const_iterator RemovableDriveManager::find_last_save_path_drive_data() const
{
	return Slic3r::binary_find_by_predicate(m_current_drives.begin(), m_current_drives.end(),
		[this](const DriveData &data){ return data.path < m_last_save_path; }, 
		[this](const DriveData &data){ return data.path == m_last_save_path; });
}

#if __APPLE__
void RemovableDriveManager::eject_thread_finish()
{
	if (m_eject_thread) {
		m_eject_thread->join();
		delete m_eject_thread;
		m_eject_thread = nullptr;
	}
}
#endif // __APPLE__

std::vector<DriveData> RemovableDriveManager::get_drive_list()
{
	{
		std::lock_guard<std::mutex> guard(m_drives_mutex);
		return m_current_drives;
	}
}
}} // namespace Slic3r::GUI
