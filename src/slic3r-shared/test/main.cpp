#include <catch2/catch_session.hpp>
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Log.hpp"

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" {
    const char* __lsan_default_suppressions()
    {
        return "leak:TPrsStd_DriverTable::Get\n"; // OpenCASCADE singleton, not a real leak.
    }
}
#endif

using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::SecretStoreDummy;

int main( int argc, char* argv[] ) {
    Slic3r::init_logging();
    Slic3r::set_log_level(0);

    using Slic3r::Biz::Platform::PlatformServices;

    PlatformServices::instance().set_main_thread_dispatcher(
        std::make_unique<StdMainThreadDispatcher>()
    );

    std::unique_ptr<SecretStoreDummy> store_dummy{std::make_unique<SecretStoreDummy>()};
    PlatformServices::instance().set_secret_store(std::move(store_dummy));

    const int result = Catch::Session().run( argc, argv );

    return result;
}
