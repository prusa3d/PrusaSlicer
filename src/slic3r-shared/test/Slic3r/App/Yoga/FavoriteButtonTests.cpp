#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Config/FavoriteButton.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App;
using namespace Slic3r::Domain;

static void hover_button(YogaComponentFixture& f, FavoriteButton* btn)
{
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    f.mouse_move(cx, cy);
    f.render();
}

static void unhover_button(YogaComponentFixture& f, FavoriteButton* btn)
{
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() - 1.f;
    const float cy  = pos.y() - 1.f;
    f.mouse_move(cx, cy);
    f.render();
}

TEST_CASE_METHOD(YogaComponentFixture, "FavoriteButton state icon: default mode shows star always")
{
    FavoriteButton* button = window->emplace_back<FavoriteButton>();
    button->set_checked(true);
    render();

    REQUIRE(button->icon() == Render::Icon::StarSolid);
    hover_button(*this, button);
    REQUIRE(button->icon() == Render::Icon::StarSolid);

    unhover_button(*this, button);
    button->set_checked(false);

    REQUIRE(button->icon() == Render::Icon::Star);
    hover_button(*this, button);
    REQUIRE(button->icon() == Render::Icon::Star);
}

TEST_CASE_METHOD(YogaComponentFixture, "FavoriteButton state icon: shows star only on hover")
{
    FavoriteButton* button = window->emplace_back<FavoriteButton>();
    button->set_show_only_on_hover(true);

    button->set_checked(false);
    render();
    REQUIRE(button->icon() == Render::Icon::None);
    hover_button(*this, button);
    REQUIRE(button->icon() == Render::Icon::Star);

    unhover_button(*this, button);
    REQUIRE(button->icon() == Render::Icon::None);

    button->set_checked(true);
    render();
    REQUIRE(button->icon() == Render::Icon::None);
    hover_button(*this, button);
    REQUIRE(button->icon() == Render::Icon::StarSolid);
}
