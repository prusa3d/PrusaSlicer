#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "RemovableDriveMonitorWin32.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include <fmt/format.h>
#include <Windows.h>
#include <shobjidl.h>
#include <shldisp.h>
#include <atlbase.h>
#include <atlcom.h>

#include <string>

namespace Slic3r::Biz::RemovableDrive {

RemovableDriveService::RemovableDriveService(Platform::IMainThreadDispatcher& dispatcher) :
    m_monitor(std::make_unique<RemovableDriveMonitorWin32>(dispatcher)),
    m_dispatcher(dispatcher)
{}

namespace {
/**
 * @brief Expects COM to be initialized.
 */
bool invoke_eject_verb(const boost::filesystem::path& path)
{
    std::wstring wpath = path.wstring();
    CComPtr<IShellDispatch> pShellDisp;
    HRESULT hr = pShellDisp.CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to get Shell pointer has failed.", path.string());
        return false;
    }
    CComPtr<Folder> pFolder;
    VARIANT vtDrives;
    VariantInit(&vtDrives);
    vtDrives.vt   = VT_I4;
    vtDrives.lVal = ssfDRIVES;
    hr            = pShellDisp->NameSpace(vtDrives, &pFolder);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to create Namespace has failed.", path.string());
        return false;
    }
    CComPtr<FolderItem> pItem;
    hr = pFolder->ParseName(static_cast<BSTR>(const_cast<wchar_t*>(wpath.c_str())), &pItem);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to Parse name has failed.", path.string());
        return false;
    }
    VARIANT vtEject;
    VariantInit(&vtEject);
    vtEject.vt      = VT_BSTR;
    vtEject.bstrVal = SysAllocString(L"Eject");
    hr              = pItem->InvokeVerb(vtEject);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to Invoke Verb has failed.", path.string());
        VariantClear(&vtEject);
        return false;
    }

    VariantClear(&vtEject);
    return true;
}

bool is_volume_gone_error(DWORD error)
{
    return error == ERROR_NOT_READY || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
        || error == ERROR_INVALID_DRIVE || error == ERROR_NO_SUCH_DEVICE || error == ERROR_DEVICE_NOT_CONNECTED;
}

/**
 * @brief Success of the Eject verb does not mean the drive was ejected (e.g. a file is still open).
 * Must run while the COM apartment is still alive, the Shell may finish the eject asynchronously.
 */
bool wait_for_volume_removal(const boost::filesystem::path& path, JThread::StopToken stop_token)
{
    std::wstring wpath = path.wstring();
    for (int i = 0; i < 20; ++i) {
        if (!::GetVolumeInformationW(wpath.c_str(), nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0)
            && is_volume_gone_error(::GetLastError()))
        {
            return true;
        }
        if (stop_token.stop_requested()) {
            return false;
        }
        Sleep(500);
    }
    SPDLOG_ERROR("Ejecting of {} has failed: Volume is still present.", path.string());
    return false;
}

bool eject_inner(const boost::filesystem::path& path, JThread::StopToken stop_token)
{
    ASSERT(!path.empty());
    HRESULT hr = CoInitialize(nullptr);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Failed to initialize COM. Error code: {}", path.string(), hr);
        return false;
    }
    bool res = invoke_eject_verb(path) && wait_for_volume_removal(path, stop_token);
    CoUninitialize();
    return res;
}
} // namespace

void RemovableDriveService::eject_in_thread(const boost::filesystem::path& path)
{
    m_eject_thread = JThread::JThread(
        [this, path, previous = std::move(m_eject_thread)](JThread::StopToken stop_token) mutable
        {
            if (previous.joinable()) {
                previous.request_stop();
                previous.join();
            }
            if (stop_token.stop_requested()) {
                return;
            }
            bool res = eject_inner(path, stop_token);
            if (!res && !stop_token.stop_requested()) {
                dispatch_status_on_main_thread(path, RemovableDriveStatus::Failed);
            }
            // Do not dispatch Removed here, it will be confirmed by system callback.
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
