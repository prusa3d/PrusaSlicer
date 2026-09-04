#include "AppInstanceMessageHandlerWin32.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Version.hpp"

#include "fmt/format.h"
#include <boost/nowide/convert.hpp>

#include <windows.h>

namespace Slic3r::Biz::AppInstance {
namespace {
void init_windows_properties(HWND window_handle, size_t instance_hash, bool maximized)
{
    size_t minor_hash          = instance_hash & 0xFFFFFFFF;
    size_t major_hash          = (instance_hash & 0xFFFFFFFF00000000) >> 32;
    size_t is_maximized        = maximized ? 1 : 0;
    HANDLE handle_minor        = UIntToPtr(minor_hash);
    HANDLE handle_major        = UIntToPtr(major_hash);
    HANDLE handle_is_maximized = UIntToPtr(is_maximized);
    SetProp(window_handle, L"Instance_Hash_Minor", handle_minor);
    SetProp(window_handle, L"Instance_Hash_Major", handle_major);
    SetProp(window_handle, L"Instance_Is_Maximized", handle_is_maximized);
}

void update_windows_properties(HWND window_handle, bool maximized)
{
    RemoveProp(window_handle, L"Instance_Is_Maximized");
    size_t is_maximized        = maximized ? 1 : 0;
    HANDLE handle_is_maximized = UIntToPtr(is_maximized);
    SetProp(window_handle, L"Instance_Is_Maximized", handle_is_maximized);
}

std::string compose_message_json(const std::string& type, const std::string& data)
{
    return fmt::format("{{ \"type\" : \"{}\" , \"data\" : \"{}\"}}", type, data);
}

} // namespace

AppInstanceMessageHandlerWin32::AppInstanceMessageHandlerWin32(
    Platform::IMainThreadDispatcher& dispatcher
) :
    AbstractAppInstanceMessageHandler(dispatcher)
{}

AppInstanceMessageHandlerWin32::~AppInstanceMessageHandlerWin32()
{
    if (m_init) {
        ASSERT(m_window_handle != nullptr);
        RemoveProp(m_window_handle, L"Instance_Hash_Minor");
        RemoveProp(m_window_handle, L"Instance_Hash_Major");
        RemoveProp(m_window_handle, L"Instance_Is_Maximized");
    }
}

void AppInstanceMessageHandlerWin32::init(void* window_handle)
{
    ASSERT(window_handle != nullptr);
    m_window_handle = static_cast<HWND>(window_handle);
    init_windows_properties(
        m_window_handle,
        Biz::Platform::PlatformServices::instance().app_hash(),
        false
    );
    m_init = true;
}

BOOL AppInstanceMessageSenderWin32::enum_windows_process_multicast(_In_ HWND hwnd)
{
    // The multicast might be done before the window of this app is created. Then we have no handle.
    if (m_window_handle != nullptr && hwnd == m_window_handle) {
        return TRUE; // Skip the initiator window and continue enumeration.
    }

    TCHAR wndText[1'000];
    TCHAR className[1'000];
    if (GetClassName(hwnd, className, 1'000) == 0 || GetWindowText(hwnd, wndText, 1'000) == 0) {
        return TRUE; // Skip and continue enumeration.
    }

    std::wstring classNameString(className);
    std::wstring wndTextString(wndText);

    if (wndTextString.find(boost::nowide::widen(Slic3r::BUILD_ID).c_str()) != std::wstring::npos && classNameString == L"wxWindowNR")
    {
        // Get the instance hash properties.
        uint64_t other_instance_hash_minor = PtrToUint(GetProp(hwnd, L"Instance_Hash_Minor"));
        uint64_t other_instance_hash_major = PtrToUint(GetProp(hwnd, L"Instance_Hash_Major"));
        uint64_t other_instance_hash = (other_instance_hash_major << 32) | other_instance_hash_minor;

        // Get the maximization state.
        bool maximized = PtrToUint(GetProp(hwnd, L"Instance_Is_Maximized")) == 1;

        ASSERT(m_instance_hash != 0);
        if (m_instance_hash == other_instance_hash) {
            // Send the multicast message.
            ASSERT(!m_multicast_message.empty());
            COPYDATASTRUCT data_to_send = {
                1,
                static_cast<DWORD>(sizeof(wchar_t) * (m_multicast_message.size() + 1)),
                const_cast<wchar_t*>(m_multicast_message.c_str())
            };
            SendMessage(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data_to_send));
            return TRUE; // Continue enumeration.
        }
    }
    return TRUE; // Continue enumeration.
}

static BOOL CALLBACK enum_windows_process_multicast_callback(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    AppInstanceMessageSenderWin32* self = reinterpret_cast<AppInstanceMessageSenderWin32*>(lParam);
    return self->enum_windows_process_multicast(hwnd);
}

void AppInstanceMessageSenderWin32::multicast_message(
    const std::string& message_type,
    const std::string& message_data,
    size_t instance_hash,
    void* window_handle
)
{
    std::string message_json = compose_message_json(message_type, message_data);
    m_multicast_message      = boost::nowide::widen(message_json);
    m_instance_hash          = instance_hash;
    m_window_handle          = static_cast<HWND>(window_handle);
    EnumWindows(enum_windows_process_multicast_callback, reinterpret_cast<LPARAM>(this));
}

BOOL AppInstanceMessageSenderWin32::enum_windows_process_broadcast(_In_ HWND hwnd)
{
    // The broadcast might be done before the window of this app is created. Then we have no handle.
    if (m_window_handle != nullptr && hwnd == m_window_handle) {
        return TRUE; // Skip the initiator window and continue enumeration.
    }

    TCHAR wndText[1'000];
    TCHAR className[1'000];
    if (GetClassName(hwnd, className, 1'000) == 0 || GetWindowText(hwnd, wndText, 1'000) == 0) {
        return TRUE; // Skip and continue enumeration.
    }

    std::wstring classNameString(className);
    std::wstring wndTextString(wndText);

    if (wndTextString.find(boost::nowide::widen(Slic3r::BUILD_ID).c_str()) != std::wstring::npos && classNameString == L"wxWindowNR")
    {
        // Get the instance hash properties.
        uint64_t other_instance_hash_minor = PtrToUint(GetProp(hwnd, L"Instance_Hash_Minor"));
        uint64_t other_instance_hash_major = PtrToUint(GetProp(hwnd, L"Instance_Hash_Major"));
        uint64_t other_instance_hash = (other_instance_hash_major << 32) | other_instance_hash_minor;

        // Get the maximization state.
        bool maximized = PtrToUint(GetProp(hwnd, L"Instance_Is_Maximized")) == 1;

        ASSERT(m_instance_hash != 0);
        if (m_instance_hash == other_instance_hash) {
            // Send the multicast message.
            ASSERT(!m_multicast_message.empty());
            COPYDATASTRUCT data_to_send = {
                1,
                static_cast<DWORD>(sizeof(wchar_t) * (m_multicast_message.size() + 1)),
                const_cast<wchar_t*>(m_multicast_message.c_str())
            };
            SendMessage(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data_to_send));
            return FALSE; // Stop enumeration.
        }
    }
    return TRUE; // Continue enumeration.
}

static BOOL CALLBACK enum_windows_process_broadcast_callback(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    AppInstanceMessageSenderWin32* self = reinterpret_cast<AppInstanceMessageSenderWin32*>(lParam);
    return self->enum_windows_process_broadcast(hwnd);
}

void AppInstanceMessageSenderWin32::broadcast_message(
    const std::string& message_type,
    const std::string& message_data,
    size_t instance_hash,
    void* window_handle
)
{
    std::string message_json = compose_message_json(message_type, message_data);
    m_multicast_message      = boost::nowide::widen(message_json);
    m_instance_hash          = instance_hash;
    m_window_handle          = static_cast<HWND>(window_handle);
    EnumWindows(enum_windows_process_broadcast_callback, reinterpret_cast<LPARAM>(this));
}

void AppInstanceMessageHandlerWin32::multicast_message(
    const std::string& message_type,
    const std::string& message_data
)
{
    AppInstanceMessageSenderWin32 sender;
    sender.multicast_message(message_type, message_data, Platform::PlatformServices::instance().app_hash(), m_window_handle);
}
} // namespace Slic3r::Biz::AppInstance
