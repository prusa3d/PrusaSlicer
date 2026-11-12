#include "Slic3r/App/Preview/DoubleSliderForGCode.hpp"

#include "Slic3r/App/CommandBindingManager.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Preview {

static constexpr float SLIDER_GCODE_HEIGHT = 40.0f;
using FuncCommandExtraOpts                 = Platform::FuncCommandExtraOpts;

DoubleSliderForGcode::DoubleSliderForGcode() :
    Imgui::DoubleSlider::Manager<unsigned int>(
        std::string("GCodeSlider"),
        Biz::L("Steps"),
        Yoga::Orientation::Horizontal
    )
{
    set_min_width(100);
    set_min_height(SLIDER_GCODE_HEIGHT);
    set_flex_shrink(0);
    m_ctrl->callbacks().value_changed       = [this]() { process_thumb_move(); };
    m_ctrl->callbacks().request_extra_frame = [this]() { process_request_extra_frames(); };
}

void DoubleSliderForGcode::register_commands(
    MenuManager& menu_manager,
    CommandBindingManager& command_binding_manager
)
{
    command_binding_manager.main_command_registry()
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-slow",
                [this]() { move_current_thumb(1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Right}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-slow",
                [this]() { move_current_thumb(-1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Left}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-medium",
                [this]() { move_current_thumb(5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Right
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-medium",
                [this]() { move_current_thumb(-5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Left
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-fast",
                [this]() { move_current_thumb(10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Right
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-fast",
                [this]() { move_current_thumb(-10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Left
                    }}
                }
            )
        );

    if (!m_menu_manager) {
        m_menu_manager = &menu_manager;
    }

    if (!m_command_binding_manager) {
        m_command_binding_manager = &command_binding_manager;
    }
}

} // namespace Slic3r::App::Preview
