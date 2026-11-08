#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Launcher/ReadCLI.hpp"

#include <string>
#include <vector>

#include <CLI/App.hpp>

using namespace Slic3r;

namespace {

App::InitParams read_cli(std::vector<std::string>& arguments)
{
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }

    CLI::App app;
    return App::Launcher::read_cli(app, "test", static_cast<int>(argv.size()), argv.data());
}

// Parses the wipe_tower flags given in arguments, returns the resulting value.
bool parse_wipe_tower(std::vector<std::string> arguments)
{
    arguments.insert(arguments.begin(), "prusa-slicer");
    const App::InitParams params = read_cli(arguments);
    REQUIRE(params.config_overrides.size() == 1);
    REQUIRE(params.config_overrides.front().name() == "wipe_tower");
    return params.config_overrides.front().get<bool>();
}

} // namespace

TEST_CASE("Command line parses config options", "[cli]")
{
    std::vector<std::string> arguments{"prusa-slicer", "--skirts", "3"};
    const App::InitParams params = read_cli(arguments);

    REQUIRE(params.exit_code == std::nullopt);
    REQUIRE(params.config_overrides.size() == 1);
    CHECK(params.config_overrides.front().name() == "skirts");
    CHECK(params.config_overrides.front().get<int>() == 3);
}

TEST_CASE("Command line negates boolean config options", "[cli]")
{
    CHECK(parse_wipe_tower({"--wipe-tower"}));
    CHECK_FALSE(parse_wipe_tower({"--no-wipe-tower"}));
}

TEST_CASE("Command line takes the last occurrence of a boolean config option", "[cli]")
{
    CHECK(parse_wipe_tower({"--no-wipe-tower", "--wipe-tower"}));
    CHECK_FALSE(parse_wipe_tower({"--wipe-tower", "--no-wipe-tower"}));
    CHECK_FALSE(parse_wipe_tower({"--wipe-tower", "--wipe-tower", "--no-wipe-tower"}));
    CHECK(parse_wipe_tower({"--no-wipe-tower", "--no-wipe-tower", "--wipe-tower"}));
}
