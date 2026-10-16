#include "Slic3r/Biz/ObjectList/ObjectListSelection.hpp"

#include "Slic3r/Biz/ObjectList/ConfigContainerObservableList.hpp"
#include "Slic3r/Biz/ObjectList/ConfigContainerBedObservableList.hpp"
#include "Slic3r/Biz/ObjectList/ObjectObservableList.hpp"
#include "Slic3r/Biz/ObjectList/InstanceObservableList.hpp"
#include "Slic3r/Biz/ObjectList/VolumeObservableList.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include <algorithm>

namespace Slic3r::Biz::ObjectList {

using Kind = ListRow::Kind;

namespace {

bool is_volume_row(const ListRow& row)
{
    return row.kind == Kind::Volume || row.kind == Kind::Volumes || row.kind == Kind::Connectors;
}

/** @brief Whether the selected element @p selected is @p element or a part of it. */
bool belongs_to(const Domain::ElementRef& selected, const Domain::ElementRef& element)
{
    if (selected.is_wipe_tower() || element.is_wipe_tower()) {
        return selected == element;
    }
    if (selected.object_id != element.object_id) {
        return false;
    }
    // Volumes are matched regardless of the instance, a volume row isn't bound to any.
    if (element.has_volume()) {
        return selected.volume_id == element.volume_id;
    }
    return !element.has_instance() || selected.instance_id == element.instance_id;
}

bool contains(const Scene::ObjectSelection& selection, const Domain::ElementRef& element)
{
    return std::ranges::any_of(
        selection.elements,
        [&element](const Domain::ElementRef& selected) { return belongs_to(selected, element); }
    );
}

template<class T> void add(std::vector<T>& items, const std::vector<T>& to_add)
{
    for (const T& item : to_add) {
        if (std::ranges::find(items, item) == items.end()) {
            items.push_back(item);
        }
    }
}

} // namespace

ObjectListSelector::ObjectListSelector(
    Biz::ProjectInteractor& project_interactor,
    Domain::SelectionId project_id,
    const ConfigContainerObservableList& printer_groups,
    const ObjectObservableList& out_of_bed_objects
) :
    m_project_interactor(project_interactor),
    m_project_id(project_id)
{
    const auto add_objects =
        [this](const ObjectObservableList& objects, std::optional<Domain::BedRef> bed)
    {
        for (size_t i = 0; i < objects.size(); ++i) {
            m_slots.push_back({Kind::Object, bed, &objects.at(i)});
        }
    };

    for (size_t i = 0; i < printer_groups.size(); ++i) {
        const auto& beds = printer_groups.at(i).beds_list;
        for (size_t j = 0; beds && j < beds->size(); ++j) {
            const Bed& bed = beds->at(j);
            const Domain::BedRef bed_ref{bed.config_container_id, bed.id};
            m_slots.push_back({Kind::Bed, bed_ref});
            if (bed.objects_list) {
                add_objects(*bed.objects_list.operator->(), bed_ref);
            }
            if (bed.has_wipe_tower) {
                m_slots.push_back({Kind::WipeTower, bed_ref});
            }
        }
    }
    add_objects(out_of_bed_objects, std::nullopt);
}

void ObjectListSelector::select(
    const ListRow& row,
    ClickModifier modifier,
    std::optional<ListRow>& anchor
)
{
    switch (modifier) {
    case ClickModifier::Ctrl:
        if (toggle(row)) {
            anchor = row;
        }
        return;
    case ClickModifier::Shift:
        if (anchor && slot_index(*anchor) && is_selected(*anchor)) {
            if (is_volume_row(*anchor)) {
                select_volume_range(*anchor, row);
            } else {
                select_range(*anchor, row);
            }
            return;
        }
        // No usable anchor - acts as a plain click.
        break;
    case ClickModifier::None:
        break;
    }

    select_one(row);
    anchor = row;
}

void ObjectListSelector::select_one(const ListRow& row)
{
    Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();

    if (row.kind == Kind::Bed) {
        if (const std::optional<size_t> index = slot_index(row)) {
            scene_interactor.bed_selection().select_one(
                *m_slots[*index].bed,
                Scene::CameraActionOnBedSelection::CenterOnBed
            );
        }
        return;
    }

    Domain::ElementRefs refs = elements(row);
    if (refs.empty()) {
        return;
    }
    const Scene::SelectionMode mode =
        is_volume_row(row) ? Scene::SelectionMode::Volume : Scene::SelectionMode::Instance;
    scene_interactor.set_object_selection({mode, std::move(refs)});
}

bool ObjectListSelector::toggle(const ListRow& row)
{
    Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const Scene::ObjectSelection& selection  = scene_interactor.object_selection();

    // Volumes can be combined only with volumes of the same object, other rows only with each
    // other (beds are always selected, so a volume can't be added to a bed selection either).
    const bool volume_selection =
        !selection.empty() && selection.mode == Scene::SelectionMode::Volume;
    if (volume_selection != is_volume_row(row)) {
        return false;
    }
    if (volume_selection && row.object_id != selection.elements.front().object_id) {
        return false;
    }

    const std::optional<size_t> index = slot_index(row);
    if (!index) {
        return false;
    }
    if (row.kind == Kind::Bed) {
        scene_interactor.bed_selection()
            .toggle(*m_slots[*index].bed, Scene::CameraActionOnBedSelection::CenterOnBed);
        return true;
    }

    const size_t instance_id = volume_selection ? selection.elements.front().instance_id : 0;
    const Domain::ElementRefs refs = elements(row, instance_id);
    if (refs.empty()) {
        return false;
    }

    Scene::ObjectSelection new_selection = selection;
    if (is_selected(row)) {
        std::erase_if(
            new_selection.elements,
            [&refs](const Domain::ElementRef& selected)
            {
                return std::ranges::any_of(
                    refs,
                    [&selected](const Domain::ElementRef& ref) { return belongs_to(selected, ref); }
                );
            }
        );
    } else {
        add(new_selection.elements, refs);
    }
    scene_interactor.set_object_selection(new_selection);
    return true;
}

void ObjectListSelector::select_range(const ListRow& anchor, const ListRow& row)
{
    const std::optional<size_t> anchor_index = slot_index(anchor);
    const std::optional<size_t> row_index    = slot_index(row);
    if (!anchor_index || !row_index) {
        return;
    }

    Domain::BedRefs beds;
    Domain::ElementRefs refs;
    const auto [first, last] = std::minmax(*anchor_index, *row_index);

    // Between two beds: the beds and only the instances placed on them, except the instances of
    // the last bed in the list.
    if (anchor.kind == Kind::Bed && row.kind == Kind::Bed) {
        for (size_t i = first; i <= last; ++i) {
            const Slot& slot = m_slots[i];
            if (slot.kind == Kind::Bed) {
                add(beds, {*slot.bed});
            } else if (slot.kind == Kind::Object) {
                add(refs, bed_instances(*slot.object));
            }
        }
        apply_range(std::move(beds), std::move(refs), m_slots[*row_index].bed);
        return;
    }

    // Every object of the range adds only the instances listed under its bed.
    // Rows strictly between the ends.
    for (size_t i = first + 1; i < last; ++i) {
        const Slot& slot = m_slots[i];
        if (slot.kind == Kind::Object) {
            add(refs, bed_instances(*slot.object));
        }
    }

    // The ends themselves.
    const auto add_end = [&](const ListRow& end, size_t end_index, const ListRow& other,
                             size_t other_index)
    {
        if (end.kind == Kind::Bed) {
            add(beds, {*m_slots[end_index].bed});
        } else {
            add(refs, range_elements(end, end_index, other, other_index));
        }
    };
    add_end(anchor, *anchor_index, row, *row_index);
    add_end(row, *row_index, anchor, *anchor_index);

    // The clicked bed brings all its objects along.
    if (row.kind == Kind::Bed) {
        const std::optional<Domain::BedRef>& bed = m_slots[*row_index].bed;
        for (size_t i = *row_index + 1; i < m_slots.size() && m_slots[i].bed == bed; ++i) {
            if (m_slots[i].kind == Kind::Object) {
                add(refs, bed_instances(*m_slots[i].object));
            }
        }
    }

    apply_range(
        std::move(beds),
        std::move(refs),
        m_slots[row.kind == Kind::Bed ? *row_index : *anchor_index].bed
    );
}

void ObjectListSelector::apply_range(
    Domain::BedRefs beds,
    Domain::ElementRefs refs,
    const std::optional<Domain::BedRef>& last_bed
)
{
    // Everything is collected before applying it: the selection change refreshes the lists the
    // slots point into.
    Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    if (!beds.empty() && last_bed) {
        scene_interactor.bed_selection().set_state(
            std::move(beds),
            *last_bed,
            Scene::CameraActionOnBedSelection::CenterOnBed
        );
    }
    scene_interactor.set_object_selection({Scene::SelectionMode::Instance, std::move(refs)});
}

void ObjectListSelector::select_volume_range(const ListRow& anchor, const ListRow& row)
{
    if ((row.kind != Kind::Volume && row.kind != Kind::Volumes)
        || row.object_id != anchor.object_id)
    {
        return;
    }

    Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    const Scene::ObjectSelection& selection  = scene_interactor.object_selection();
    const size_t instance_id =
        !selection.empty() && selection.mode == Scene::SelectionMode::Volume ?
        selection.elements.front().instance_id :
        0;

    Domain::ElementRefs refs;
    if (anchor.kind == Kind::Volume && row.kind == Kind::Volume) {
        const std::optional<size_t> index = slot_index(row);
        if (!index) {
            return;
        }
        const VolumeObservableList& volumes = *m_slots[*index].object->volumes_list.operator->();
        std::optional<size_t> anchor_pos;
        std::optional<size_t> row_pos;
        for (size_t i = 0; i < volumes.size(); ++i) {
            if (volumes.at(i).vol_id == anchor.volume_id) {
                anchor_pos = i;
            }
            if (volumes.at(i).vol_id == row.volume_id) {
                row_pos = i;
            }
        }
        if (!anchor_pos || !row_pos) {
            return;
        }
        const auto [first, last] = std::minmax(*anchor_pos, *row_pos);
        for (size_t i = first; i <= last; ++i) {
            refs.emplace_back(row.object_id, instance_id, volumes.at(i).vol_id);
        }
    } else {
        refs = elements(anchor, instance_id);
        add(refs, elements(row, instance_id));
    }

    scene_interactor.set_object_selection({Scene::SelectionMode::Volume, std::move(refs)});
}

std::optional<size_t> ObjectListSelector::slot_index(const ListRow& row) const
{
    const Kind slot_kind =
        row.kind == Kind::Bed || row.kind == Kind::WipeTower ? row.kind : Kind::Object;
    for (size_t i = 0; i < m_slots.size(); ++i) {
        const Slot& slot = m_slots[i];
        const std::optional<size_t> bed_id =
            slot.bed ? std::optional<size_t>(slot.bed->instance_id) : std::nullopt;
        if (slot.kind == slot_kind
            && bed_id == row.bed_id
            && (slot_kind != Kind::Object || slot.object->id == row.object_id))
        {
            return i;
        }
    }
    return std::nullopt;
}

Domain::ElementRefs ObjectListSelector::elements(const ListRow& row, size_t volumes_instance_id)
    const
{
    switch (row.kind) {
    case Kind::Bed: return {};
    case Kind::WipeTower:
        ASSERT(row.bed_id);
        return {Domain::ElementRef(Domain::SlicingId{m_project_id, *row.bed_id})};
    case Kind::Object: return {Domain::ElementRef{row.object_id}};
    case Kind::Instance: return {Domain::ElementRef{row.object_id, row.instance_id}};
    case Kind::Volume:
        return {Domain::ElementRef{row.object_id, volumes_instance_id, row.volume_id}};
    case Kind::Connectors: {
        Domain::ElementRefs refs;
        const Domain::ModelObject* object =
            m_project_interactor.project(m_project_id).find_object_by_id(row.object_id);
        if (object) {
            for (const Domain::ModelVolume* volume : object->volumes) {
                if (volume->is_cut_connector()) {
                    refs.emplace_back(row.object_id, volumes_instance_id, volume->id().id);
                }
            }
        }
        return refs;
    }
    case Kind::Volumes:
    case Kind::Instances: {
        Domain::ElementRefs refs;
        const std::optional<size_t> index = slot_index(row);
        if (!index) {
            return refs;
        }
        const Object& object = *m_slots[*index].object;
        if (row.kind == Kind::Volumes) {
            for (size_t i = 0; i < object.volumes_list->size(); ++i) {
                refs.emplace_back(
                    row.object_id,
                    volumes_instance_id,
                    object.volumes_list->at(i).vol_id
                );
            }
        } else {
            refs = bed_instances(object);
        }
        return refs;
    }
    }
    return {};
}

Domain::ElementRefs ObjectListSelector::bed_instances(const Object& object) const
{
    Domain::ElementRefs refs;
    for (size_t i = 0; i < object.instances_list->size(); ++i) {
        refs.emplace_back(object.id, object.instances_list->at(i).id);
    }
    return refs;
}

Domain::ElementRefs ObjectListSelector::range_elements(
    const ListRow& row,
    size_t row_index,
    const ListRow& other,
    size_t other_index
) const
{
    // An object or a clicked volume (standing for its parent object) adds the object's instances
    // listed under its bed.
    if (row.kind == Kind::Object || is_volume_row(row)) {
        return bed_instances(*m_slots[row_index].object);
    }
    if (row.kind != Kind::Instance) {
        return elements(row);
    }

    // An instance end spans its object's instances towards the other end.
    const InstanceObservableList& instances =
        *m_slots[row_index].object->instances_list.operator->();
    const auto position = [&instances](size_t instance_id) -> std::optional<size_t>
    {
        for (size_t i = 0; i < instances.size(); ++i) {
            if (instances.at(i).id == instance_id) {
                return i;
            }
        }
        return std::nullopt;
    };

    const std::optional<size_t> row_pos = position(row.instance_id);
    if (!row_pos) {
        return elements(row);
    }
    size_t first = *row_pos;
    size_t last  = *row_pos;
    if (other_index == row_index) {
        // The other end is in the same object: only an instance limits the span.
        const std::optional<size_t> other_pos =
            other.kind == Kind::Instance ? position(other.instance_id) : std::nullopt;
        if (other_pos) {
            std::tie(first, last) = std::minmax(*row_pos, *other_pos);
        }
    } else if (other_index > row_index) {
        last = instances.size() - 1;
    } else {
        first = 0;
    }

    Domain::ElementRefs refs;
    for (size_t i = first; i <= last; ++i) {
        refs.emplace_back(row.object_id, instances.at(i).id);
    }
    return refs;
}

bool ObjectListSelector::is_selected(const ListRow& row) const
{
    const Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();

    if (row.kind == Kind::Bed) {
        const std::optional<size_t> index = slot_index(row);
        return index
            && !scene_interactor.bed_selection().empty()
            && scene_interactor.bed_selection().is_selected(*m_slots[*index].bed);
    }

    const Domain::ElementRefs refs = elements(row);
    if (refs.empty()) {
        return false;
    }
    const Scene::ObjectSelection& selection = scene_interactor.object_selection();
    const auto selected = [&selection](const Domain::ElementRef& ref)
    { return contains(selection, ref); };
    // A group row is selected when all its items are, other rows when any part of them is.
    return row.kind == Kind::Instances || row.kind == Kind::Volumes ?
        std::ranges::all_of(refs, selected) :
        std::ranges::any_of(refs, selected);
}

} // namespace Slic3r::Biz::ObjectList
