#pragma once

#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <optional>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ObjectList {

class ConfigContainerObservableList;
class ObjectObservableList;
struct Object;

/** @brief Keyboard modifier held while clicking an object list row. */
enum class ClickModifier
{
    None,
    Ctrl,
    Shift,
};

/** @brief Identifies one clickable row of the object list. */
struct ListRow
{
    enum class Kind
    {
        Bed,
        WipeTower,
        Object,
        Connectors,
        Volumes,
        Volume,
        Instances,
        Instance,
    };

    Kind kind{Kind::Object};
    /** @brief Bed the row is listed under (the bed itself for Bed), nullopt for "Out of bed". */
    std::optional<size_t> bed_id;
    size_t object_id{0};
    size_t instance_id{0};
    size_t volume_id{0};

    bool operator==(const ListRow&) const = default;
};

/**
 * @brief Applies a click on an object list row to the bed and object selections of a project.
 *
 * - A plain click selects just the row (a bed click changes only the bed selection).
 * - Ctrl+click adds the row to the selection, or removes it if it is already selected. Volume
 *   rows can only be combined with volumes of the same object, other rows only with each other.
 * - Shift+click selects the rows between the anchor (the last plain/Ctrl-clicked row) and the
 *   clicked row, in the order they are listed: objects, the clicked bed with all its objects, the
 *   clicked instances (not their object), the parent object of a clicked volume and the clicked
 *   wipe tower. Every object adds only its instances listed under its bed. Between two beds, it
 *   also selects the beds, but not the instances of the last bed in the list. Starting from a
 *   volume, only volumes of the same object can be added. Without a selected anchor it acts as
 *   a plain click.
 *
 * Lives only for one click: the list snapshot it walks is taken at construction.
 */
class ObjectListSelector
{
public:
    ObjectListSelector(
        Biz::ProjectInteractor& project_interactor,
        Domain::SelectionId project_id,
        const ConfigContainerObservableList& printer_groups,
        const ObjectObservableList& out_of_bed_objects
    );

    /**
     * @param anchor The row a Shift+click ranges from, set to @p row by a plain or Ctrl click.
     */
    void select(const ListRow& row, ClickModifier modifier, std::optional<ListRow>& anchor);

private:
    /** @brief A row of the list order a Shift+click range is taken from. */
    struct Slot
    {
        ListRow::Kind kind;
        std::optional<Domain::BedRef> bed;
        const Object* object{nullptr};
    };

    void select_one(const ListRow& row);
    /** @return False if the row can't be combined with the current selection. */
    bool toggle(const ListRow& row);
    void select_range(const ListRow& anchor, const ListRow& row);
    void select_volume_range(const ListRow& anchor, const ListRow& row);
    /** @brief Selects @p beds (if any, @p last_bed is the last selected one) and @p refs. */
    void apply_range(
        Domain::BedRefs beds,
        Domain::ElementRefs refs,
        const std::optional<Domain::BedRef>& last_bed
    );

    std::optional<size_t> slot_index(const ListRow& row) const;
    /** @param volumes_instance_id Instance the volume elements are bound to (0 = the first). */
    Domain::ElementRefs elements(const ListRow& row, size_t volumes_instance_id = 0) const;
    /** @brief Instances of @p object placed on the bed it is listed under. */
    Domain::ElementRefs bed_instances(const Object& object) const;
    /** @brief Elements the end @p row of a Shift+click range adds, @p other is the other end. */
    Domain::ElementRefs range_elements(
        const ListRow& row,
        size_t row_index,
        const ListRow& other,
        size_t other_index
    ) const;
    bool is_selected(const ListRow& row) const;

    Biz::ProjectInteractor& m_project_interactor;
    Domain::SelectionId m_project_id;
    std::vector<Slot> m_slots;
};

} // namespace Slic3r::Biz::ObjectList
