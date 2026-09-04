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
bool eject_inner(const boost::filesystem::path& path)
{
    ASSERT(!path.empty());
    std::wstring wpath = path.wstring();
    HRESULT hr         = CoInitialize(nullptr);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Failed to initialize COM. Error code: {}", path.string(), hr);
        return false;
    }
    CComPtr<IShellDispatch> pShellDisp;
    hr = pShellDisp.CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to get Shell pointer has failed.", path.string());
        CoUninitialize();
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
        CoUninitialize();
        return false;
    }
    CComPtr<FolderItem> pItem;
    hr = pFolder->ParseName(static_cast<BSTR>(const_cast<wchar_t*>(wpath.c_str())), &pItem);
    if (!SUCCEEDED(hr)) {
        SPDLOG_ERROR("Ejecting of {} has failed: Attempt to Parse name has failed.", path.string());
        CoUninitialize();
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
        CoUninitialize();
        return false;
    }

    Sleep(2'000);

    VariantClear(&vtEject);
    CoUninitialize();
    return true;
}
} // namespace

void RemovableDriveService::eject_in_thread(const boost::filesystem::path& path)
{
    if (m_eject_thread.joinable()) {
        m_eject_thread.request_stop();
        m_eject_thread.join();
    }

    m_eject_thread = JThread::JThread(
        [this, path](JThread::StopToken stop_token)
        {
            bool res = eject_inner(path);
            if (!res) {
                dispatch_status_on_main_thread(path, RemovableDriveStatus::Failed);
            }
            // Do not dispatch Removed here, it will be confirmed by system callback.
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
