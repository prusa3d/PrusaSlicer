#include "Slic3r/App/Desktop/Mouse3D/Mouse3DBackend.hpp"

#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"
#include "Slic3r/Log.hpp"

#include <cstdint>
#include <dlfcn.h>
#include <fmt/format.h>
#include <stdlib.h>
#include <string>

namespace Slic3r::App::Desktop {

namespace {

// The subset of the 3DconnexionClient framework API used by the application.
constexpr std::uint16_t ConnexionClientModeTakeOver = 1;
constexpr std::uint32_t ConnexionMaskAxis           = 0x3f00;
constexpr std::uint16_t ConnexionCmdHandleAxis      = 3;
constexpr std::uint32_t ConnexionMsgDeviceState     = '3dSR';
constexpr std::uint32_t ConnexionCtlGetDeviceID     = '3did';

#pragma pack(push, 2)

struct ConnexionDeviceState
{
    std::uint16_t version;
    std::uint16_t client;
    std::uint16_t command;
    std::int16_t param;
    std::int32_t value;
    std::uint64_t time;
    std::uint8_t report[8];
    std::uint16_t buttons8;
    std::int16_t axis[6];
    std::uint16_t address;
    std::uint32_t buttons;
};

#pragma pack(pop)

using AddedHandler   = void (*)(std::uint32_t);
using RemovedHandler = void (*)(std::uint32_t);
using MessageHandler = void (*)(std::uint32_t, std::uint32_t, void*);

using SetConnexionHandlersFunction =
    std::int16_t (*)(MessageHandler, AddedHandler, RemovedHandler, bool);
using InstallConnexionHandlersFunction =
    std::int16_t (*)(MessageHandler, AddedHandler, RemovedHandler);
using CleanupConnexionHandlersFunction = void (*)();
using RegisterConnexionClientFunction =
    std::uint16_t (*)(std::uint32_t, const char*, std::uint16_t, std::uint32_t);
using UnregisterConnexionClientFunction = void (*)(std::uint16_t);
using ConnexionClientControlFunction =
    std::int16_t (*)(std::uint16_t, std::uint32_t, std::int32_t, std::int32_t*);

// The driver callbacks carry no user data.
Mouse3D::Mouse3DController* s_mouse_3d_controller         = nullptr;
std::uint16_t s_client_id                                 = 0;
ConnexionClientControlFunction s_connexion_client_control = nullptr;

template <typename Function>
Function load_function(void* library, const char* function_name)
{
    const Function function = reinterpret_cast<Function>(dlsym(library, function_name));
    if (function == nullptr) {
        SPDLOG_ERROR(
            "Loading of the 3Dconnexion driver function {} failed: {}",
            function_name,
            dlerror()
        );
    }
    return function;
}

void on_device_added(std::uint32_t)
{
    std::int32_t device_id = 0;
    s_connexion_client_control(s_client_id, ConnexionCtlGetDeviceID, 0, &device_id);
    const std::uint32_t unsigned_device_id = static_cast<std::uint32_t>(device_id);
    s_mouse_3d_controller->device_connected(
        fmt::format("VID{:04X},PID{:04X}", unsigned_device_id >> 16, unsigned_device_id & 0xffff)
    );
}

void on_device_removed(std::uint32_t)
{
    s_mouse_3d_controller->device_disconnected();
}

void on_device_message(std::uint32_t, std::uint32_t message_type, void* message_argument)
{
    if (message_type != ConnexionMsgDeviceState) {
        return;
    }

    const ConnexionDeviceState* device_state =
        static_cast<const ConnexionDeviceState*>(message_argument);
    if (device_state->client == s_client_id && device_state->command == ConnexionCmdHandleAxis) {
        // The driver scales the axes by the speed set in its preferences.
        const std::int16_t* axis = device_state->axis;
        s_mouse_3d_controller->handle_axis_input(
            Domain::Vec3d(axis[0], axis[1], axis[2]),
            Domain::Vec3d(axis[3], axis[4], axis[5])
        );
    }
}

class Mouse3DBackendMac final : public IMouse3DBackend
{
public:
    explicit Mouse3DBackendMac(Mouse3D::Mouse3DController& mouse_3d_controller)
    {
        m_library = dlopen(
            "/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient",
            RTLD_LAZY | RTLD_LOCAL
        );
        if (m_library == nullptr) {
            SPDLOG_INFO("3Dconnexion driver is not available: {}", dlerror());
            return;
        }

        // Drivers since 10.2.2 provide SetConnexionHandlers, older ones InstallConnexionHandlers.
        const SetConnexionHandlersFunction set_connexion_handlers = reinterpret_cast<
            SetConnexionHandlersFunction>(dlsym(m_library, "SetConnexionHandlers"));
        const InstallConnexionHandlersFunction install_connexion_handlers =
            set_connexion_handlers != nullptr ?
            nullptr :
            load_function<InstallConnexionHandlersFunction>(m_library, "InstallConnexionHandlers");
        const RegisterConnexionClientFunction register_connexion_client =
            load_function<RegisterConnexionClientFunction>(m_library, "RegisterConnexionClient");
        m_cleanup_connexion_handlers =
            load_function<CleanupConnexionHandlersFunction>(m_library, "CleanupConnexionHandlers");
        m_unregister_connexion_client = load_function<
            UnregisterConnexionClientFunction>(m_library, "UnregisterConnexionClient");
        s_connexion_client_control =
            load_function<ConnexionClientControlFunction>(m_library, "ConnexionClientControl");
        if ((set_connexion_handlers == nullptr && install_connexion_handlers == nullptr)
            || register_connexion_client == nullptr
            || m_cleanup_connexion_handlers == nullptr
            || m_unregister_connexion_client == nullptr
            || s_connexion_client_control == nullptr)
        {
            close_library();
            return;
        }

        s_mouse_3d_controller = &mouse_3d_controller;
        // The handlers are called on the main thread.
        const std::int16_t error = set_connexion_handlers != nullptr ?
            set_connexion_handlers(on_device_message, on_device_added, on_device_removed, false) :
            install_connexion_handlers(on_device_message, on_device_added, on_device_removed);
        if (error != 0) {
            SPDLOG_ERROR(
                "Installing of the 3Dconnexion driver handlers failed with error {}",
                error
            );
            close_library();
            return;
        }

        // The driver identifies the client by the executable name passed as a Pascal string.
        std::string client_name = getprogname();
        client_name.insert(client_name.begin(), static_cast<char>(client_name.size()));
        s_client_id = register_connexion_client(
            0,
            client_name.c_str(),
            ConnexionClientModeTakeOver,
            ConnexionMaskAxis
        );
        SPDLOG_INFO("3Dconnexion driver client registered");
    }

    ~Mouse3DBackendMac() override
    {
        if (m_library == nullptr) {
            return;
        }

        m_unregister_connexion_client(s_client_id);
        m_cleanup_connexion_handlers();
        close_library();
    }

private:
    void close_library()
    {
        dlclose(m_library);
        m_library                  = nullptr;
        s_mouse_3d_controller      = nullptr;
        s_client_id                = 0;
        s_connexion_client_control = nullptr;
    }

    void* m_library{nullptr};
    CleanupConnexionHandlersFunction m_cleanup_connexion_handlers{nullptr};
    UnregisterConnexionClientFunction m_unregister_connexion_client{nullptr};
};

} // namespace

namespace Mouse3DBackendFactory {

std::unique_ptr<IMouse3DBackend> create_mouse_3d_backend(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher&
)
{
    return std::make_unique<Mouse3DBackendMac>(mouse_3d_controller);
}

} // namespace Mouse3DBackendFactory

} // namespace Slic3r::App::Desktop
