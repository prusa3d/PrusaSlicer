#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"
#include "Slic3r/App/ObjectList/ObjectItem.hpp"

#include "Slic3r/Biz/ObjectList/ConfigContainerBedObservableList.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/BedRef.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Yoga {
class Icon;
class Text;
} // namespace Yoga

namespace ObjectList {

using RemoveBedFn = std::function<void(size_t config_container_id, size_t bed_id)>;

/**
 * @brief Compact status badge for a bed row (SLICED / SLICING / UPDATING / STOPPING / INVALID),
 * or - in Preview mode when the bed's data is Modified - a clickable "SLICE" action.
 * Mirrors ObjectListOld::render_slicing_state_marker.
 */
class SlicingStateMarker : public Yoga::RectangleButton
{
public:
    SlicingStateMarker();

    void set_status(std::optional<Biz::Slicing::StatusCode> status_code, bool show_slice_button);
    void set_callbacks(std::function<void()> on_slice_clicked);

private:
    Yoga::Text* m_label{nullptr};
    std::function<void()> m_on_slice_clicked;
};

/**
 * @brief Displays a live-rendered bed thumbnail (Render::TexturePtr) in place of a generic
 * icon, mirroring ObjectListOld's use of Plater::BedThumbnailTexture as the bed row's tex_id.
 * Falls back to nothing drawn (letting the sibling generic Bed icon show through) until a
 * thumbnail has been pushed via set_texture.
 */
class BedThumbnailIcon : public Yoga::Item
{
public:
    void set_texture(Render::TexturePtr texture);

protected:
    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    Render::TexturePtr m_texture;
};

class CCBedButton : public TreeNodeButton
{
public:
    CCBedButton(CollapsibleArea* collapsible_item, std::function<void()>&& remove_bed_fn);

    void set_name(const std::string& name);
    void set_remove_button_visible(bool visible);
    void set_selected(bool selected);
    void set_selection_callbacks(
        Biz::Scene::SceneInteractor& scene_interactor,
        std::function<Domain::BedRef()> get_bed_ref
    );
    void set_thumbnail(Render::TexturePtr texture);

    SlicingStateMarker* slicing_state_marker() const
    {
        return m_slicing_state_marker;
    }

private:
    void hovered_updated_internal() override;
    void update_remove_button_visibility();

    Yoga::Icon* m_icon{nullptr};
    BedThumbnailIcon* m_thumbnail_icon{nullptr};
    Yoga::Text* m_name{nullptr};
    SlicingStateMarker* m_slicing_state_marker{nullptr};
    ActionButton* m_remove_button{nullptr};
    std::function<void()> m_remove_bed_fn{nullptr};
    bool m_is_visible_remove_button{false};
};

/**
 * @brief A per-bed row (owned by ConfigContainerBedItem, not per-object) representing the
 * bed's wipe tower, mirroring ObjectListOld::render_wipe_tower_node.
 */
class WipeTowerButton : public LeafButton
{
public:
    WipeTowerButton();

    void set_selected(bool selected);
    void set_callbacks(
        Biz::ProjectInteractor& project_interactor,
        std::function<Biz::Scene::ObjectSelection()> get_selection
    );

private:
    Yoga::Icon* m_icon{nullptr};
};

class ConfigContainerBedItem : public Biz::DataObserver<Biz::ObjectList::Bed>, public Yoga::Item
{
public:
    ConfigContainerBedItem(
        size_t index,
        const Biz::ObjectList::Bed& state,
        Biz::ProjectInteractor& project_interactor,
        RemoveBedFn m_remove_bed_fn,
        Mode mode
    );

    size_t bed_id() const
    {
        return state()->id;
    }

    void set_thumbnail(Render::TexturePtr texture);

private:
    void on_data_update() override;
    void refresh_from_state();

    using ObjectListViewFactory = Yoga::
        ViewFactory<ObjectItem, Biz::ObjectList::Object, Biz::ProjectInteractor&>;
    using ObjectListView =
        Yoga::ListView<ObjectItem, Biz::ObjectList::Object, ObjectListViewFactory>;

    CCBedButton* m_cc_bed_button{nullptr};
    CollapsibleArea* m_collapsible_content{nullptr};
    WipeTowerButton* m_wipe_tower_button{nullptr};

    ObjectListView* m_objects_list_view{nullptr};

    RemoveBedFn m_remove_bed_fn{nullptr};

    Biz::ProjectInteractor& m_project_interactor;
    Mode m_mode;
};

} // namespace ObjectList
} // namespace Slic3r::App
