#pragma once

namespace Slic3r::App {
    class InitParams;
    class AppServices;
}

namespace Slic3r::App::Desktop {

int run(const InitParams& init_params, AppServices& app_services);

} // namespace Slic3r::App::Desktop

