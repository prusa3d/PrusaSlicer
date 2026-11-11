#pragma once

#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>
#include <Slic3r/App/Yoga/Window.hpp>

#include "ImGuiFixture.hpp"

/** @brief Fixture for testing Yoga components that require a Window context. */
struct YogaComponentFixture : public ImGuiFixture
{
    YogaComponentFixture()
    {
        window = root.emplace_back<Slic3r::App::Yoga::Window>("Test");
        window->set_padding(0.f);
        window->set_flex_grow(1.f);
        window->set_rounding(0.f);
        window->set_flags(
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoScrollWithMouse
        );
        render(); // warm-up: register the Window with ImGui so hover detection works from the first frame
    }

    /** @brief Lay out, move the mouse over a point of the item (fractions of its size) and advance two frames. */
    void hover(const Slic3r::App::Yoga::Item* item, float fx = 0.5f, float fy = 0.5f)
    {
        render();
        const Slic3r::App::Yoga::Vec2f pos = item->get_global_pos();
        mouse_move(pos.x() + item->width() * fx, pos.y() + item->height() * fy);
        render();
        render();
    }

    /** @brief Add a 300x100 ScrollArea to the window; call add_overflow() after adding its content. */
    Slic3r::App::Yoga::ScrollArea*
    make_page(Slic3r::App::Yoga::Orientation orientation = Slic3r::App::Yoga::Orientation::Vertical)
    {
        auto* page = window->emplace_back<Slic3r::App::Yoga::ScrollArea>();
        page->set_width(300.f);
        page->set_height(100.f);
        page->set_orientation(orientation);
        return page;
    }

    void add_overflow(Slic3r::App::Yoga::ScrollArea* page)
    {
        auto* overflow = page->emplace_back<Slic3r::App::Yoga::Item>();
        overflow->set_width(600.f);
        overflow->set_height(300.f);
        overflow->set_flex_shrink(0.f);
    }

    Slic3r::App::Yoga::Window* window = nullptr;
};
