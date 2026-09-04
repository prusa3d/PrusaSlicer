#include "Slic3r/App/AppConfigProvider.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r::App {


boost::filesystem::path AppConfigProvider::download_dir() const
{
    boost::filesystem::path dest_dir{AppServices::instance().app_config().get<std::string>("downloads_directory")};
    // The app config value is currently not checked after user change in preferences dialog.
    // If not exist and cannot be created - use default.
    boost::system::error_code ec;
    if (boost::filesystem::exists(dest_dir, ec) && !ec && boost::filesystem::is_directory(dest_dir, ec) && !ec)
    {
        return dest_dir;
    }
    if (!boost::filesystem::create_directories(dest_dir, ec) || ec) 
    {
        boost::filesystem::path system_dir = system_downloads_dir();
        SPDLOG_ERROR("Failed to create directory for downloads ({}). Using default path instead: {}", dest_dir.string(), system_dir.string());
        return system_dir;
    }
    return dest_dir;
}

bool AppConfigProvider::get_show_step_import_parameters() const
{
    return AppServices::instance().app_config().get<bool>("show_step_import_parameters");
}

void AppConfigProvider::set_show_step_import_parameters(bool show)
{
    return AppServices::instance().app_config_interactor().set_item_value(
        "show_step_import_parameters",
        Domain::ConfigValue(show)
    );
}

double AppConfigProvider::get_step_linear_precision() const
{
    return AppServices::instance().app_config().get<double>("step_linear_precision");
}

void AppConfigProvider::set_step_linear_precision(double precision)
{
    AppServices::instance().app_config_interactor().set_item_value(
        "step_linear_precision",
        Domain::ConfigValue(precision)
    );
}

double AppConfigProvider::get_step_angle_precision() const
{
    return AppServices::instance().app_config().get<double>("step_angle_precision");
}

void AppConfigProvider::set_step_angle_precision(double precision)
{
    AppServices::instance().app_config_interactor().set_item_value(
        "step_angle_precision",
        Domain::ConfigValue(precision)
    );
}


} // Slic3r::App
