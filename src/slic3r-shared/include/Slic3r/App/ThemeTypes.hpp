#pragma once

namespace Slic3r::App::Platform {

/**
 * @brief Color Id enum
 */
enum class Color
{
    Text,
    TextLink,
    WindowBg,
    WindowBgAlternate,
    Control,
    AccentPrimary,
    AccentSecondary,
    AccentTertiary,
    Button,
    ButtonTransparent,
    RadioButtonBackground,
    RadioButton,
    Scrollbar,
    NavCursor,
    ModalWindowDimBg,
    Warning,
    Error,
    SceneBgTop,
    SceneBgBottom,
    SceneBgErrorTop,
    SceneBgErrorBottom,
    Transparent,
};

/**
 * @brief Color state group, tied with Color itself, consider Colors[Color][ColorGroup]
 */
enum class ColorGroup
{
    Default, ///< Unselected control
    Disabled, ///< Disabled control
    Active, ///< Selected active state
    ActiveDisabled, ///< Activated & Disabled state
    Hovered, ///< Hovered by mouse
};

} // namespace Slic3r::App::Platform
