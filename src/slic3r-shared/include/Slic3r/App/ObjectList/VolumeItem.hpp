#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"
#include "Slic3r/App/ObjectList/ExtruderMarkerReactor.hpp"

#include "Slic3r/Biz/ObjectList/VolumeObservableList.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {
enum class VolumeInsertPosition : int;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::Domain {
enum class ModelVolumeType : int;
} // namespace Slic3r::Domain

namespace Slic3r::App {

namespace Yoga {
class Icon;
class InputTextField;
} // namespace Yoga

class ExtruderIcon;

namespace ObjectList {

class VolumesButton : public TreeNodeButton
{
public:
    VolumesButton(CollapsibleArea* collapsible_item);
};

class VolumeButton : public Biz::DataObserver<Biz::ObjectList::Volume>, public LeafButton
{
public:
    VolumeButton(
        size_t index,
        const Biz::ObjectList::Volume& state,
        Biz::ProjectInteractor& project_interactor
    );
    void on_data_update() override;

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    /**
     * @brief Insertion line in screen coordinates, lying on the boundary between this row and
     * its upper/lower neighbour.
     */
    struct DropLine
    {
        Yoga::Vec2f start;
        float end_x{0.f};
    };

    /**
     * @return The insertion line to draw while a volume is dragged over this row, if dropping it
     * there changes the volume order.
     * @note Drawn by VolumesItem after all rows are rendered, so no row paints over it.
     */
    const std::optional<DropLine>& drop_line() const;

protected:
    void dnd_accepted_internal(const Yoga::DnDPayload& dnd_payload) override;
    void dnd_could_accept_changed_internal(bool could_accept) override;

private:
    /**
     * @return Whether dropping the volume @p volume_id at @p position relative to this volume
     * changes the volume order.
     */
    bool can_drop_volume(size_t volume_id, Biz::Scene::VolumeInsertPosition position) const;

    /**
     * @brief Updates the drop position from the hovered half of this row and the matching
     * insertion line.
     */
    void update_drop_line(const Yoga::Vec2f& pos, const Yoga::Vec2f& size);

    // type
    Yoga::Icon* m_icon{nullptr};
    Yoga::InputTextField* m_name{nullptr};
    ExtruderIcon* m_extruder_icon{nullptr};
    ExtruderMarkerReactor m_extruder_marker_reactor;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::VolumeInsertPosition m_drop_position;
    bool m_drop_hovered{false};
    std::optional<DropLine> m_drop_line;
};

class VolumesItem : public Yoga::Item
{
public:
    VolumesItem(Biz::ProjectInteractor& project_interactor);
    bool has_visible_content() const;

    void set_source_list(std::weak_ptr<Biz::ObjectList::VolumeObservableList> volumes_list);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void render_drop_line(const VolumeButton::DropLine& drop_line) const;

    using VolumeListViewFactory = Yoga::ViewFactory<
        VolumeButton,
        Biz::ObjectList::Volume,
        Biz::ProjectInteractor&>;
    using VolumeListView = Yoga::
        ListView<VolumeButton, Biz::ObjectList::Volume, VolumeListViewFactory, CollapsibleArea>;

    VolumesButton* m_volumes_button{nullptr};
    VolumeListView* m_volumes_list_view{nullptr};

    size_t m_object_id;
    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace ObjectList
} // namespace Slic3r::App
