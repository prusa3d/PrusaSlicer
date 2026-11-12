#include "Slic3r/Biz/Platform/Termination.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Biz::Platform {
    void close() {
        SPDLOG_DEBUG("closing");
        PlatformServices::instance().main_thread_dispatcher().close();
    }
}
