#include "Slic3r/Biz/Scene/BedFactory.hpp"

#include "Slic3r/Domain/BedContainer.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Biz/Algorithms/Bed.hpp"

namespace Slic3r::Biz::Scene {

Domain::Bed& get_or_create_bed(
    Domain::BedContainer& bed_container,
    const Domain::ConfigContainer& config_container,
    const std::string& assets_path,
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    std::function<Domain::Vec2ds(Domain::SelectionId, Domain::SelectionId)>
        system_preset_bed_shape_getter
)
{
    auto item = config_container.selected_preset().printer.config_box().find("bed_shape");
    ASSERT(item.item != nullptr);
    Domain::Vec2ds bed_shape = item.item->value().get<Domain::Vec2ds>();
    Domain::BedType bed_type = Algorithms::Bed::detect_bed_type_from_contour(bed_shape);
    indexed_triangle_set bed_its = Algorithms::Bed::bed_contour_as_its(bed_shape);
    return bed_container.get_or_create_bed(bed_type, bed_shape, bed_its, config_container, assets_path, project_id, config_container_id,
        system_preset_bed_shape_getter);
}

} // namespace Slic3r::Biz::Scene
