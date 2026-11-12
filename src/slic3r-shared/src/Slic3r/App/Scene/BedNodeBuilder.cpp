#include "Slic3r/App/Scene/BedNodeBuilder.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/Biz/Scene/BedGeometry.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"

#include <numbers>
#include <optional>

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

using namespace Slic3r::Biz;

namespace Slic3r::App::Scene {

static double z_offset(BedElementType type)
{
    switch (type) {
    default:
    case BedElementType::Axis: {
        return 0.0 * BED_OFFSET_Z;
    }
    case BedElementType::Label: {
        return 1.0 * BED_OFFSET_Z;
    }
    case BedElementType::Contour:
    case BedElementType::Grid:
    case BedElementType::PrintVolume: {
        return 2.0 * BED_OFFSET_Z;
    }
    case BedElementType::PlateDefault:
    case BedElementType::PlateTextured: {
        return 3.0 * BED_OFFSET_Z;
    }
    case BedElementType::Model: {
        return 4.0 * BED_OFFSET_Z;
    }
    }
}

static void
plate_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, RenderLayerId layer_id)
{
    auto& geom_mgr    = ctx.model_geometry_manager();
    auto& trimesh_mgr = ctx.model_triangle_mesh_manager();

    std::vector<std::pair<Vec3f, Vec2f>> triangles = Biz::Scene::BedGeometry::plate_triangles(bed);
    DEBUG_ASSERT(!triangles.empty());

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedPlate, bed.id().id};
    const auto* geom = geom_mgr.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangles(device, triangles); }
    );
    const auto& trimesh = trimesh_mgr.get_or_create(
        id,
        [&]() -> std::unique_ptr<TriangleMesh>
        {
            Domain::TriangleMesh mesh = Biz::Scene::BedGeometry::plate_mesh(bed);
            return std::make_unique<TriangleMesh>(std::move(mesh.its));
        }
    );

    Render::Material material;
    const BedElementType type{bed.texture_filename().empty() ? BedElementType::PlateDefault : BedElementType::PlateTextured};
    switch (type) {
    case BedElementType::PlateDefault: {
        material = BedMaterials::plate_default_material(device);
        break;
    }
    case BedElementType::PlateTextured: {
        material = BedMaterials::plate_textured_material(device, bed);
        break;
    }
    default:
        PANIC("Unknown type!");
    }

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} plate", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, type, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform(
                    [&](Transform3d& xform) { xform.translate(z_offset(type) * Vec3d::UnitZ()); }
                )
                .set_shadows(Render::Shadows{false, true})
                .set_pbr(DEFAULT_BED_PLATE_PBRPARAMS)
                .set_aabb(trimesh->aabb_mesh());
        }
    );
}

static void
grid_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, RenderLayerId layer_id)
{
    auto& geom_mgr = ctx.model_geometry_manager();

    std::vector<Vec3f> lines = BedRenderHelper::plate_grid(bed);
    DEBUG_ASSERT(!lines.empty());

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedGrid, bed.id().id};
    const auto* geom =
        geom_mgr.get_or_create(id, [&]() { return Render::geometry_from_lines(device, lines); });

    auto material = BedMaterials::grid_material(device);

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} grid", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::Grid, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform([](Transform3d& xform)
                           { xform.translate(z_offset(BedElementType::Grid) * Vec3d::UnitZ()); });
        }
    );
}

static void
contour_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, RenderLayerId layer_id)
{
    auto& geom_mgr = ctx.model_geometry_manager();

    std::vector<Vec3f> lines = Biz::Scene::BedGeometry::plate_contour(bed);
    DEBUG_ASSERT(!lines.empty());

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedContour, bed.id().id};
    const auto* geom =
        geom_mgr.get_or_create(id, [&]() { return Render::geometry_from_lines(device, lines); });

    auto material = BedMaterials::contour_material(device);

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} contour", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::Contour, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform([](Transform3d& xform)
                           { xform.translate(z_offset(BedElementType::Contour) * Vec3d::UnitZ()); });
        }
    );
}

static void
print_volume_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, RenderLayerId layer_id)
{
    auto& geom_mgr = ctx.model_geometry_manager();

    std::vector<Vec3f> lines = Biz::Scene::BedGeometry::print_volume(bed);
    DEBUG_ASSERT(!lines.empty());

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedPrintVolume, bed.id().id};
    const auto* geom =
        geom_mgr.get_or_create(id, [&]() { return Render::geometry_from_lines(device, lines); });

    auto material = BedMaterials::print_volume_material(device);

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} contour", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::PrintVolume, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform(
                    [](Transform3d& xform)
                    { xform.translate(z_offset(BedElementType::PrintVolume) * Vec3d::UnitZ()); }
                );
        }
    );
}

static void
model_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, RenderLayerId layer_id)
{

    auto& geom_mgr    = ctx.model_geometry_manager();
    auto& trimesh_mgr = ctx.model_triangle_mesh_manager();

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedModel, bed.id().id};
    const auto& trimesh = trimesh_mgr.get_or_create(
        id,
        [&]() -> std::unique_ptr<TriangleMesh>
        {
            // copy made intentionally
            Domain::TriangleMesh mesh = Biz::Scene::BedGeometry::model(bed);
            ASSERT(!mesh.empty());
            return std::make_unique<TriangleMesh>(std::move(mesh.its));
        }
    );
    const auto* geom = geom_mgr.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(device, trimesh->triangles()); }
    );

    auto material = BedMaterials::model_material(device);

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} model", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::Model, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform([](Transform3d& xform)
                           { xform.translate(z_offset(BedElementType::Model) * Vec3d::UnitZ()); })
                .set_shadows(Render::Shadows{false, true})
                .set_pbr(DEFAULT_BED_MODEL_PBRPARAMS)
                .set_aabb(trimesh->aabb_mesh());
        }
    );
}

static void
axis_node(uint8_t axis_id, Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag,
    RenderLayerId layer_id)
{
    auto& geom_mgr    = ctx.model_geometry_manager();
    auto& trimesh_mgr = ctx.model_triangle_mesh_manager();

    AuxiliaryElementId id{AuxiliaryElementId::Type::BedAxis, bed.id().id};
    const auto& trimesh = trimesh_mgr.get_or_create(
        id,
        [&]() -> std::unique_ptr<TriangleMesh>
        {
            Domain::TriangleMesh mesh = Biz::Scene::BedGeometry::axis(bed);
            return std::make_unique<TriangleMesh>(std::move(mesh.its));
        }
    );
    const auto* geom = geom_mgr.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(device, trimesh->triangles()); }
    );

    builder.child(
        [&](NodeBuilder& bldr)
        {
            Render::Material material = BedMaterials::axis_material(device, axis_id);

            bldr.set_debug_name(fmt::format("bed {} axis {}", tag.instance_id, axis_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::Axis, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                // add collision geometry to let the axis be taken in account by camera frustum tighting,
                // see: CameraFrustumUpdater::update_camera_frustum
                .set_aabb(trimesh->aabb_mesh())
                .transform(
                    [axis_id](Transform3d& xform)
                    {
                        switch (axis_id) {
                            // X axis
                        case 0: {
                            xform.rotate(Eigen::AngleAxisd(0.5 * std::numbers::pi, Vec3d::UnitY()));
                            break;
                        }
                            // Y axis
                        case 1: {
                            xform.rotate(Eigen::AngleAxisd(-0.5 * std::numbers::pi, Vec3d::UnitX()));
                            break;
                        }
                        default: {
                            break;
                        }
                        }
                    }
                );
        }
    );
}

static void
axes_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const BedNodeTag& tag, int layer_id)
{
    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} axes main", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::AxesMain, tag.is_virtual});

            bldr.child(
                [&](NodeBuilder& in_bldr)
                {
                    in_bldr.set_debug_name(fmt::format("bed {} axes scaler", tag.instance_id))
                        .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::AxesScaler, tag.is_virtual});
                    for (uint8_t i = 0; i < 3; ++i) {
                        axis_node(i, device, ctx, in_bldr, bed, tag, layer_id);
                    }
                }
            );
        }
    );
}

static void
label_node(Render::Device& device, ScenePresenterProjectContext& ctx, NodeBuilder& builder, const Domain::Bed& bed, const Domain::BedInstance& instance,
    const BedNodeTag& tag, RenderLayerId layer_id)
{
    Render::Material material = BedMaterials::label_material(device, instance.label());

    auto& geom_mgr = ctx.model_geometry_manager();

    // adapt label geometry to the extents of the label texture
    DEBUG_ASSERT(!material.textures().empty());
    Render::Texture* tex = material.textures().begin()->second.get();
    DEBUG_ASSERT(tex != nullptr);
    int tex_width      = tex->width();
    int tex_height     = tex->height();
    float label_height = 20.0f;
    float label_width  = label_height * float(tex_width) / float(tex_height);
    std::vector<std::pair<Vec3f, Vec2f>> triangles = Biz::Scene::BedGeometry::label(bed, label_width, label_height);
    DEBUG_ASSERT(!triangles.empty());

    // create unique geometry for each different label size
    // so that label textures, where the label strings are rendered, can share it when they are of the same size
    AuxiliaryElementId id{AuxiliaryElementId::Type::BedLabel, size_t(tex_width * 100000 + tex_height)};
    const auto* geom = geom_mgr.get_or_create(id,
        [&]() { return Render::geometry_from_triangles(device, triangles); }
    );

    Vec3d label_position =
        Biz::Algorithms::Point::to_3d(bed.contour_aabb().min, 0.0) + Vec3d(5.0, 5.0, z_offset(BedElementType::Label));

    builder.child(
        [&](NodeBuilder& bldr)
        {
            bldr.set_debug_name(fmt::format("bed {} label", tag.instance_id))
                .set_tag(BedNodeTag{tag.config_container_id, tag.instance_id, BedElementType::Label, tag.is_virtual})
                .set_mesh(geom, material, layer_id)
                .transform([&](Transform3d& xform)
                    { xform.translate(label_position); });
        }
    );
}

void build_bed_node(NodeBuilder& builder, const Domain::BedInstance& instance, const BedNodeTag& tag, Render::Device& device,
    ScenePresenterProjectContext& ctx, RenderLayerId layer_id)
{
    builder.set_debug_name(fmt::format("bed {}", tag.instance_id))
        .set_tag(tag)
        .transform([&instance](auto& t) { t = instance.matrix(); });

    const Domain::Bed& bed = instance.bed.get();

    plate_node(device, ctx, builder, bed, tag, layer_id);
    if (!bed.model_filename().empty())
        model_node(device, ctx, builder, bed, tag, layer_id);
    if (bed.texture_filename().empty())
        grid_node(device, ctx, builder, bed, tag, layer_id);
    contour_node(device, ctx, builder, bed, tag, layer_id);
    print_volume_node(device, ctx, builder, bed, tag, layer_id);
    axes_node(device, ctx, builder, bed, tag, layer_id);
    label_node(device, ctx, builder, bed, instance, tag, layer_id);
}

void build_virtual_bed_node(NodeBuilder& builder, const Domain::BedInstance& instance,
    std::size_t config_container_id, Render::Device& device,
    ScenePresenterProjectContext& ctx, RenderLayerId layer_id)
{
    BedNodeTag tag{config_container_id, 0, BedElementType::Undefined, /*is_virtual=*/true};
    build_bed_node(builder, instance, tag, device, ctx, layer_id);
}

} // namespace Slic3r::App::Scene
