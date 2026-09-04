#pragma once

#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterDialog.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"

namespace Slic3r::App {

class Navigator;

/**
 * The surface the application runs on until the presets installed on this computer are known to be
 * usable with this build.
 *
 * It needs no presets, no project and no scene, so it can be made active before anything preset
 * related is initialized. It draws nothing but the background, and shows the PresetUpdaterDialog
 * when the presets turn out to need a forced reconfiguration.
 */
class LoadingRenderModule : public Platform::AbstractRenderModule
{
public:
    LoadingRenderModule(PresetUpdater::PresetUpdaterController& controller, Navigator& navigator);
    ~LoadingRenderModule() override;

    void set_opened_preset_updater(bool opened);
    bool is_opened_preset_updater() const;

    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void render_imgui(Render::CommandBuffer& cmd_buffer) override;

    void set_navigator(Navigator* navigator) override {}

    MenuManager& menu_manager() override
    {
        return m_menu_manager;
    }

    CommandBindingManager& command_binding_manager() override
    {
        return m_command_binding_manager;
    }

    const Platform::CommandRegistry::CommandsMap& gizmo_commands() const override;

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const override;
    void set_camera_synch_data(const Platform::CameraSynchData& data) override {}

protected:
    void on_init(
        Render::Device& device,
        Render::ImguiRender& imgui_render,
        Platform::AnimationManager& animation_manager
    ) override;

    void on_screen_resized() override;

private:
    PresetUpdater::PresetUpdaterController& m_controller;
    Navigator& m_navigator;

    Yoga::RootItem m_layout;
    Yoga::SizeInfo m_info;
    Yoga::Passthrough<PresetUpdaterDialog> m_dialog;

    MenuManager m_menu_manager;
    CommandBindingManager m_command_binding_manager;
};

} // namespace Slic3r::App
