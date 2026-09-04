#pragma once

#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include "Slic3r/Biz/libpgcode/Types.hpp"
#include "libslic3r/Print.hpp"

namespace Slic3r::Biz::Slicing {

using MoveVerticesPerLayer = std::map<int, libpgcode::MoveVertices>;
using ObjectInstanceShifts = std::vector<Domain::Point>;

MoveVerticesPerLayer get_wipe_tower_preview(const Slic3r::Print& print);

MoveVerticesPerLayer get_skirt_preview(
    const Slic3r::Print& print, std::vector<int>&& print_zs
);

MoveVerticesPerLayer get_brim_preview(const Slic3r::Print& print, const float height);

MoveVerticesPerLayer get_perimeters_preview(const PrintObject& object);

MoveVerticesPerLayer get_infill_preview(const PrintObject& object);

MoveVerticesPerLayer get_supports_preview(const PrintObject& object);

struct ObjectPreview
{
    MoveVerticesPerLayer perimeters;
    MoveVerticesPerLayer infill;
    MoveVerticesPerLayer supports;
    ObjectInstanceShifts instance_shifts;
};

struct PreviewConfig {
    PreviewConfig() = default;
    PreviewConfig(const Slic3r::Print& print);

    bool spiral_vase_enabled{};
    double z_offset{};
    double max_print_height{};
    std::vector<Domain::Vec2f> bed_shape;
    std::optional<Domain::CustomGCode::Info> custom_gcode;
    std::size_t material_slot_count{};
    std::vector<std::string> extruder_colors;

    bool operator==(const PreviewConfig&) const = default;
};

class PrePreview {
public:
    PrePreview() = default;
    PrePreview(const Slic3r::Print& print);
    ~PrePreview();

    bool invalidate(const Slic3r::Print& print);
    libpgcode::ProcessorResult generate_result() const;

private:
    std::map<const PrintObject*, ObjectPreview> m_object_previews;
    MoveVerticesPerLayer m_brim_preview;
    MoveVerticesPerLayer m_wipe_tower_preview;
    MoveVerticesPerLayer m_skirt_preview;
    PreviewConfig m_prepreview_config;
};
}
