#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"

#include "Slic3r/App/ExtruderIcon.hpp"
#include "Slic3r/App/ContextMenu.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ObjectList/ObjectListInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui.h>

namespace Slic3r::App::ObjectList {

using namespace Yoga;

namespace {
const Unit default_height = 28_fpx;
} // namespace

ActionButton::ActionButton(Render::Icon icon, const std::string& tooltip) :
    LayoutButton(std::string{}, icon, tooltip)
{
    set_object_name("ActionButton");
    set_min_width(18_fpx);
    set_min_height(18_fpx);
    set_content_padding(0);
    set_content_apply_window_alpha(true);
    set_background_color(Platform::Color::ButtonTransparent);
    set_background_color_checked(
        m_theme->color_imgui(Platform::Color::ButtonTransparent),
        m_theme->color_imgui(Platform::Color::ButtonTransparent)
    );
    set_flex_shrink(0);
}

TreeNodeButton::TreeNodeButton(NodeLevel level, CollapsibleArea* collapsible_item) :
    m_level(level),
    m_collapsible_item(collapsible_item)
{
    set_content_justify_content(YGJustifyFlexStart);
    set_content_align_items(YGAlignCenter);
    set_content_apply_window_alpha(true);
    set_background_color(Platform::Color::ButtonTransparent);
    set_allow_overlap(true);
    set_flex_shrink(0);
    set_rounding(0.f);
    if (level != NodeLevel::Bed) {
        set_height(default_height);
    }

    const Unit left_padding = 20_fpx + static_cast<size_t>(m_level) * 10_fpx;
    set_content_padding(Paddings({left_padding, 5_fpx, 20_fpx, 5_fpx}));
    set_content_gap(5_fpx);

    Item* collapse_wrapper = emplace_back<Item>();
    collapse_wrapper->set_width(20_fpx);
    collapse_wrapper->set_height(20_fpx);

    m_collapse_button =
        collapse_wrapper->emplace_back<LayoutButton>(std::string{}, Render::Icon::CloseArrow);
    m_collapse_button->set_self_align(YGAlignCenter);
    m_collapse_button->set_checkable(true);
    m_collapse_button->set_min_width(18_fpx);
    m_collapse_button->set_min_height(18_fpx);
    m_collapse_button->set_content_padding(0);
    m_collapse_button->set_background_color(
        m_theme->color_imgui(Platform::Color::ButtonTransparent),
        m_theme->color_imgui(Platform::Color::WindowBgAlternate)
    );
    m_collapse_button->set_background_color_checked(
        m_theme->color_imgui(Platform::Color::ButtonTransparent),
        m_theme->color_imgui(Platform::Color::WindowBgAlternate)
    );
    m_collapse_button->set_flex_shrink(0);
    m_collapse_button->set_checked(false);

    m_collapse_button->callbacks().checked_changed = [this](bool checked)
    {
        m_collapse_button->set_icon(checked ? Render::Icon::OpenArrow : Render::Icon::CloseArrow);
        if (m_collapsible_item) {
            m_collapsible_item->set_visible(checked);
        }
    };
    m_collapse_button->callbacks().action = [this]() { m_expanded = m_collapse_button->checked(); };

    update_state();
}

void TreeNodeButton::update_state()
{
    if (m_collapsible_item->callbacks().has_content) {
        bool is_visible = m_collapsible_item->callbacks().has_content();
        m_collapse_button->set_visible(is_visible);
        m_collapse_button->set_checked(is_visible && m_expanded);
    }
}

void TreeNodeButton::expand()
{
    m_expanded = true;
    update_state();
}

LeafButton::LeafButton(NodeLevel level) : RectangleButton()
{
    set_content_justify_content(YGJustifyFlexStart);
    set_content_align_items(YGAlignCenter);
    set_content_apply_window_alpha(true);
    set_background_color(Platform::Color::ButtonTransparent);
    set_flex_shrink(0);
    set_rounding(0.f);

    // 45px = 20px(padding)+ 20px(collapse icon)+ 5px(gap)
    const Unit left_padding = 45_fpx + static_cast<size_t>(level) * 10_fpx;
    set_content_padding(Paddings({left_padding, 5_fpx, 20_fpx, 5_fpx}));
    set_content_gap(5_fpx);

    set_height(default_height);
}

ActionButton* add_printable_button(
    Item* parent,
    PrintableItem item,
    bool is_printable,
    std::function<void(bool)> on_process_checked,
    std::function<void()> on_update_visibility
)
{
    auto refresh = [](ActionButton* button, const PrintableItem& item, bool is_printable)
    {
        button->set_icon(is_printable ? Render::Icon::EyeOpen : Render::Icon::EyeClosed);
        std::string tooltip = is_printable ?
            (item == PrintableItem::Instance ?
                 Biz::_u8L("This object instance is printable.") :
                 Biz::_u8L("This object is printable.")) :
            (item == PrintableItem::Instance ?
                 Biz::_u8L("This object instance is unprintable.") :
                 Biz::_u8L("This object is unprintable."));
        tooltip += "\n" + Biz::_u8L("Click to change printable state.");
        button->set_tooltip(tooltip);
    };

    ActionButton* button = parent->emplace_back<ActionButton>(Render::Icon::EyeClosed);
    refresh(button, item, is_printable);
    button->set_checkable(true);
    button->set_checked(is_printable);
    button->set_visible(!is_printable);

    button->callbacks().action = [button, on_process_checked]()
    {
        if (on_process_checked) {
            on_process_checked(button->checked());
        }
    };

    button->callbacks().checked_changed = [button, item, refresh, on_update_visibility](bool is_printable)
    {
        refresh(button, item, is_printable);
        if (on_update_visibility) {
            on_update_visibility();
        }
    };

    button->callbacks().hovered_changed = [on_update_visibility](bool)
    {
        if (on_update_visibility) {
            on_update_visibility();
        }
    };

    return button;
}

Icon* add_icon(Item* parent, Render::Icon render_icon, Yoga::Unit size)
{
    Icon* icon = parent->emplace_back<Icon>(render_icon);

    icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    icon->set_aspect_ratio(1);
    icon->set_width(size);

    return icon;
}

ExtruderIcon* add_extruder_icon(Item* parent, Unit size)
{
    ExtruderIcon* icon = parent->emplace_back<ExtruderIcon>();
    icon->set_extruder_color({126, 25, 36});
    icon->set_extruder_index(2);
    icon->set_width(size);

    return icon;
}

void select_row(Biz::ProjectInteractor& project_interactor, const Biz::ObjectList::ListRow& row)
{
    using Biz::ObjectList::ClickModifier;

    const ImGuiIO& io = ImGui::GetIO();
    const ClickModifier modifier = io.KeyCtrl ?
        ClickModifier::Ctrl :
        (io.KeyShift ? ClickModifier::Shift : ClickModifier::None);
    project_interactor.scene_interactor().object_list_interactor().select_row(row, modifier);
}

void request_context_menu(
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester,
    bool row_checked,
    const Biz::ObjectList::ListRow& row
)
{
    using Kind = Biz::ObjectList::ListRow::Kind;
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    // A checked row can also mean "a child of this row is selected" (e.g. a volume of the object),
    // so the current selection is kept only when it is in the same mode as the row's selection.
    const Biz::Scene::SelectionMode row_mode =
        row.kind == Kind::Volume || row.kind == Kind::Volumes || row.kind == Kind::Connectors ?
        Biz::Scene::SelectionMode::Volume :
        Biz::Scene::SelectionMode::Instance;
    const bool keep_selection =
        row_checked && scene_interactor.object_selection().mode == row_mode;
    if (!keep_selection) {
        scene_interactor.object_list_interactor()
            .select_row(row, Biz::ObjectList::ClickModifier::None);
        if (scene_interactor.object_selection().empty()) {
            return;
        }
    }

    const ImVec2 mouse_pos = ImGui::GetMousePos();
    context_menu_requester.request_context_menu(
        context_menu_type(
            scene_interactor.object_selection(),
            project_interactor.selected_project()
        ),
        Domain::Vec2f(mouse_pos.x, mouse_pos.y)
    );
}

} // namespace Slic3r::App::ObjectList
