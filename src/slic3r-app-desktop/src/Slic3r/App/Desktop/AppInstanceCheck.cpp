#include "AppInstanceCheck.hpp"

#include "SingleInstanceCheckerFactory.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"
#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"
#include "Slic3r/Directories.hpp"

#include <string>
#include <boost/filesystem.hpp>

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
} // namespace

bool instance_check(const Slic3r::App::InitParams& init_params, bool app_config_single_instance)
{
    std::string program_path = boost::filesystem::absolute(
                                   boost::filesystem::weakly_canonical(init_params.argv[0])
    )
                                   .string();
    size_t hashed_path    = std::hash<std::string>{}(program_path);
    std::string lock_name = std::to_string(hashed_path);
    Biz::Platform::PlatformServices::instance().set_app_hash(hashed_path);

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
