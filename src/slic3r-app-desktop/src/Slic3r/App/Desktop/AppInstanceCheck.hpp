#pragma once

#include <Slic3r/App/Init.hpp>

#include <memory>

namespace Slic3r::App::Desktop::AppInstance {

    /**
     * @brief Searches for other running instances of the same app and sends them argv.
     * @return True if this instance should terminate. 
     */
    bool instance_check(const Slic3r::App::InitParams& init_params, bool app_config_single_instance);
}