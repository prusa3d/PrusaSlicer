#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/Slider.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

TEST_CASE_METHOD(YogaComponentFixture, "Slider: mouse wheel steps the value unless turned off")
{
    Slider* slider = window->emplace_back<Slider>(0., 10.);
    slider->set_width(200.f);
    slider->set_height(20.f);
    slider->set_flex_shrink(0.f);

    hover(slider);
    mouse_wheel(1.f);
    const double stepped = slider->value();

    Item::set_wheel_changes_value(false);
    mouse_wheel(1.f);
    Item::set_wheel_changes_value(true);

    REQUIRE(stepped == 1.);
    REQUIRE(slider->value() == 1.);
}
