#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/LayoutButton.hpp>
#include <Slic3r/App/Yoga/SliderWithInput.hpp>
#include <vector>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App;
using namespace Slic3r::App::Yoga;

namespace {
LayoutButton* find_button(Item* item, Render::Icon icon)
{
    if (auto* button = dynamic_cast<LayoutButton*>(item); button && button->icon() == icon)
        return button;
    for (Item* child : item->items()) {
        if (auto* button = find_button(child, icon))
            return button;
    }
    return nullptr;
}

struct AngleSliderFixture : YogaComponentFixture
{
    AngleSliderFixture()
    {
        auto* row = window->emplace_back<Item>();
        row->set_width(360);
        row->set_height(32);
        row->set_margin(Margins(100, 100, 0, 0));
        slider = row->emplace_back<SliderWithInput>("°");
        slider->set_flex_grow(1.f);
        slider->set_input_width(100);
        slider->set_begin_value(-180.);
        slider->set_end_value(180.);
        slider->set_step(0.1);
        slider->set_step_buttons_visible(true);
        auto* reset = row->emplace_back<LayoutButton>("", Render::Icon::DSRevert);
        reset->set_width(24);
        reset->set_height(24);
        slider->set_revert_button(reset);
        slider->set_default(0.);
        slider->set_value(0.);
        render();
    }

    void click_button(Render::Icon icon)
    {
        auto* button = icon == Render::Icon::DSRevert ? slider->revert_button() : find_button(slider, icon);
        REQUIRE(button != nullptr);
        render();
        const Vec2f pos = button->get_global_pos();
        simulate_click(pos.x() + button->width() * 0.5f, pos.y() + button->height() * 0.5f);
    }

    SliderWithInput* slider{nullptr};
};
} // namespace

TEST_CASE_METHOD(AngleSliderFixture, "SliderWithInput: fine steps update value and reset", "[angle-slider]")
{
    std::vector<double> changes;
    slider->callbacks().value_changed = [&](double value) { changes.push_back(value); };
    click_button(Render::Icon::Plus);
    REQUIRE(slider->value() == Catch::Approx(0.1));
    REQUIRE(slider->revert_button()->is_visible());
    click_button(Render::Icon::Minus);
    REQUIRE(slider->value() == Catch::Approx(0.).margin(1e-10));
    click_button(Render::Icon::Minus);
    REQUIRE(slider->value() == Catch::Approx(-0.1));
    click_button(Render::Icon::DSRevert);
    REQUIRE(slider->value() == Catch::Approx(0.).margin(1e-10));
    REQUIRE_FALSE(slider->revert_button()->is_visible());
    REQUIRE(changes.size() == 4);
}

TEST_CASE_METHOD(AngleSliderFixture, "SliderWithInput: fine steps respect angle limits", "[angle-slider]")
{
    slider->set_value(180.);
    click_button(Render::Icon::Plus);
    REQUIRE(slider->value() == Catch::Approx(180.));
    click_button(Render::Icon::Minus);
    REQUIRE(slider->value() == Catch::Approx(179.9));
    slider->set_value(-180.);
    click_button(Render::Icon::Minus);
    REQUIRE(slider->value() == Catch::Approx(-180.));
    click_button(Render::Icon::Plus);
    REQUIRE(slider->value() == Catch::Approx(-179.9));
}

TEST_CASE_METHOD(AngleSliderFixture, "SliderWithInput: disabled fine controls resume after unlocking", "[angle-slider]")
{
    slider->set_value(12.3);
    slider->set_enabled(false);
    click_button(Render::Icon::Plus);
    click_button(Render::Icon::Minus);
    REQUIRE(slider->value() == Catch::Approx(12.3));
    slider->set_enabled(true);
    click_button(Render::Icon::Plus);
    REQUIRE(slider->value() == Catch::Approx(12.4));
}
