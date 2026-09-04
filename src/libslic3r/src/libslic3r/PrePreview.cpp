#include <span>

#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "Slic3r/Biz/libpgcode/Types.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrePreview.hpp"
#include "libslic3r/Layer.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::Biz::Slicing {

MoveVerticesPerLayer merge(MoveVerticesPerLayer a, const MoveVerticesPerLayer& b) {
    MoveVerticesPerLayer result{std::move(a)};

    for (auto &[layer_id, move_vertices] : b) {
        libpgcode::MoveVertices &result_layer{result[layer_id]};
        result_layer.insert(
            result_layer.end(),
            move_vertices.begin(),
            move_vertices.end()
        );
    }

    return result;
}

class WipeTowerHelper
{
public:
    WipeTowerHelper(const WipeTowerData& wipe_tower_data) : m_wipe_tower_data(wipe_tower_data) {
        if (wipe_tower_data.priming) {
            for (size_t i = 0; i < wipe_tower_data.priming.get()->size(); ++i) {
                m_priming.emplace_back(wipe_tower_data.priming.get()->at(i));
            }
        }
        if (wipe_tower_data.final_purge)
            m_final.emplace_back(*wipe_tower_data.final_purge.get());

        m_angle = wipe_tower_data.rotation_angle / 180.0f * PI;
        m_position = wipe_tower_data.position;
        m_layers_count = wipe_tower_data.tool_changes.size() + (m_priming.empty() ? 0 : 1);
    }

    const std::vector<Slic3r::WipeTower::ToolChangeResult>& tool_change(size_t idx) const {
        const auto& tool_changes = m_wipe_tower_data.tool_changes;
        return m_priming.empty() ?
            ((idx == tool_changes.size()) ? m_final : tool_changes[idx]) :
            ((idx == 0) ? m_priming : (idx == tool_changes.size() + 1) ? m_final : tool_changes[idx - 1]);
    }

    float get_angle() const { return m_angle; }
    const Slic3r::Vec2d& get_position() const { return m_position; }
    size_t get_layers_count() const { return m_layers_count; }
    bool is_priming_layer(size_t idx) const { return !m_priming.empty() && idx == 0; }

private:
    const WipeTowerData& m_wipe_tower_data;
    std::vector<Slic3r::WipeTower::ToolChangeResult> m_priming;
    std::vector<Slic3r::WipeTower::ToolChangeResult> m_final;
    Slic3r::Vec2d m_position{ Slic3r::Vec2d::Zero() };
    float m_angle{ 0.0f };
    size_t m_layers_count{ 0 };
};

libpgcode::MoveVertex create_move_vertex(
    const Vec3f& position,
    const float width,
    const float height,
    const libpgcode::MoveVertex& vertex_template
) {
    libpgcode::MoveVertex result{vertex_template};
    result.position = position;
    result.width = width;
    result.height = height;
    return result;
}

libpgcode::MoveVertices convert_lines_to_vertices(
    const Slic3r::Lines& lines,
    const std::vector<float>& widths,
    const std::vector<float>& heights,
    const float print_z,
    const libpgcode::MoveVertex& vertex_template
)
{
    if (lines.empty())
        return {};

    libpgcode::MoveVertices result;
    result.reserve(lines.size() + 1);

    // Add the start point of the first line so the first segment is not lost.
    result.push_back(create_move_vertex(
        to_3d(unscaled(lines[0].a).cast<float>(), print_z), widths[0], heights[0], vertex_template));

    for (size_t i{0}; i < lines.size(); ++i) {
        const Slic3r::Vec3f position{to_3d(unscaled(lines[i].b).cast<float>(), print_z)};
        result.push_back(create_move_vertex(position, widths[i], heights[i], vertex_template));
    }

    return result;
}

using ExtrusionSpan = std::span<const Slic3r::WipeTower::Extrusion>;
struct ExtrusionRange {
    ExtrusionSpan extrusions;
    unsigned tool;
};
using ExtrusionRanges = std::vector<ExtrusionRange>;

ExtrusionRanges get_extrusion_ranges(const ExtrusionSpan extrusions) {
    ExtrusionRanges result;

    std::optional<std::size_t> range_begin;
    std::optional<unsigned> current_tool;
    for (size_t i{}; i < extrusions.size(); ++i) {
        const Slic3r::WipeTower::Extrusion& extrusion{extrusions[i]};
        if (range_begin) {
            if (extrusion.width <= 0.0f || extrusion.tool != current_tool) {
                result.push_back({extrusions.subspan(*range_begin, i - *range_begin), *current_tool});
                range_begin = std::nullopt;
                current_tool = std::nullopt;
            }
        }
        if (!range_begin && extrusion.width > 0.0f) {
            // Include the preceding zero-width (travel) extrusion as the range start
            // so that the segment from the travel endpoint to the first extruded point
            // is not lost. Width of that first segment is taken from the destination
            // extrusion by convert_to_move_vertices, so the zero width of the travel
            // entry does not affect rendering.
            range_begin = (i > 0 && extrusions[i - 1].width <= 0.0f) ? i - 1 : i;
            current_tool = extrusion.tool;
        }
    }
    if (range_begin) {
        result.push_back({extrusions.subspan(*range_begin, extrusions.size() - *range_begin), *current_tool});
    }

    return result;
}

libpgcode::MoveVertices convert_to_move_vertices(
    const ExtrusionRange& range,
    const std::function<Point(Point)>& transformation,
    const float layer_height,
    const float print_z
) {
    using Extrusion = Slic3r::WipeTower::Extrusion;

    if (range.extrusions.size() < 2) {
        return {};
    }

    Slic3r::Lines lines;
    std::vector<float> widths;
    lines.reserve(range.extrusions.size() - 1);
    widths.reserve(range.extrusions.size() - 1);

    Extrusion previous_extrusion{range.extrusions.front()};
    for (const Extrusion& extrusion : range.extrusions.subspan(1)) {
        const Point a{transformation(scaled(previous_extrusion.pos))};
        const Point b{transformation(scaled(extrusion.pos))};
        lines.push_back({a, b});
        widths.emplace_back(extrusion.width);
        previous_extrusion = extrusion;
    }

    const std::vector<float> heights(range.extrusions.size() - 1, layer_height);

    const libpgcode::MoveVertex vertex_template{
        .type = libpgcode::MoveType::Extrude,
        .extrusion_role = GCodeExtrusionRole::WipeTower,
        .extruder_id = static_cast<uint8_t>(range.tool)
    };

    libpgcode::MoveVertices result{convert_lines_to_vertices(
        lines, widths, heights, print_z, vertex_template
    )};

    if (!result.empty()) {
        result.push_back(result.back());
        result.back().type = libpgcode::MoveType::Noop;
    }
    return result;
}

MoveVerticesPerLayer get_wipe_tower_preview(const Slic3r::Print& print)
{
    if (!print.is_step_done(Slic3r::psWipeTower) || !print.wipe_tower_data()) {
        return {};
    }

    const WipeTowerData& wipe_tower_data{*print.wipe_tower_data()};

    MoveVerticesPerLayer result{};
    const WipeTowerHelper wipe_tower_helper{wipe_tower_data};
    const float angle = wipe_tower_helper.get_angle();
    const Slic3r::Vec2d& position = wipe_tower_helper.get_position();

    for (size_t layer_id = 0; layer_id < wipe_tower_helper.get_layers_count(); ++layer_id) {
        const bool is_priming = wipe_tower_helper.is_priming_layer(layer_id);
        const std::vector<Slic3r::WipeTower::ToolChangeResult>& tool_changes = wipe_tower_helper.tool_change(layer_id);
        for (const Slic3r::WipeTower::ToolChangeResult& tool_change : tool_changes) {
            for (const ExtrusionRange& range : get_extrusion_ranges(tool_change.extrusions)) {
                const auto transform = [&](const Point& point) {
                    if (is_priming)
                        return scaled(unscaled(point));
                    return scaled(Vec2d{Eigen::Rotation2Dd(angle) * unscaled(point) + position});
                };
                append(result[scaled(tool_change.print_z)], convert_to_move_vertices(
                    range, transform, tool_change.layer_height, tool_change.print_z
                ));
            }
        }
    }

    return result;
}

libpgcode::MoveVertices path_to_vertices(
    const ExtrusionPath& path,
    const float print_z,
    const libpgcode::MoveVertex& vertex_template,
    const Point &offset = Point::Zero()
) {
    Slic3r::Polyline polyline = path.polyline;
    Algorithms::Polyline::remove_consecutive_duplicate_points(polyline);
    polyline.translate(offset);
    const Slic3r::Lines lines = Algorithms::Polyline::to_lines(polyline);
    const std::vector<float> widths(lines.size(), path.width());
    const std::vector<float> heights(lines.size(), path.height());

    return convert_lines_to_vertices(lines, widths, heights, print_z, vertex_template);
}

template <typename ...Args>
libpgcode::MoveVertices convert_to_vertices(
    const ExtrusionLoop& loop,
    Args&&... args
) {
    libpgcode::MoveVertices result;
    for (const ExtrusionPath& path : loop.paths) {
        append(result, path_to_vertices(path, std::forward<Args>(args)...));
    }
    if (result.empty()){
        return {};
    }
    result.push_back(result.front());
    return result;
}

template <typename ...Args>
libpgcode::MoveVertices convert_to_vertices(
    const ExtrusionMultiPath& multi_path,
    Args&&... args
) {
    libpgcode::MoveVertices result;
    for (const ExtrusionPath& path : multi_path.paths) {
        append(result, path_to_vertices(path, std::forward<Args>(args)...));
    }
    return result;
}

template <typename ...Args>
libpgcode::MoveVertices convert_to_vertices(
    const ExtrusionEntityCollection& extrusion_entity_collection,
    Args&&... args
);

template <typename ...Args>
libpgcode::MoveVertices convert_entity_to_vertices(
    const Slic3r::ExtrusionEntity& extrusion_entity,
    Args&&... args
) {
    libpgcode::MoveVertices result;

    auto* extrusion_path{dynamic_cast<const Slic3r::ExtrusionPath*>(&extrusion_entity)};
    if (extrusion_path != nullptr) {
        result = path_to_vertices(*extrusion_path, std::forward<Args>(args)...);
    } else {
        auto* extrusion_loop = dynamic_cast<const Slic3r::ExtrusionLoop*>(&extrusion_entity);
        if (extrusion_loop != nullptr) {
            result = convert_to_vertices(*extrusion_loop, std::forward<Args>(args)...);
        } else {
            auto* extrusion_multi_path = dynamic_cast<const Slic3r::ExtrusionMultiPath*>(&extrusion_entity);
            if (extrusion_multi_path != nullptr) {
                result = convert_to_vertices(*extrusion_multi_path, std::forward<Args>(args)...);
            } else {
                auto* extrusion_entity_collection = dynamic_cast<const Slic3r::ExtrusionEntityCollection*>(&extrusion_entity);
                if (extrusion_entity_collection != nullptr) {
                    result = convert_to_vertices(*extrusion_entity_collection, std::forward<Args>(args)...);
                } else {
                    throw std::runtime_error{"Unknown entity type!"};
                }
            }
        }
    }

    if (!result.empty()) {
        result.push_back(result.back());
        result.back().type = libpgcode::MoveType::Noop;
    }
    return result;
}

template <typename ...Args>
libpgcode::MoveVertices convert_to_vertices(
    const ExtrusionEntityCollection& collection,
    Args&&... args
)
{
    libpgcode::MoveVertices result;
    for (const Slic3r::ExtrusionEntity* entity : collection.entities) {
        if (entity == nullptr) {
            continue;
        }
        libpgcode::MoveVertices vertices{
            convert_entity_to_vertices(*entity, std::forward<Args>(args)...)
        };
        if (!vertices.empty()) {
            append(result, std::move(vertices));
        }
    }
    return result;
}

std::vector<int> get_skirt_print_zs(const Slic3r::Print& print, std::vector<int>&& print_zs) {
    std::vector<int> result{std::move(print_zs)};

    if (print.has_infinite_skirt()) {
        return result;
    }
    const std::size_t skirt_height{std::max<size_t>(print.config().get<int>("skirt_height"), 0)};
    result.resize(std::min(skirt_height, result.size()));
    return result;
}

MoveVerticesPerLayer get_skirt_preview(
    const Slic3r::Print& print, std::vector<int>&& print_zs
)
{
    if (!print.is_step_done(psSkirtBrim)) {
        return {};
    }

    MoveVerticesPerLayer result;
    const std::vector<int>& skirt_print_zs{get_skirt_print_zs(print, std::move(print_zs))};

    for (const int print_z : skirt_print_zs) {
        const libpgcode::MoveVertex vertex_template{
            .type = libpgcode::MoveType::Extrude,
            .extrusion_role = GCodeExtrusionRole::Skirt,
            .extruder_id = static_cast<uint8_t>(0),
        };
        result[print_z] =
            convert_to_vertices(print.skirt(), unscaled(print_z), vertex_template);
    }
    return result;
}

MoveVerticesPerLayer get_brim_preview(const Slic3r::Print& print, const float height)
{
    if (!print.is_step_done(Slic3r::psSkirtBrim) || print.brim().empty()) {
        return {};
    }

    const libpgcode::MoveVertex vertex_template{
        .type = libpgcode::MoveType::Extrude,
        .extrusion_role = GCodeExtrusionRole::Skirt,
        .extruder_id = static_cast<uint8_t>(0),
    };

    return {
        {scaled(height), convert_to_vertices(print.brim(), height, vertex_template)}
    };
}

void for_each_region_object_layer(
    const PrintObject& object,
    const std::function<bool(const PrintObject&)>& skip_object,
    const std::function<void(const Layer&, const PrintInstance&, const LayerRegion&)>& func
)
{
    if (skip_object(object)) {
        return;
    }
    for (const Layer* layer : object.layers()) {
        for (const Slic3r::PrintInstance& instance : object.instances()) {
            for (const Slic3r::LayerRegion* region : layer->regions()) {
                func(*layer, instance, *region);
            }
        }
    }
}

void for_each_support_layer(
    const PrintObject& object,
    const std::function<bool(const PrintObject&)>& skip_object,
    const std::function<void(const SupportLayer&, const PrintInstance&)>& func
)
{
    if (skip_object(object)) {
        return;
    }
    for (const SupportLayer* layer : object.support_layers()) {
        for (const Slic3r::PrintInstance& instance : object.instances()) {
            func(*layer, instance);
        }
    }
}

MoveVerticesPerLayer get_perimeters_preview(
    const PrintObject& object
)
{
    MoveVerticesPerLayer result;

    for_each_region_object_layer(
        object,
        [](const PrintObject& object) { return !object.is_step_done(Slic3r::posPerimeters); },
        [&](const Layer& layer, const PrintInstance& instance, const LayerRegion& region) {
            if (region.slices().empty())
                return;
            const Slic3r::PrintRegionConfigView& config{region.region().config()};
            const auto extruder_id{
                static_cast<uint8_t>(std::max(config.get<int>("perimeter_extruder") - 1, 0))};

            const libpgcode::MoveVertex vertex_template{
                .type = libpgcode::MoveType::Extrude,
                .extrusion_role = GCodeExtrusionRole::ExternalPerimeter,
                .extruder_id = extruder_id};

            append(result[scaled(layer.print_z)], convert_to_vertices(
                region.perimeters(), layer.print_z, vertex_template, instance.shift()
            ));
        }
    );
    return result;
}

MoveVerticesPerLayer get_infill_preview(
    const PrintObject& object
)
{
    MoveVerticesPerLayer result;

    for_each_region_object_layer(
        object,
        [](const PrintObject& object) { return !object.is_step_done(Slic3r::posInfill); },
        [&](const Layer& layer, const PrintInstance& instance, const LayerRegion& region) {
            for (const Slic3r::ExtrusionEntity* entity : region.fills()) {
                const auto& fill{*dynamic_cast<const Slic3r::ExtrusionEntityCollection*>(entity)};
                if (fill.entities.empty()) {
                    continue;
                }

                const Slic3r::PrintRegionConfigView& config{region.region().config()};
                const bool is_solid_infill = fill.entities.front()->role().is_solid_infill();
                const auto extruder_id = is_solid_infill ?
                    static_cast<uint8_t>(std::max(config.get<int>("solid_infill_extruder") - 1, 0)) :
                    static_cast<uint8_t>(std::max(config.get<int>("infill_extruder") - 1, 0));

                const libpgcode::MoveVertex vertex_template{
                    .type = libpgcode::MoveType::Extrude,
                    .extrusion_role =
                        is_solid_infill ?
                        GCodeExtrusionRole::SolidInfill :
                        GCodeExtrusionRole::InternalInfill,
                    .extruder_id = extruder_id
                };

                append(result[scaled(layer.print_z)],
                    convert_to_vertices(fill, layer.print_z, vertex_template, instance.shift()));
            }
        }
    );

    return result;
}

MoveVerticesPerLayer get_supports_preview(
    const PrintObject& object
)
{
    MoveVerticesPerLayer result;

    for_each_support_layer(
        object,
        [](const PrintObject& object) { return !object.is_step_done(Slic3r::posSupportMaterial); },
        [&](const SupportLayer& support_layer, const PrintInstance& instance) {
            const Slic3r::PrintObjectConfigView& config{support_layer.object()->config()};
            for (const Slic3r::ExtrusionEntity* entity : support_layer.support_fills.entities) {
                const bool is_support_material = entity->role() ==
                    Slic3r::ExtrusionRole::SupportMaterial;
                const auto extruder_id{
                    is_support_material
                        ? static_cast<uint8_t>(
                              std::max(config.get<int>("support_material_extruder") - 1, 0)
                          )
                        : static_cast<uint8_t>(
                              std::max(config.get<int>("support_material_interface_extruder") - 1, 0)
                          )};

                const libpgcode::MoveVertex vertex_template{
                    .type = libpgcode::MoveType::Extrude,
                    .extrusion_role = is_support_material
                        ? GCodeExtrusionRole::SupportMaterial
                        : GCodeExtrusionRole::SupportMaterialInterface,
                    .extruder_id = extruder_id};

                append(result[scaled(support_layer.print_z)], convert_entity_to_vertices(
                    *entity, support_layer.print_z, vertex_template, instance.shift()
                ));
            }
        }
    );

    return result;
}

ObjectInstanceShifts get_object_instance_shifts(const PrintObject& print_object)
{
    ObjectInstanceShifts object_instance_shifts;
    object_instance_shifts.reserve(print_object.instances().size());
    for (const PrintInstance& instance : print_object.instances()) {
        object_instance_shifts.emplace_back(instance.shift());
    }

    return object_instance_shifts;
}

std::vector<Vec2f> double_to_float(const std::vector<Vec2d>& src)
{
    std::vector<Vec2f> ret;
    std::transform(src.begin(), src.end(), std::back_inserter(ret), [](const Vec2d& value) {
        return value.cast<float>();
    });
    return ret;
}

PreviewConfig::PreviewConfig(const Slic3r::Print& print)
{
    spiral_vase_enabled = print.config().get<bool>("spiral_vase");
    z_offset            = print.config().get<double>("z_offset");
    max_print_height    = print.config().get<double>("max_print_height");
    bed_shape           = double_to_float(print.config().get<std::vector<Vec2d>>("bed_shape"));
    if (print.custom_gcode()) {
        custom_gcode = print.custom_gcode()->get();
    }
    material_slot_count = print.config().hw_config().material_slot_count();
    extruder_colors = print.config().get<std::vector<std::string>>("extruder_colour");
}

PrePreview::PrePreview(const Slic3r::Print& print) : m_prepreview_config{print}
{
    for (const PrintObject* object : print.objects()) {
        ObjectPreview& object_preview{m_object_previews[object]};
        object_preview.perimeters      = get_perimeters_preview(*object);
        object_preview.infill          = get_infill_preview(*object);
        object_preview.supports        = get_supports_preview(*object);
        object_preview.instance_shifts = get_object_instance_shifts(*object);
    }

    const auto layer_height{print.config().get<double>("layer_height")};
    const auto brim_height{static_cast<float>(print.config().get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(layer_height))};
    m_brim_preview = get_brim_preview(print, brim_height);
    m_wipe_tower_preview = get_wipe_tower_preview(print);

    // TODO skirt
    m_skirt_preview = get_skirt_preview(print, {});
}

PrePreview::~PrePreview() = default;

bool PrePreview::invalidate(const Slic3r::Print& print)
{
    std::set<const PrintObject*> valid_objects{print.objects().begin(), print.objects().end()};

    bool invalidated{false};

    const std::size_t erase_count{std::erase_if(
        m_object_previews,
        [&](const auto& pair) { return !valid_objects.contains(pair.first); }
    )};
    if (erase_count > 0) {
        invalidated = true;
    }

    for (auto& [object, object_preview] : m_object_previews) {
        if (!object_preview.perimeters.empty()
            && !object->is_step_done(FDMPrintObjectStep::posPerimeters))
        {
            object_preview.perimeters = {};
            invalidated               = true;
        }
        if (!object_preview.infill.empty() && !object->is_step_done(FDMPrintObjectStep::posInfill)) {
            object_preview.infill = {};
            invalidated           = true;
        }
        if (!object_preview.supports.empty()
            && !object->is_step_done(FDMPrintObjectStep::posSupportMaterial))
        {
            object_preview.supports = {};
            invalidated             = true;
        }
    }

    for (auto& [object, object_preview] : m_object_previews) {
        const ObjectInstanceShifts current_shifts{get_object_instance_shifts(*object)};
        if (object_preview.instance_shifts != current_shifts) {
            object_preview.perimeters      = {};
            object_preview.infill          = {};
            object_preview.supports        = {};
            object_preview.instance_shifts = current_shifts;
            invalidated                    = true;
        }
    }

    if (!m_brim_preview.empty() && !print.is_step_done(FDMPrintStep::psSkirtBrim)) {
        m_brim_preview = {};
        invalidated    = true;
    }
    if (!m_skirt_preview.empty() && !print.is_step_done(FDMPrintStep::psSkirtBrim)) {
        m_skirt_preview = {};
        invalidated     = true;
    }
    if (!m_wipe_tower_preview.empty() && !print.is_step_done(FDMPrintStep::psSkirtBrim)) {
        m_wipe_tower_preview = {};
        invalidated          = true;
    }

    const PreviewConfig new_config{print};

    if (m_prepreview_config != new_config) {
        m_prepreview_config = new_config;
        invalidated         = true;
    }

    return invalidated;
}

libpgcode::ProcessorResult PrePreview::generate_result() const {
    MoveVerticesPerLayer moves_per_layer;

    for (const auto& [_, moves]:  m_object_previews) {
        moves_per_layer = merge(std::move(moves_per_layer), moves.infill);
        moves_per_layer = merge(std::move(moves_per_layer), moves.perimeters);
        moves_per_layer = merge(std::move(moves_per_layer), moves.supports);
    }
    moves_per_layer = merge(std::move(moves_per_layer), m_brim_preview);
    moves_per_layer = merge(std::move(moves_per_layer), m_wipe_tower_preview);
    moves_per_layer = merge(std::move(moves_per_layer), m_skirt_preview);

    libpgcode::ProcessorResult result;
    result.producer = libpgcode::GCodeProducer::PrusaSlicer;
    result.spiral_vase_enabled = m_prepreview_config.spiral_vase_enabled;
    result.z_offset = m_prepreview_config.z_offset;
    result.max_print_height = m_prepreview_config.max_print_height;
    result.bed_shape = m_prepreview_config.bed_shape;
    if (m_prepreview_config.custom_gcode) {
        result.custom_gcode_per_print_z = m_prepreview_config.custom_gcode->gcodes;
    }
    result.extruders_count = m_prepreview_config.material_slot_count;

    auto* basic_print_stats{
        std::get_if<Domain::BasicPrintStatistics>(&result.print_statistics)
    };
    ASSERT(basic_print_stats);

    for (uint8_t extruder_id{}; extruder_id < result.extruders_count; ++extruder_id) {
        basic_print_stats->volumes_per_extruder.insert({extruder_id, 0.0});
        result.filament_diameters.push_back(0.0);
        result.filament_densities.push_back(0.0);
    }

    result.extruder_str_colors = m_prepreview_config.extruder_colors;
    result.extruder_str_colors.resize(result.extruders_count);

    std::vector<Domain::CustomGCode::Item> custom_color_gcodes;
    if (m_prepreview_config.custom_gcode) {
        for (const Domain::CustomGCode::Item& code : m_prepreview_config.custom_gcode->gcodes) {
            if (code.type == Domain::CustomGCode::Type::ColorChange) {
                custom_color_gcodes.emplace_back(code);
            }
        }
        DEBUG_ASSERT(std::is_sorted(custom_color_gcodes.begin(), custom_color_gcodes.end()));
    }

    std::size_t layer_id{0};
    for (const auto& [z, moves] : moves_per_layer) {
        const auto it{std::upper_bound(
            custom_color_gcodes.begin(),
            custom_color_gcodes.end(),
            unscaled(z),
            [](double z, const Domain::CustomGCode::Item& custom_gcode)
            { return z < custom_gcode.print_z; }
        )};

        for (const libpgcode::MoveVertex& move : moves) {
            const uint8_t extruder_id{move.extruder_id};

            result.moves().push_back(move);
            result.moves().back().layer_id = layer_id;
            result.moves().back().cp_color_id = extruder_id;

            // custom_color_gcode.empty => begin() == end()
            if (it != custom_color_gcodes.begin()) {
                const auto custom_gcode_it{std::prev(it)};
                const size_t custom_gcode_index{
                    size_t(custom_gcode_it - custom_color_gcodes.begin())
                };
                ASSERT(custom_gcode_it->extruder > 0);
                if (custom_gcode_it->extruder - 1 == extruder_id) {
                    ASSERT(
                        custom_gcode_index >= 0 && custom_gcode_index < custom_color_gcodes.size()
                    );
                    result.moves().back().cp_color_id =
                        result.extruder_str_colors.size() + custom_gcode_index;
                }
            }
        }
        layer_id++;
    }

    for (std::size_t gcode_id{}; gcode_id < result.moves().size(); ++gcode_id) {
        result.moves()[gcode_id].gcode_id = gcode_id;
    }
    return result;
}

}
