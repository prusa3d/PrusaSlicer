#include "Slic3r/App/ObjectList/ConfigContainerBedItem.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/App/ContextMenu.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <Slic3r/App/Render/Texture.hpp>

#include <imgui/imgui.h>

namespace Slic3r::App::ObjectList {

using namespace Yoga;

void BedThumbnailIcon::set_texture(Render::TexturePtr texture)
{
    m_texture = std::move(texture);
    set_visible(m_texture != nullptr);
}

void BedThumbnailIcon::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    if (m_texture) {
        constexpr ImVec2 uv0{0, 0};
        constexpr ImVec2 uv1{1, 1};
        constexpr ImVec4 bg_color{0, 0, 0, 0};

        ImGui::SetCursorScreenPos(pixel_round(to_im(pos)));
        render_image(m_texture, to_im(size), uv0, uv1, bg_color, ImVec4(1, 1, 1, 1));
    }

    render_item_end(pos, size);
}

CCBedButton::CCBedButton(CollapsibleArea* collapsible_item, std::function<void()>&& remove_bed_fn) :
    TreeNodeButton(NodeLevel::Bed, collapsible_item),
    m_remove_bed_fn(std::move(remove_bed_fn))
{
    set_object_name("CCBedButton");
    m_icon = emplace_back<Icon>(Render::Icon::Bed);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_icon->set_aspect_ratio(1);
    m_icon->set_width(40_fpx);

    m_thumbnail_icon = emplace_back<BedThumbnailIcon>();
    m_thumbnail_icon->set_aspect_ratio(1);
    m_thumbnail_icon->set_width(40_fpx);
    m_thumbnail_icon->set_visible(false);

    m_name = emplace_back<Text>("Bed 1");
    m_name->set_flex_grow(1.f);
    m_name->set_wrap_mode(Text::WrapMode::WrapElide);

    m_remove_button =
        emplace_back<ActionButton>(Render::Icon::DelBedIcon, Biz::_u8L("Remove this bed"));
    m_remove_button->callbacks().action          = m_remove_bed_fn;
    m_remove_button->callbacks().hovered_changed = [this](bool)
    { update_remove_button_visibility(); };

    m_slicing_state_marker = emplace_back<SlicingStateMarker>();
    m_slicing_state_marker->callbacks().hovered_changed = [this](bool)
    { update_remove_button_visibility(); };
}

void CCBedButton::set_name(const std::string& name)
{
    m_name->set_text(name);
}

void CCBedButton::set_remove_button_visible(bool visible)
{
    m_is_visible_remove_button = visible;
    update_remove_button_visibility();
}

void CCBedButton::set_thumbnail(Render::TexturePtr texture)
{
    m_thumbnail_icon->set_texture(texture);
    m_icon->set_visible(texture == nullptr);
}

void CCBedButton::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    update_remove_button_visibility();
}

void CCBedButton::update_remove_button_visibility()
{
    bool any_hovered = hovered() || m_remove_button->hovered() || m_slicing_state_marker->hovered();
    m_remove_button->set_visible(m_is_visible_remove_button && any_hovered);
}

void CCBedButton::set_selected(bool selected)
{
    set_checked(selected);
}

void CCBedButton::set_selection_callbacks(
    Biz::Scene::SceneInteractor& scene_interactor,
    ContextMenuRequester& context_menu_requester,
    std::function<Domain::BedRef()> get_bed_ref
)
{
    callbacks().secondary_action =
        [this, &scene_interactor, &context_menu_requester, get_bed_ref]()
    {
        // A bed which is part of the current selection keeps the selection.
        if (!checked()) {
            scene_interactor.bed_selection()
                .select_one(get_bed_ref(), Biz::Scene::CameraActionOnBedSelection::CenterOnBed);
        }
        const ImVec2 mouse_pos = ImGui::GetMousePos();
        context_menu_requester.request_context_menu(
            ContextMenuType::Bed,
            Domain::Vec2f(mouse_pos.x, mouse_pos.y)
        );
    };

    callbacks().action = [&scene_interactor, get_bed_ref]()
    {
        if (ImGui::GetIO().KeyCtrl) {
            scene_interactor.bed_selection()
                .toggle(get_bed_ref(), Biz::Scene::CameraActionOnBedSelection::CenterOnBed);
        } else {
            scene_interactor.bed_selection()
                .select_one(get_bed_ref(), Biz::Scene::CameraActionOnBedSelection::CenterOnBed);
        }
    };
}

SlicingStateMarker::SlicingStateMarker()
{
    set_object_name("SlicingStateMarker");
    set_self_align(YGAlignCenter);
    set_content_justify_content(YGJustifyCenter);
    set_content_align_items(YGAlignCenter);
    set_content_padding(Paddings(6_fpx, 2_fpx));
    set_rounding(3.f);
    set_flex_shrink(0);
    set_visible(false);

    m_label = emplace_back<Text>(std::string{});
    m_label->set_font_type(Render::ImguiFontType::Bold);
}

void SlicingStateMarker::
    set_status(std::optional<Biz::Slicing::StatusCode> status_code, bool show_slice_button)
{
    using Biz::Slicing::StatusCode;

    if (!status_code || *status_code == StatusCode::Empty) {
        set_visible(false);
        callbacks().action = nullptr;
        return;
    }

    set_visible(true);
    callbacks().action = nullptr;

    m_label->set_font_type(
        *status_code == StatusCode::Finished || *status_code == StatusCode::Modified ?
            Render::ImguiFontType::Bold :
            Render::ImguiFontType::Regular
    );

    switch (*status_code) {
    case StatusCode::Finished:
        m_label->set_text(Biz::_u8L("SLICED"));
        set_background_color(Platform::Color::AccentSecondary);
        break;
    case StatusCode::Updating:
        m_label->set_text(Biz::_u8L("UPDATING"));
        set_background_color(Platform::Color::AccentSecondary);
        break;
    case StatusCode::Stopping:
        m_label->set_text(Biz::_u8L("STOPPING"));
        set_background_color(Platform::Color::AccentSecondary);
        break;
    case StatusCode::Running:
        m_label->set_text(Biz::_u8L("SLICING"));
        set_background_color(Platform::Color::AccentSecondary);
        break;
    case StatusCode::InvalidData:
        m_label->set_text(Biz::_u8L("INVALID"));
        set_background_color(Platform::Color::Error);
        break;
    case StatusCode::Modified:
        if (show_slice_button) {
            m_label->set_text(Biz::_u8L("SLICE"));
            set_background_color(Platform::Color::AccentPrimary);
            callbacks().action = [this]()
            {
                if (m_on_slice_clicked) {
                    m_on_slice_clicked();
                }
            };
        } else {
            set_visible(false);
        }
        break;
    default:
        set_visible(false);
        break;
    }
}

void SlicingStateMarker::set_callbacks(std::function<void()> on_slice_clicked)
{
    m_on_slice_clicked = std::move(on_slice_clicked);
}

WipeTowerButton::WipeTowerButton() : LeafButton(NodeLevel::Object)
{
    set_object_name("WipeTowerButton");

    add_icon(this, Render::Icon::Palette);

    Text* label = emplace_back<Text>(Biz::_u8L("Wipe tower"));
    label->set_flex_grow(1.f);
}

void WipeTowerButton::set_selected(bool selected)
{
    set_checked(selected);
}

void WipeTowerButton::set_callbacks(
    Biz::ProjectInteractor& project_interactor,
    std::function<Biz::Scene::ObjectSelection()> get_selection
)
{
    callbacks().action = [&project_interactor, get_selection]()
    { select_elements(project_interactor, get_selection()); };
}

ConfigContainerBedItem::ConfigContainerBedItem(
    size_t index,
    const Biz::ObjectList::Bed& state,
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester,
    RemoveBedFn remove_bed_fn,
    Mode mode
) :
    Biz::DataObserver<Biz::ObjectList::Bed>(index, state),
    m_remove_bed_fn(remove_bed_fn),
    m_project_interactor(project_interactor),
    m_mode(mode)
{
    set_object_name("ConfigContainerBedItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);

    m_collapsible_content = emplace_back<CollapsibleArea>();
    m_collapsible_content->set_orientation(Orientation::Vertical);
    m_collapsible_content->callbacks().has_content = [this]() -> bool
    { return this->state()->objects_list->size() > 0; };

    m_wipe_tower_button = m_collapsible_content->emplace_back<WipeTowerButton>();
    m_wipe_tower_button->set_callbacks(
        project_interactor,
        [this]() -> Biz::Scene::ObjectSelection
        {
            return {
                Biz::Scene::SelectionMode::Instance,
                {Domain::ElementRef(
                    Domain::SlicingId{m_project_interactor.selected_project_id(), this->state()->id}
                )}
            };
        }
    );

    m_objects_list_view = m_collapsible_content->emplace_back<ObjectListView>(
        ObjectListViewFactory(project_interactor, context_menu_requester)
    );
    m_objects_list_view->set_object_name("ObjectListView");
    m_objects_list_view->set_orientation(Orientation::Vertical);
    m_objects_list_view->set_source_list(state.objects_list.get());

    m_cc_bed_button = emplace<CCBedButton>(
        0,
        m_collapsible_content,

        [this]()
        {
            if (m_remove_bed_fn) {
                m_remove_bed_fn(this->state()->config_container_id, this->state()->id);
            };
        }
    );
    m_cc_bed_button->set_name(state.name);
    m_cc_bed_button->set_selection_callbacks(
        project_interactor.scene_interactor(),
        context_menu_requester,
        [this]() { return Domain::BedRef{this->state()->config_container_id, this->state()->id}; }
    );

    m_cc_bed_button->slicing_state_marker()->set_callbacks(
        [this]()
        {
            m_project_interactor.slicing_interactor().slice_bed(
                {m_project_interactor.selected_project_id(), this->state()->id}
            );
        }
    );
    refresh_from_state();
}

void ConfigContainerBedItem::on_data_update()
{
    refresh_from_state();
    m_cc_bed_button->update_state();
}

void ConfigContainerBedItem::refresh_from_state()
{
    m_cc_bed_button->set_name(state()->name);
    m_cc_bed_button->set_remove_button_visible(state()->can_be_deleted);
    m_cc_bed_button->set_selected(state()->selected);
    m_cc_bed_button->slicing_state_marker()
        ->set_status(state()->status_code, m_mode == Mode::Preview);

    m_wipe_tower_button->set_visible(state()->has_wipe_tower);
    m_wipe_tower_button->set_selected(state()->wipe_tower_selected);

    m_objects_list_view->set_source_list(state()->objects_list.get());
}

void ConfigContainerBedItem::set_thumbnail(Render::TexturePtr texture)
{
    m_cc_bed_button->set_thumbnail(std::move(texture));
}

} // namespace Slic3r::App::ObjectList
