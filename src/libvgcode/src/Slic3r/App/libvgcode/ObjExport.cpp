
#include "Slic3r/App/libvgcode/ObjExport.hpp"
#include "Slic3r/App/libvgcode/Utils.hpp"
#include "Slic3r/App/libvgcode/FdmViewer.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::Vec3f;

using namespace Slic3r::Biz::libpgcode;

namespace Slic3r::App::libvgcode {

enum class EObjExportFlag : uint8_t
{
    First    = 0x01,
    Last     = 0x02,
    Internal = 0x04
};

enum class ECornerType : uint8_t
{
    RightTurn = 1,
    LeftTurn,
    Straight
};

struct SegmentLocalAxes
{
    Vec3f forward;
    Vec3f right;
    Vec3f up;
};

struct Vertex
{
    Vec3f position;
    Vec3f normal;
};

struct CrossSection
{
    Vertex right;
    Vertex top;
    Vertex left;
    Vertex bottom;
};

static SegmentLocalAxes segment_local_axes(const Vec3f& v1, const Vec3f& v2)
{
    SegmentLocalAxes ret;
    ret.forward = (v2 - v1).normalized();
    ret.right = ret.forward.cross(Vec3f::UnitZ()).normalized();
    ret.up = ret.right.cross(ret.forward).normalized();
    return ret;
}

static CrossSection cross_section(const Vec3f& v, const Vec3f& right, const Vec3f& up, float width, float height)
{
    CrossSection ret;
    Vec3f w_shift = 0.5f * width * right;
    Vec3f h_shift = 0.5f * height * up;
    ret.right.position = v + w_shift;
    ret.right.normal = right;
    ret.top.position = v + h_shift;
    ret.top.normal = up;
    ret.left.position = v - w_shift;
    ret.left.normal = -right;
    ret.bottom.position = v - h_shift;
    ret.bottom.normal = -up;
    return ret;
}

static CrossSection normal_cross_section(const Vec3f& v, const SegmentLocalAxes& axes, float width, float height)
{
    return cross_section(v, axes.right, axes.up, width, height);
}

static CrossSection corner_cross_section(const Vec3f& v, const SegmentLocalAxes& axes1, const SegmentLocalAxes& axes2,
    float width, float height, uint8_t& corner_type)
{
    if (std::abs(std::abs(axes1.forward.dot(axes2.forward)) - 1.0f) < 0.001f)
        corner_type = uint8_t(ECornerType::Straight);
    else if (axes1.up.dot(axes1.forward.cross(axes2.forward)) < 0.0f)
        corner_type = uint8_t(ECornerType::RightTurn);
    else
        corner_type = uint8_t(ECornerType::LeftTurn);
    return cross_section(v, (0.5f * (axes1.right + axes2.right)).normalized(), axes1.up, width, height);
}

static size_t color_id(size_t vertex_id, const FdmViewer& viewer, std::vector<ColorRGB>& colors) {
    const MoveVertex& v = viewer.vertex_at(vertex_id);
    size_t top_layer_id = viewer.is_top_layer_only_view_range() ? viewer.layers_range()[1] : 0;
    bool color_top_layer_only = viewer.view_full_range()[1] != viewer.view_visible_range()[1];
    ColorRGB color = (color_top_layer_only && v.layer_id < top_layer_id &&
        (!viewer.is_spiral_vase_enabled() || vertex_id != viewer.view_enabled_range()[0])) ?
        DUMMY_COLOR : viewer.vertex_color(v);
    auto color_it = std::find_if(colors.begin(), colors.end(), [&color](const ColorRGB& m) { return m == color; });
    if (color_it == colors.end()) {
        colors.emplace_back(color);
        color_it = std::prev(colors.end());
    }
    return std::distance(colors.begin(), color_it);
}

static void export_vertex(FILE& f, const Vertex& v)
{
    fprintf(&f, "v %g %g %g\n", v.position.x(), v.position.y(), v.position.z());
    fprintf(&f, "vn %g %g %g\n", v.normal.x(), v.normal.y(), v.normal.z());
}

static void export_triangle(FILE& f, size_t v1, size_t v2, size_t v3)
{
    fprintf(&f, "f %zu//%zu %zu//%zu %zu//%zu\n", v1, v1, v2, v2, v3, v3);
}

static void export_material(FILE& f, size_t material_id)
{
    fprintf(&f, "\nusemtl material_%zu\n", material_id + 1);
}

static void export_segment(FILE& f, uint8_t flags, const FdmViewer& viewer, const ObjExportParams& params, size_t v1_id,
    const MoveVertex& v1, const MoveVertex& v2, const MoveVertex& v3, size_t& vertices_count, Palette& colors) {

    auto vertex_id = [&vertices_count](int id) { return size_t(1 + int(vertices_count) + id); };

    SegmentLocalAxes v1_v2 = segment_local_axes(v1.position, v2.position);
    SegmentLocalAxes v2_v3 = segment_local_axes(v2.position, v3.position);

    // starting cap
    if ((flags & uint8_t(EObjExportFlag::First)) > 0) {
        Vertex v0 = { v1.position - params.cap_rounding_factor * v1.width * v1_v2.forward, -v1_v2.forward };
        CrossSection ncs = normal_cross_section(v1.position, v1_v2, v1.width, v1.height);
        export_vertex(f, v0);            // 0
        export_vertex(f, ncs.right);  // 1
        export_vertex(f, ncs.top);    // 2
        export_vertex(f, ncs.left);   // 3
        export_vertex(f, ncs.bottom); // 4
        export_material(f, color_id(v1_id, viewer, colors));
        export_triangle(f, vertex_id(0), vertex_id(1), vertex_id(2));
        export_triangle(f, vertex_id(0), vertex_id(2), vertex_id(3));
        export_triangle(f, vertex_id(0), vertex_id(3), vertex_id(4));
        export_triangle(f, vertex_id(0), vertex_id(4), vertex_id(1));
        vertices_count += 5;
    }
    // segment body + ending cap
    if ((flags & uint8_t(EObjExportFlag::Last)) > 0) {
        Vertex v0 = { v2.position + params.cap_rounding_factor * v2.width * v1_v2.forward, v1_v2.forward };
        CrossSection ncs = normal_cross_section(v2.position, v1_v2, v2.width, v2.height);
        export_vertex(f, v0);            // 0
        export_vertex(f, ncs.right);  // 1
        export_vertex(f, ncs.top);    // 2
        export_vertex(f, ncs.left);   // 3
        export_vertex(f, ncs.bottom); // 4
        export_material(f, color_id(v1_id + 1, viewer, colors));
        // segment body
        export_triangle(f, vertex_id(-4), vertex_id(1), vertex_id(2));
        export_triangle(f, vertex_id(-4), vertex_id(2), vertex_id(-3));
        export_triangle(f, vertex_id(-3), vertex_id(2), vertex_id(3));
        export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(-2));
        export_triangle(f, vertex_id(-2), vertex_id(3), vertex_id(4));
        export_triangle(f, vertex_id(-2), vertex_id(4), vertex_id(-1));
        export_triangle(f, vertex_id(-1), vertex_id(4), vertex_id(1));
        export_triangle(f, vertex_id(-1), vertex_id(1), vertex_id(-4));
        // ending cap
        export_triangle(f, vertex_id(0), vertex_id(3), vertex_id(2));
        export_triangle(f, vertex_id(0), vertex_id(2), vertex_id(1));
        export_triangle(f, vertex_id(0), vertex_id(1), vertex_id(4));
        export_triangle(f, vertex_id(0), vertex_id(4), vertex_id(3));
        vertices_count += 5;
    }
    else {
        uint8_t corner_type = 0;
        CrossSection ccs = corner_cross_section(v2.position, v1_v2, v2_v3, v2.width, v2.height, corner_type);
        CrossSection ncs12 = normal_cross_section(v2.position, v1_v2, v2.width, v2.height);
        CrossSection ncs23 = normal_cross_section(v2.position, v2_v3, v2.width, v2.height);
        if (corner_type == uint8_t(ECornerType::Straight)) {
            export_vertex(f, ncs12.right);  // 0
            export_vertex(f, ncs12.top);    // 1
            export_vertex(f, ncs12.left);   // 2
            export_vertex(f, ncs12.bottom); // 3
            export_material(f, color_id(v1_id + 1, viewer, colors));
            // segment body
            export_triangle(f, vertex_id(-4), vertex_id(0), vertex_id(1));
            export_triangle(f, vertex_id(-4), vertex_id(1), vertex_id(-3));
            export_triangle(f, vertex_id(-3), vertex_id(1), vertex_id(2));
            export_triangle(f, vertex_id(-3), vertex_id(2), vertex_id(-2));
            export_triangle(f, vertex_id(-2), vertex_id(2), vertex_id(3));
            export_triangle(f, vertex_id(-2), vertex_id(3), vertex_id(-1));
            export_triangle(f, vertex_id(-1), vertex_id(3), vertex_id(0));
            export_triangle(f, vertex_id(-1), vertex_id(0), vertex_id(-4));
            vertices_count += 4;
        }
        else if (corner_type == uint8_t(ECornerType::RightTurn)) {
            export_vertex(f, ncs12.left);   // 0
            export_vertex(f, ccs.left);     // 1
            export_vertex(f, ccs.right);    // 2
            export_vertex(f, ncs12.top);    // 3
            export_vertex(f, ncs23.left);   // 4
            export_vertex(f, ncs12.bottom); // 5
            export_material(f, color_id(v1_id + 1, viewer, colors));
            // segment body
            export_triangle(f, vertex_id(-4), vertex_id(2), vertex_id(3));
            export_triangle(f, vertex_id(-4), vertex_id(3), vertex_id(-3));
            export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(0));
            export_triangle(f, vertex_id(-3), vertex_id(0), vertex_id(-2));
            export_triangle(f, vertex_id(-2), vertex_id(0), vertex_id(5));
            export_triangle(f, vertex_id(-2), vertex_id(5), vertex_id(-1));
            export_triangle(f, vertex_id(-1), vertex_id(5), vertex_id(2));
            export_triangle(f, vertex_id(-1), vertex_id(2), vertex_id(-4));
            // corner
            export_triangle(f, vertex_id(1), vertex_id(0), vertex_id(3));
            export_triangle(f, vertex_id(1), vertex_id(3), vertex_id(4));
            export_triangle(f, vertex_id(1), vertex_id(4), vertex_id(5));
            export_triangle(f, vertex_id(1), vertex_id(5), vertex_id(0));
            vertices_count += 6;
        }
        else {
            export_vertex(f, ncs12.right);  // 0
            export_vertex(f, ccs.right);    // 1
            export_vertex(f, ncs23.right);  // 2
            export_vertex(f, ncs12.top);    // 3
            export_vertex(f, ccs.left);     // 4
            export_vertex(f, ncs12.bottom); // 5
            export_material(f, color_id(v1_id + 1, viewer, colors));
            // segment body
            export_triangle(f, vertex_id(-4), vertex_id(0), vertex_id(3));
            export_triangle(f, vertex_id(-4), vertex_id(3), vertex_id(-3));
            export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(4));
            export_triangle(f, vertex_id(-3), vertex_id(4), vertex_id(-2));
            export_triangle(f, vertex_id(-2), vertex_id(4), vertex_id(5));
            export_triangle(f, vertex_id(-2), vertex_id(5), vertex_id(-1));
            export_triangle(f, vertex_id(-1), vertex_id(5), vertex_id(0));
            export_triangle(f, vertex_id(-1), vertex_id(0), vertex_id(-4));
            // corner
            export_triangle(f, vertex_id(1), vertex_id(2), vertex_id(3));
            export_triangle(f, vertex_id(1), vertex_id(3), vertex_id(0));
            export_triangle(f, vertex_id(1), vertex_id(0), vertex_id(5));
            export_triangle(f, vertex_id(1), vertex_id(5), vertex_id(2));
            vertices_count += 6;
        }
    }
}

static void export_materials(FILE& f, const Palette& colors) {
    size_t materials_counter = 0;
    for (const auto& color : colors) {
        fprintf(&f, "\nnewmtl material_%zu\n", ++materials_counter);
        fprintf(&f, "Ka 1 1 1\n");
        fprintf(&f, "Kd %g %g %g\n", color.r(), color.g(), color.b());
        fprintf(&f, "Ks 0 0 0\n");
    }
}

bool export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params, const FdmViewer& viewer)
{
    try {
        // write header to geometry file
        fprintf(&obj_file, "# G-Code Toolpaths\n");
        fprintf(&obj_file, "# Generated by %s-%s\n", params.app_name.c_str(), params.app_version.c_str());
        fprintf(&obj_file, "\nmtllib ./%s\n", params.materials_filename.c_str());

        // write header to material file
        fprintf(&mtl_file, "# G-Code Toolpaths Materials\n");
        fprintf(&mtl_file, "# Generated by %s-%s\n", params.app_name.c_str(), params.app_version.c_str());

        // write geometry
        Palette colors;
        size_t vertices_count = 0;
        Interval visible_range = viewer.view_visible_range();
        if (viewer.is_top_layer_only_view_range())
            visible_range[0] = viewer.view_full_range()[0];
        for (size_t i = visible_range[0]; i <= visible_range[1]; ++i) {
            const MoveVertex& curr = viewer.vertex_at(i);
            const MoveVertex& next = viewer.vertex_at(i + 1);
            if (!curr.is_extrusion() || !next.is_extrusion())
                continue;
            const MoveVertex& nextnext = viewer.vertex_at(i + 2);
            uint8_t flags = 0;
            if (curr.gcode_id == next.gcode_id)
                flags |= uint8_t(EObjExportFlag::First);
            if (i + 1 == visible_range[1] || !nextnext.is_extrusion())
                flags |= uint8_t(EObjExportFlag::Last);
            else
                flags |= uint8_t(EObjExportFlag::Internal);
            export_segment(obj_file, flags, viewer, params, i, curr, next, nextnext, vertices_count, colors);
        }

        // write materials
        export_materials(mtl_file, colors);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace Slic3r::App::libvgcode
