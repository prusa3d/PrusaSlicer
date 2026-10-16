#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ObjectList {
struct ListRow;
} // namespace Slic3r::Biz::ObjectList

namespace Slic3r::App {

namespace Yoga {
class LayoutButton;
class Icon;
} // namespace Yoga

class ExtruderIcon;
class ContextMenuRequester;

namespace ObjectList {

enum class NodeLevel : size_t
{
    Bed = 0,
    Object,
    SubObject,
};

enum class Mode
{
    Plater,
    Preview,
};

class CollapsibleArea : public Yoga::Item
{
public:
    struct Callbacks
    {
        std::function<bool()> has_content{nullptr};
    };

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

private:
    Callbacks m_callbacks;
};

class ActionButton :public Yoga::LayoutButton
{
public:
    ActionButton(Render::Icon icon, const std::string& tooltip = std::string{});
};

class TreeNodeButton : public Yoga::RectangleButton
{
public:
    void update_state();

    /**
     * @brief Expands the node (shows its collapsible area), as a click on the collapse button.
     */
    void expand();

protected:
    TreeNodeButton(NodeLevel level, CollapsibleArea* collapsible_item);

private:
    Yoga::LayoutButton* m_collapse_button{nullptr};
    bool m_expanded{true};

    NodeLevel m_level{NodeLevel::Bed};
    CollapsibleArea* m_collapsible_item{nullptr};
};

/**
 * @brief A leaf-row style shared by Object-level rows (Connectors, WipeTower)
 * or SubObject-level rows (Volume, Instance) -
 * no collapse arrow, just an indented row of icon/text/controls.
 */
class LeafButton : public Yoga::RectangleButton
{
public:
    LeafButton(NodeLevel level);
};

enum class PrintableItem
{
    Object,
    Instance
};

/**
 * @brief Creates the shared "printable" eye-icon toggle button used by both ObjectButton and
 * InstanceButton, wired to hide itself unless hovered/unprintable.
 * @param on_process_checked Called with the new checked state when the button is clicked.
 * @param on_update_visibility Called whenever the button's own visibility should be re-evaluated
 *        (checked state changed or hovered state changed).
 */
ActionButton* add_printable_button(
    Yoga::Item* parent,
    PrintableItem item,
    bool is_printable,
    std::function<void(bool)> on_process_checked,
    std::function<void()> on_update_visibility
);

Yoga::Icon* add_icon(Yoga::Item* parent, Render::Icon render_icon, Yoga::Unit size = Yoga::Unit(20.f));

ExtruderIcon* add_extruder_icon(Yoga::Item* parent, Yoga::Unit size = Yoga::Unit(18.f));

/**
 * @brief Left click on @p row: applies it to the selection with the Ctrl/Shift modifier currently
 * held, see Biz::ObjectList::ObjectListSelector for the rules.
 */
void select_row(Biz::ProjectInteractor& project_interactor, const Biz::ObjectList::ListRow& row);

/**
 * @brief Right-click on a row: a checked row whose selection mode matches the current selection
 * keeps the selection (so the menu applies to a whole multi-selection), otherwise the selection
 * is replaced by just this row (no Ctrl/Shift handling). Then the context menu matching the
 * resulting selection is requested at the mouse position.
 * @param row_checked Whether the clicked row is shown as selected, highlighted or bordered (which
 * is also the case when only a part of the row is selected, e.g. a volume of the object).
 */
void request_context_menu(
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester,
    bool row_checked,
    const Biz::ObjectList::ListRow& row
);

} // namespace ObjectList
} // namespace Slic3r::App
