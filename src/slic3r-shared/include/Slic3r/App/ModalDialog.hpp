#pragma once

namespace Slic3r::App {

enum class ModalDialog
{
    None,
    Preferences,
    CrashedProjects,
    Welcome,
    PresetUpdater,
    EffectiveValues,
    MsgBox, ///< One-of modal dialogs created by MsgBoxHandler
    InvalidData,
};

} // namespace Slic3r::App
