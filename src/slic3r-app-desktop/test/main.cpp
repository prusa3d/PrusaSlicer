#include <catch2/catch_session.hpp>

#include "Slic3r/Log.hpp"

int main(int argc, char* argv[])
{
    Slic3r::init_logging();
    Slic3r::set_log_level(Slic3r::LogLevel::Critical);
    return Catch::Session().run(argc, argv);
}
