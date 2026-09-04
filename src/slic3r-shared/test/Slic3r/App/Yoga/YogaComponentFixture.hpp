#pragma once

#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Yoga/Window.hpp>

#include "ImGuiFixture.hpp"

/** @brief Fixture for testing Yoga components that require a Window context. */
struct YogaComponentFixture : public ImGuiFixture
{
    YogaComponentFixture()
    {
        window = root.emplace_back<Slic3r::App::Yoga::Window>("test");
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
    Slic3r::App::Yoga::Window* window = nullptr;
};
