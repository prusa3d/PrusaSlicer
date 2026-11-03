#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Preview/FdmViewerWrapper.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

using namespace Slic3r::App::Preview;
namespace CustomGCode = Slic3r::Domain::CustomGCode;

TEST_CASE(
    "Layers slider ticks keep the text of the project custom G-codes",
    "[Preview][CustomGCode]"
)
{
    CustomGCode::Info project_custom_gcode;
    project_custom_gcode.mode   = CustomGCode::Mode::SingleExtruder;
    project_custom_gcode.gcodes = {
        {1.2, CustomGCode::Type::Custom, 1, "", "M117 AAA"},
        {3.0, CustomGCode::Type::PausePrint, 1, "", "Insert magnets"},
        {5.4, CustomGCode::Type::Template, 1, "", ""},
        {7.2, CustomGCode::Type::Custom, 1, "", "M117 BBB"},
    };

    // As rebuilt by the G-code processor: no text, templates reported as custom G-codes.
    Slic3r::Biz::libpgcode::ProcessorResult result;
    for (const CustomGCode::Item& item : project_custom_gcode.gcodes) {
        const CustomGCode::Type type =
            item.type == CustomGCode::Type::Template ? CustomGCode::Type::Custom : item.type;
        result.custom_gcode_per_print_z.push_back({item.print_z, type, item.extruder, "", ""});
    }

    SECTION("Ticks are taken from the project custom G-codes")
    {
        const FdmViewerWrapperInputData data =
            extract_wrapper_input_data_from_result(result, project_custom_gcode);
        CHECK(data.custom_gcode_info.gcodes == project_custom_gcode.gcodes);
    }

    SECTION("No ticks when the bed has no custom G-codes")
    {
        const FdmViewerWrapperInputData data =
            extract_wrapper_input_data_from_result(result, std::nullopt);
        CHECK(data.custom_gcode_info.gcodes.empty());
    }
}
