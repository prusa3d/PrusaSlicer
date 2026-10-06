#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"

#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include <imgui/imgui.h>

namespace Slic3r::App::ObjectList {

using namespace Yoga;

ActionButton::ActionButton(Render::Icon icon, const std::string& tooltip) :
    LayoutButton(std::string{}, icon, tooltip)
{
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

namespace {

/**
 * @brief Ordered set of elements that a Shift+click range may span between `anchor` and
 * `target`, or empty if the two aren't part of any recognized rangeable sibling set (different
 * objects, mismatched element "shape", wipe tower involved, ...).
 */
Domain::ElementRefs range_siblings(
    Biz::ProjectInteractor& project_interactor,
    const Domain::ElementRef& anchor,
    const Domain::ElementRef& target
)
{
    if (anchor.is_wipe_tower() || target.is_wipe_tower()) {
        return {};
    }

    // Whole-object rows use volume_id == 0 && instance_id == 0 (mode Volume, no sub-element).
    if (anchor.instance_id == 0
        && anchor.volume_id == 0
        && target.instance_id == 0
        && target.volume_id == 0)
    {
        Domain::ElementRefs siblings;
        for (const Domain::ModelObject* object :
             project_interactor.selected_project().model().objects)
        {
            siblings.emplace_back(object->id().id);
        }
        return siblings;
    }

    if (anchor.object_id != target.object_id) {
        return {};
    }
    const Domain::ModelObject* object =
        project_interactor.selected_project().find_object_by_id(anchor.object_id);
    if (!object) {
        return {};
    }

    // Instance rows: volume_id == 0, instance_id != 0.
    if (anchor.volume_id == 0 && target.volume_id == 0) {
        Domain::ElementRefs siblings;
        for (const Domain::ModelInstance* instance : object->instances) {
            siblings.emplace_back(anchor.object_id, instance->id().id);
        }
        return siblings;
    }

    // Volume rows: volume_id != 0 on both ends (excludes cut connectors, matching
    // VolumeObservableList's own filter - connectors aren't a rangeable list).
    if (anchor.instance_id == 0
        && target.instance_id == 0
        && anchor.volume_id != 0
        && target.volume_id != 0)
    {
        Domain::ElementRefs siblings;
        for (const Domain::ModelVolume* volume : object->volumes) {
            if (!volume->is_cut_connector()) {
                siblings.emplace_back(anchor.object_id, 0, volume->id().id);
            }
        }
        return siblings;
    }

    return {};
}

/**
 * @brief The contiguous sub-range of `siblings` spanning [anchor, target] (order-independent,
 * inclusive), or empty if either endpoint isn't found in `siblings`.
 */
Domain::ElementRefs extract_range(
    const Domain::ElementRefs& siblings,
    const Domain::ElementRef& anchor,
    const Domain::ElementRef& target
)
{
    const auto anchor_it = std::find(siblings.begin(), siblings.end(), anchor);
    const auto target_it = std::find(siblings.begin(), siblings.end(), target);
    if (anchor_it == siblings.end() || target_it == siblings.end()) {
        return {};
    }
    const auto [from, to] =
        anchor_it <= target_it ? std::pair{anchor_it, target_it} : std::pair{target_it, anchor_it};
    return Domain::ElementRefs(from, to + 1);
}

} // namespace

void
select_elements(Biz::ProjectInteractor& project_interactor, Biz::Scene::ObjectSelection selection)
{
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    Domain::ElementRefs& elements = selection.elements;
    if (elements.empty()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyShift && !io.KeyCtrl) {
        Biz::Scene::ObjectSelection old_selection = scene_interactor.object_selection();
        if (old_selection.anchor && (old_selection.empty() || selection.mode == old_selection.mode))
        {
            const Domain::ElementRefs siblings =
                range_siblings(project_interactor, *old_selection.anchor, elements.front());
            const Domain::ElementRefs range =
                extract_range(siblings, *old_selection.anchor, elements.front());
            if (!range.empty()) {
                Biz::Scene::ObjectSelection new_selection{selection.mode, range};
                new_selection.anchor = old_selection.anchor;
                scene_interactor.set_object_selection(new_selection);
                return;
            }
        }
        // No usable anchor, mode mismatch, or anchor/target not part of the same rangeable
        // sibling set - fall through to plain-click behavior below.
    }

    if (io.KeyCtrl) {
        Biz::Scene::ObjectSelection old_selection = scene_interactor.object_selection();
        if (!old_selection.empty()) {
            if (selection.mode != old_selection.mode) {
                return;
            }
            if (old_selection.mode == Biz::Scene::SelectionMode::Volume) {
                size_t obj_id  = old_selection.elements.front().object_id;
                size_t inst_id = old_selection.elements.front().instance_id;
                for (Domain::ElementRef& element : elements) {
                    if (element.object_id != obj_id)
                        return;
                    element.instance_id = inst_id;
                }
            }
        }
        for (const Domain::ElementRef& element : elements) {
            if (!old_selection.remove(element)) {
                old_selection.elements.push_back(element);
            }
        }
        old_selection.anchor = elements.back();
        scene_interactor.set_object_selection(old_selection);
    } else {
        selection.anchor = elements.front();
        scene_interactor.set_object_selection(selection);
    }
}

} // namespace Slic3r::App::ObjectList
