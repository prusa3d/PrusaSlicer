#pragma once

#include <catch2/catch_test_macros.hpp>

#include <imgui_internal.h>

#include <Slic3r/Assert.hpp>
#include <Slic3r/Biz/Platform/IRenderRequestHandler.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Theme.hpp>

/** @brief ImGui test fixture for Yoga component tests. */
struct ImGuiFixture : public Slic3r::Biz::Platform::IRenderRequestHandler
{
    ImGuiFixture()
    {
        default_size_info.dpi               = 96;
        default_size_info.dpi_scale_factor  = 1.0f;
        default_size_info.viewport_size_x   = 1280;
        default_size_info.viewport_size_y   = 720;
        default_size_info.root_font_size    = 18;
        default_size_info.root_font_size_pt = 11;

        Slic3r::Biz::Platform::PlatformServices::instance().set_render_request_handler(this);

        m_theme = std::make_unique<Slic3r::App::Theme>(Slic3r::App::Theme::Style::Dark);
        Slic3r::App::Yoga::Item::set_theme(m_theme.get());

        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1'280, 720);
        io.DeltaTime   = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendRendererName = "UnitTest-NoRenderer";
        // NavEnableKeyboard is required for SetKeyboardFocusHere() / InputText focus to work.
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::NewFrame();
        process_textures();
        ImGui::PushID(Catch::getResultCapture().getCurrentTestName().c_str());
    }

    ~ImGuiFixture()
    {
        ImGui::PopID();
        ImGui::Render();
        ImGui::DestroyContext(ctx);
    }

    /** @brief Satisfy pending WantCreate texture requests with a dummy handle. */
    void process_textures()
    {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->Status == ImTextureStatus_WantCreate) {
                tex->SetTexID((ImTextureID) 0x1);
                tex->SetStatus(ImTextureStatus_OK);
            }
        }
    }

    /** @brief Close the current frame and open a new one. IO events injected before this take effect in it. */
    void next_frame()
    {
        ImGui::PopID();
        ImGui::Render();
        ImGui::NewFrame();
        process_textures();
        ImGui::PushID(Catch::getResultCapture().getCurrentTestName().c_str());
    }

    /** @brief Advance one frame and render the RootItem. Call once as warm-up before injecting events. */
    void render()
    {
        next_frame();
        root.root_render(default_size_info);
    }

    void mouse_move(float x, float y)
    {
        ImGui::GetIO().AddMousePosEvent(x, y);
    }

    void mouse_down(int btn = 0)
    {
        ImGui::GetIO().AddMouseButtonEvent(btn, true);
    }

    void mouse_up(int btn = 0)
    {
        ImGui::GetIO().AddMouseButtonEvent(btn, false);
    }

    /** @brief Simulate a full mouse click over 3 frames (hover → down → up). */
    void simulate_click(float x, float y, int btn = 0)
    {
        mouse_move(x, y);
        render();
        mouse_down(btn);
        render();
        mouse_up(btn);
        render();
    }

    /** @brief Advance two frames so a preceding request_focus() takes effect via ImGui's nav system. */
    void flush_focus()
    {
        render();
        render();
    }

    void key_down(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
    }

    void key_up(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, false);
    }

    void key_tap(ImGuiKey key)
    {
        key_down(key);
        render();
        key_up(key);
        render();
    }

    /** @brief Inject UTF-8 characters and advance one frame. */
    void type_text(const char* str)
    {
        ImGui::GetIO().AddInputCharactersUTF8(str);
        render();
    }

    ImGuiContext* ctx;
    std::unique_ptr<Slic3r::App::Theme> m_theme;
    Slic3r::App::Yoga::SizeInfo default_size_info;
    Slic3r::App::Yoga::RootItem root;

    void request_render() override {}
};
