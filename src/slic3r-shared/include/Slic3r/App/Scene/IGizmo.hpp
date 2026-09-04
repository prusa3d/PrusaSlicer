#pragma once

#include "Slic3r/App/Scene/GizmoEventContext.hpp"
#include "Slic3r/App/Scene/GizmoKeyEventContext.hpp"
#include "Slic3r/App/Scene/IGizmoController.hpp"
#include "Slic3r/App/Platform/CommandRegistry.hpp"

#include "Slic3r/Domain/PrinterTechnology.hpp"

namespace Slic3r::App::Plater {
class GizmoWindow;
}

namespace Slic3r::App::Scene {

class Clipper;

/**
 * @brief State of event procession for specific IGizmo.
 */
enum class GizmoActivationState {
    /// Event is not processed, no more event needed until next cycle
    Inactive,
    /// Gizmo is still probing if process the event, expects next event to come
    Probing,
    /// Gizmo is consuming the event
    Active,
    /// Gizmo finished event consuming (next cycle can start).
    Done
};

/**
 * @brief Interface for plater scene gizmo, a piece of logic processing user events and eventually
 * modifying scene.
 *
 * GizmoManager is the main entry point for gizmo logic, it maintains a list of gizmos and manages
 * selection of active one.
 *
 * IGizmo implementation needs to provide only single method on_mouse(), other methods are optional.
 *
 * ### Event processing
 *
 * #### Basic cycle
 *
 * Basic event processing cycle is made of following gizmo calls:
 * - on_cycle_prepare() is called for all gizmos before mouse event processing starts. This is
 *   useful to clear gizmo state before the cycle.
 * - on_mouse() is called one or multiple times (depending on the return value of all active gizmos
 *   indicating gizmo activation state).
 * .
 *
 * @note The activation state indicates if the gizmo:
 * - don't want to process the cycle anymore (`GizmoActivationState::Inactive`),
 * - is not sure if it will continue in the cycle (`GizmoActivationState::Probing`),
 * - is actively processing the event (and modifying the scene, `GizmoActivationState::Active`),
 * - or finished with processing and a new cycle can start (`GizmoActivationState::Done`)
 * .
 *
 * #### Transient Event
 *
 * Additionally there is on_transient_mouse() to allow handling a simple logic (like highlighting)
 * to run independently on the activation state.
 *
 * ### Adhoc Rendering
 *
 * There following methods provides interface to plug-in ad-hoc rendering if suitable:
 * - render_scene() for rendering 3D geometry (e.g. selection rectangle),
 * .
 */
class IGizmo {
public:
    virtual ~IGizmo() = default;
    /**
     * @name Mouse Event Cycle
     * @{
     */
    virtual void on_cycle_prepare() {}
    virtual GizmoActivationState on_mouse(GizmoEventContext& ctx, bool only_active) = 0;
    /**@}*/

    /**
     * @name Transient Mouse Event
     * @{
     */
    virtual void on_transient_mouse(GizmoEventContext& ctx) {}
    /**@}*/

    /**
     * @name Keyboard invoked commands
     * @{
     */
    virtual void on_keyboard(GizmoKeyEventContext& ctx) {}
    /**
     *
     */
    virtual void register_commands(Platform::CommandRegistry& registry) {}
    /**@}*/

    /**
     * @name Ad-hoc rendering
     * @{
     */
    virtual void render_scene(Render::CommandBuffer& cmd_buffer) {}
    /**@}*/

    /**
     * @name Clipper providing
     * @{
     */
    virtual void provide_clipper(Clipper& clipper) {}
    /**@}*/

    /**
     * @name object selection disabling
     * @brief Returns whether the object selection should be ignored by this tool.
     * @{
     */
    virtual bool disable_object_selection() const { return false; }
    /**@}*/

    /**
     * @brief Indicates whether the gizmo handles object selection.
     * This function will be overridden only in QuickSelectGizmo to return true.
     * @{
     */
    virtual bool handles_object_selection() const { return false; }
    /**@}*/
};

/**
 * @brief Identification of tool gizmo type (purpose).
 *
 * @note The ToolType do not contain any printer type variants. It is valid to have multiple
 * IToolGizmo instances of same type, each for differnet pritnter technology.
 */
enum class ToolType : uint8_t
{
    None,
    Translation,
    Rotation,
    Scale,
    Svg, // embosing tool for Scalable vector graphics(.svg files)
    PlaceOnFace,
    Simplify,
    PaintOnSupportsGizmo,
    PaintOnSeamsGizmo,
    PaintOnFuzzySkinGizmo,
    MultiMaterialPaintingGizmo,
    TextGizmo,
    MeasureGizmo,
    ArrangeGizmo,
    CutGizmo,
    VariableLayerHeightGizmo,
    HeightRangeGizmo,
    // add as needed, no printer type variants (use two distinct IToolGizmos with same type instead)
};

/**
 * @brief Tool gizmo with own toolbar button and activation/deactivation cycle.
 *
 * Unlike IGizmo the IToolGizmo represents a tool with own button in toolbar and a optional dialog.
 * This provides clear activation/deactivation
 */
class IToolGizmo : public IGizmo {
public:
    /**
     * @name Activation and deactivation cycle
     * @{
     */
    /**
     * Called when toolbar button gets clicked and gizmo tool can populate scene with own handlers.
     */
    virtual void on_activated() = 0;
    /**
     * Called when toolbar button gets clicked and gizmo tool can populate scene with own handlers.
     */
    virtual void on_deactivated() = 0;
    /**@}*/

    /**
     *  @brief  Opening tool by double clicking on volume(e.g. Text, SVG, CUT, ...)
     *  @param  ctx - ray cast into scene under mouse
     *  @retval     - True when tool wants to be acitvated
     */
    virtual bool allows_activation_by_double_click(const GizmoEventContext& ctx) { return false; }

    /**
     * Imgui render function 
     * Do not use the Yoga tree
     */
    virtual void render_imgui() {}

    /**
     * @name Project activation and deactivation
     * @note Project deactivation of a tool gizmo happens when a project with active tool gizmo gets
     * switched from (i.e. project will disappear). And analogically project activation of a tool
     * gizmo happens when a project where the tool gizmo was active gets switched to (i.e. project
     * will appear).
     * @{
     */
    virtual void on_project_activated(size_t new_project_id) {}
    virtual void on_project_deactivated(size_t old_project_id) {}
    /**@}*/

    /**
     * @name Identification and capabilities
     * @{
     */
    virtual ToolType type() const = 0;
    virtual bool supports_printer(Domain::PrinterTechnology pt) const;
    /**@}*/

    /**
     * @name Enabled state
     * @{
     */
    virtual bool enabled() const;
    /**@}*/

    virtual std::unique_ptr<Plater::GizmoWindow> release_ui_window();

    /**
     * @name IGizmoController providing
     * @{
     */
    virtual void provide_gizmo_controller(IGizmoController& controller) {}

    virtual std::optional<Undo::ToolState> get_tool_state() const
    {
        return std::nullopt;
    }

    virtual void set_tool_state(const Undo::ToolState&) {}

    /**@}*/
};

} // namespace Slic3r::App::Scene
