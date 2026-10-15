#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "../ConnectJsonTestUtils.hpp"
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem/path.hpp>

#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

using namespace Slic3r;
std::string read_json(const std::string& file_name)
{
    auto file_path = Tests::get_datadir() / "hw_config_connect" / file_name;
    std::ifstream f{file_path.string()};
    if (!f.is_open()) {
        FAIL("Cannot open file " + file_path.string());
    }
    std::string content ((std::istreambuf_iterator(f)), std::istreambuf_iterator<char>());
    f.close();
    return content;
}

TEST_CASE("Load Connect HW Config Tests", "[preset]")
{

    for (const auto& [test_input, expected] : Biz::Preset::ConnectJsonTestUtils::create_test_desc())
    {
        INFO("Printer file: " << test_input.file_name);
        auto json = read_json(test_input.file_name);
        auto hw_config_result = Biz::PhysicalPrinter::hw_config_from_connect_json(json);

        REQUIRE(hw_config_result.has_value());

        const auto& hw_config = hw_config_result.value();
        REQUIRE(hw_config.model == expected.model);
        REQUIRE(hw_config.tool_count == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tools.size());
        REQUIRE(hw_config.technology == expected.technology);
        for (size_t i = 0; i < hw_config.tools.size(); ++i) {
            INFO("tool: " << i);
            const auto& exp = expected.tools.at(i);
            const auto& t = hw_config.tools.at(i);

            REQUIRE(t.features == exp.features);
        }
    }
}
