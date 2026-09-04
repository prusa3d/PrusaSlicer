#include <catch2/catch_test_macros.hpp>

#if SLIC3R_ENABLE_FORMAT_STEP

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

#include "Slic3r/Biz/Format/STEP.hpp"

using Slic3r::Domain::Vec3d;
using Slic3r::Domain::is_approx;
using namespace Slic3r;

static inline std::string step_path(const char* path)
{
    return std::string(TEST_DATA_DIR) + "/test_step/" + path;
}

SCENARIO("Reading a STEP file", "[step]")
{
    GIVEN("a valid STEP file")
    {
        WHEN("STEP file is read")
        {
            THEN("load should succeed")
            {
                auto result = Biz::load_step(step_path("test.step"));
                REQUIRE(result.has_value());
                REQUIRE(!result->objects.empty());

                // Check that we loaded a model with at least one object
                const auto& model = result.value();
                REQUIRE(model.objects.size() > 0);

                // Verify the object has volumes
                REQUIRE(!model.objects.front()->volumes.empty());
            }
        }
    }

    GIVEN("a non-existent file")
    {
        WHEN("attempting to load")
        {
            THEN("load should fail with an error message")
            {
                auto result = Biz::load_step(step_path("nonexistent.stp"));
                REQUIRE(!result.has_value());
                REQUIRE(!result.error().empty());
            }
        }
    }

    GIVEN("a STEP file with custom deflection parameters")
    {
        WHEN("STEP file is read with specific deflections")
        {
            THEN("load should succeed")
            {
                // Test with custom linear and angular deflections
                std::pair<double, double> deflections{0.01, 0.5};
                auto result = Biz::load_step(step_path("test.step"), deflections);
                REQUIRE(result.has_value());
                REQUIRE(!result->objects.empty());
                Domain::BoundingBox3d bbox = result->objects.front()->volumes.front()->mesh().bounding_box();
                REQUIRE(is_approx(Biz::Algorithms::BoundingBox::sizes(bbox), Vec3d(88., 31., 88.), 0.01));
            }
        }
    }
}

#endif // SLIC3R_ENABLE_FORMAT_STEP
