#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/TestUtils.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace Slic3r::Domain;
using namespace Slic3r::Domain::Preset;

using Addresses = std::vector<Address>;
using Slic3r::Test::build_fff_printer_config;


Addresses iterate(const HwPrinterConfig& config)
{
    Addresses visited_addresses;
    for (auto it : MaterialIterator(config))
        visited_addresses.push_back(it.address());
    return visited_addresses;
}

TEST_CASE("MaterialIterator")
{
    SECTION("Single-tool")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(1, {});
        printer_config.tools[0].features["x"] = false;
        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0}});

        auto it = begin(MaterialIterator{printer_config});
        REQUIRE(it.is_valid());
        REQUIRE(it.address() == Address{0});
        REQUIRE(it.feeder_count() == 0);
        REQUIRE(std::get<bool>(it.tool_config().features.find("x")->second) == false);

        ++it;
        REQUIRE(it == end(MaterialIterator{printer_config}));
    }

    SECTION("Multi-tool")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(3, {});
        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0}, {1}, {2}});
    }

    SECTION("Single-tool with feeder")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(
            1,
            {
                {
                    {0}, HwFeederConfig{
                        .slot_count = 3,
                        .features = {{"x", 2.f}}
                    }
                }
            }
        );
        printer_config.tools[0].features["x"] = 1.f;

        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0, 0}, {0, 1}, {0, 2}});

        auto it = begin(MaterialIterator{printer_config});
        for (uint8_t i = 0; i < 3; i++) {
            REQUIRE(it != end(MaterialIterator{printer_config}));
            REQUIRE(it.is_valid());

            REQUIRE(it.address() == Address{0,  i});
            REQUIRE(it.feeder_count() == 1);
            REQUIRE(std::get<double>(it.tool_config().features.find("x")->second) == 1.0f);
            REQUIRE(std::get<double>(it.feeder_config(0).features.find("x")->second) == 2.0f);

            ++it;
        }

        REQUIRE(it == end(MaterialIterator{printer_config}));
        REQUIRE(it.is_valid() == false);
    }

    SECTION("Single-tool with nested feeder")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(
            1, {
                {
                    {0}, HwFeederConfig{.slot_count = 3}
                },
                {
                    {0,0}, HwFeederConfig{.slot_count = 3}}
                }
            );

        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 1}, {0, 2}});
    }

    SECTION("Multi-tool with nested feeder")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(
            3, {
                {
                    {0}, HwFeederConfig{.slot_count = 3}
                },
                {
                    {0,0}, HwFeederConfig{.slot_count = 3}}
                }
            );

        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 1}, {0, 2}, {1}, {2}});
    }

    SECTION("Multi-tool with multiple feeders")
    {
        HwPrinterConfig printer_config = build_fff_printer_config(
            3, {{{1}, HwFeederConfig{.slot_count = 3}}, {{2}, HwFeederConfig{.slot_count = 3}}}
        );

        Addresses visited_addresses = iterate(printer_config);
        REQUIRE(visited_addresses == Addresses{{0}, {1, 0}, {1, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}});
    }
}
