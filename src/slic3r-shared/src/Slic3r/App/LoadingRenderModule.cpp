#include "Slic3r/App/LoadingRenderModule.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Render/CommandBuffer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Yoga/ImGuiUtils.hpp"

namespace Slic3r::App {

LoadingRenderModule::LoadingRenderModule(
    PresetUpdater::PresetUpdaterController& controller, Navigator& navigator
) :
    m_controller(controller),
    m_navigator(navigator),
    m_menu_manager(m_command_registry),
    m_command_binding_manager(m_command_registry)
{}

LoadingRenderModule::~LoadingRenderModule() = default;

void LoadingRenderModule::on_init(
    Render::Device& device,
    Render::ImguiRender& imgui_render,
    Platform::AnimationManager& animation_manager
)
{
    Platform::AbstractRenderModule::on_init(device, imgui_render, animation_manager);

    AppServices::instance().theme().initialize_imgui_style();
    Yoga::Item::set_imgui_render(&imgui_render);
    Yoga::Item::set_theme(&AppServices::instance().theme());

    m_dialog.reset(std::make_unique<PresetUpdaterDialog>(m_controller, m_navigator));
    m_layout.append(m_dialog.release());
    m_dialog->attach_to_center();
}

void LoadingRenderModule::set_opened_preset_updater(bool opened)
{
    if (m_dialog.get() == nullptr || m_dialog->opened() == opened) {
        return;
    }
    if (opened) {
        m_dialog->open();
    } else {
        m_dialog->close();
    }
}

bool LoadingRenderModule::is_opened_preset_updater() const
{
    return m_dialog.get() != nullptr && m_dialog->opened();
}

const Platform::CommandRegistry::CommandsMap& LoadingRenderModule::gizmo_commands() const
{
    static const Platform::CommandRegistry::CommandsMap no_gizmo_commands;
    return no_gizmo_commands;
}

const std::optional<Platform::CameraSynchData>& LoadingRenderModule::camera_synch_data() const
{
    static const std::optional<Platform::CameraSynchData> no_camera;
    return no_camera;
}

void LoadingRenderModule::render_scene(Render::CommandBuffer& cmd_buffer)
{
    Render::ScopedDebugGroup event_render("Loading Render", cmd_buffer);
    m_device->load_state();

    const Domain::ColorRGBA& background =
        AppServices::instance().theme().color(Platform::Color::WindowBg);

    cmd_buffer.set_viewport(Render::Rect::from(0, 0, m_screen_info));
    cmd_buffer.set_clear_values({background.r(), background.g(), background.b(), background.a()});
    cmd_buffer.clear_buffers(true, true);

    cmd_buffer.submit();
}

void LoadingRenderModule::render_imgui(Render::CommandBuffer& cmd_buffer)
{
    Yoga::SetOurStyleVars our_vars;
    m_layout.root_render(m_info);
}

void LoadingRenderModule::on_screen_resized()
{
    Yoga::Object::set_scale_factor(m_screen_info.scale());
    if (m_imgui_render != nullptr) {
        m_imgui_render->set_scale_factor(m_screen_info.scale());
    }

    m_info.dpi_scale_factor = m_screen_info.scale();
    m_info.dpi              = m_screen_info.dpi() > 0 ?
        m_screen_info.dpi() :
        static_cast<int>(96.f * m_info.dpi_scale_factor);
    m_info.viewport_size_x   = static_cast<int>(m_screen_info.logical_width());
    m_info.viewport_size_y   = static_cast<int>(m_screen_info.logical_height());
    m_info.root_font_size    = m_screen_info.root_font_size();
    m_info.root_font_size_pt = m_screen_info.root_font_size_pt();
}

} // namespace Slic3r::App
