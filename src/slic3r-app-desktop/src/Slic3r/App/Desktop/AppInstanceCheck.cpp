#include "AppInstanceCheck.hpp"

#include "SingleInstanceCheckerFactory.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"
#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <string>
#include <boost/filesystem.hpp>

#ifdef _WIN32
#include <windows.h>
#include <boost/nowide/convert.hpp>
#else
#include <boost/dll/runtime_symbol_info.hpp>
#endif

namespace Slic3r::App::Desktop::AppInstance {

namespace {
std::string get_init_params_in_string(int argc, char** argv)
{
    std::string result;
    for (int i = 1; i < argc; ++i) {
        const std::string token = argv[i];
        // We do now want escape_strings_cstyle that quotes strings
        // It would not be possible to use inside json
        result += Biz::Algorithms::escape_string_cstyle(token);
        result += ";";
    }
    return result;
}

#ifdef _WIN32
// Normalizes case/short-path variance that argv[0] canonicalization misses. "" on failure.
std::string get_canonical_executable_path_win32()
{
    wchar_t module_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return {};

    HANDLE file = CreateFileW(
        module_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE)
        return {};

    wchar_t final_path[32768];
    DWORD count = GetFinalPathNameByHandleW(file, final_path, 32768, FILE_NAME_NORMALIZED);
    CloseHandle(file);
    if (count == 0 || count >= 32768)
        return {};
    return boost::nowide::narrow(final_path, count);
}
#else
// Launch independent executable path, canonicalized so macOS agrees with Linux. "" on failure.
std::string get_canonical_executable_path_posix()
{
    boost::dll::fs::error_code ec;
    const boost::filesystem::path location = boost::dll::program_location(ec);
    if (ec)
        return {};

    const boost::filesystem::path canonical = boost::filesystem::canonical(location, ec);
    return ec ? location.string() : canonical.string();
}
#endif // _WIN32
} // namespace

bool instance_check(const Slic3r::App::InitParams& init_params, bool app_config_single_instance)
{
    std::string program_path;
#ifdef _WIN32
    program_path = get_canonical_executable_path_win32();
#else
    program_path = get_canonical_executable_path_posix();
#endif
    if (program_path.empty()) {
        program_path = boost::filesystem::absolute(
                           boost::filesystem::weakly_canonical(init_params.argv[0])
        )
                           .string();
    }
    size_t hashed_path    = std::hash<std::string>{}(program_path);
    std::string lock_name = std::to_string(hashed_path);
    Biz::Platform::PlatformServices::instance().set_app_hash(hashed_path);
    SPDLOG_INFO("App instance hash {} from executable {}", hashed_path, program_path);

    // Parameters from init params override app config value
    bool should_send_and_exit = app_config_single_instance;
    if (init_params.misc.single_instance.has_value()) {
        should_send_and_exit = *init_params.misc.single_instance;
    }

    // The path in second parameter should change, once the new data dir structure is set.
    std::unique_ptr<Biz::Platform::ISingleInstanceChecker>
        single_instance_checker = SingleInstanceCheckerFactory::create_single_instance_checker(
            boost::filesystem::path(Slic3r::data_dir()) / "cache" / (lock_name + ".lock")
        );
    ASSERT(single_instance_checker != nullptr);

    bool is_another_running = single_instance_checker->is_another_running();
    if (is_another_running && should_send_and_exit) {
        std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageSender>
            sender = Biz::AppInstance::create_app_instance_message_sender();
        std::string forwarded_args = get_init_params_in_string(init_params.argc, init_params.argv);
        sender->broadcast_message("CLI", forwarded_args, hashed_path, nullptr);
        return true;
    }
    Biz::Platform::PlatformServices::instance().set_single_instance_checker(
        std::move(single_instance_checker)
    );

    return false;
}

} // namespace Slic3r::App::Desktop::AppInstance
