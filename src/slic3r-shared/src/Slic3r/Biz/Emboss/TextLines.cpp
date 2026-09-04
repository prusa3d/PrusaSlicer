#include "Slic3r/Biz/Emboss/TextLines.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp" // its_make_sphere
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp" // to_linesf
#include "Slic3r/Biz/Algorithms/Polygon.hpp" // count points
#include "Slic3r/Biz/Algorithms/AABBTreeLines.hpp"
#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/KDTreeIndirect.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/Triangulation.hpp"
#include "Slic3r/Domain/ExPolygonsIndex.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <algorithm>

using namespace Slic3r;

namespace {
Domain::BoundingBoxes2crd get_extents(const Domain::Polygons& polygons) {
    Domain::BoundingBoxes2crd bb;
    bb.reserve(polygons.size());
    for (const Domain::Polygon& polygon : polygons)
        bb.emplace_back(Biz::Algorithms::BoundingBox::construct(polygon.points));
    return bb;
}

void add_triangles(std::vector<stl_triangle_vertex_indices>& result,
        const Domain::Polygons& inner, int inner_vertex_offset,
        const Domain::Polygons& outer, int outer_vertex_offset) {
    int outer_count = Biz::Algorithms::Polygon::count_points(outer);
    int inner_count = Biz::Algorithms::Polygon::count_points(inner);
    Domain::BoundingBoxes2crd o_bbs = get_extents(outer);
    Domain::BoundingBoxes2crd i_bbs = get_extents(inner);
    Domain::Polygons inner_rev = inner; // copy
    Biz::Algorithms::Polygon::reverse(inner_rev);
    for (size_t o_i = 0; o_i < outer.size(); ++o_i){
        const Domain::Polygon& o = outer[o_i];
        Domain::ExPolygon expoly(o);
        const Domain::BoundingBox2crd& o_bb = o_bbs[o_i];
        std::vector<int> inner_offsets; // same size as holes
        int inner_offset = inner_vertex_offset;
        for (size_t i_i = 0; i_i < inner.size(); ++i_i) {
            if (o_bb.contains(i_bbs[i_i])) {
                expoly.holes.push_back(inner_rev[i_i]);
                inner_offsets.push_back(inner_offset);
            }
            inner_offset += inner[i_i].points.size();
        }
        auto indices = Biz::CGAL::Algorithms::Triangulation::triangulate(expoly);
        // triangulation hates duplicit and self intersections, but this is only visualization,
        // so we can just skip invalid triangles
        int max_index = static_cast<int>(Biz::Algorithms::ExPolygon::count_points(expoly));
        std::erase_if(indices, [max_index](const Domain::Index3& t) {
            return std::ranges::any_of(t.begin(), t.end(), [max_index](int i) {
                    return i < 0 || i >= max_index; 
                });
        });
        //indices.erase(std::remove_if(indices.begin(), indices.end(), 
        //    [max_index](const Domain::Index3& t) {
        //        for (int i : t) {
        //            if (i < 0 || i >= max_index)
        //                return true;
        //        }
        //        return false;
        //    }), indices.end());
        result.reserve(result.size() + 2 * indices.size());

        auto convert_index = [&expoly, outer_vertex_offset, &inner_offsets](int i)->int{
            if (i < expoly.contour.points.size())
                return outer_vertex_offset + i;
            i -= expoly.contour.points.size();
            for (const Domain::Polygon& hole : expoly.holes) {
                if (i < hole.points.size()) {
                    size_t hole_index = &hole - &expoly.holes.front();
                    // offset of each hole is in inner_offsets
                    // holes order is reverted
                    return inner_offsets[hole_index] + (hole.points.size() - 1 - i);
                }
                i -= hole.points.size();
            }
            ASSERT(false);
            return 0;
        };        
        auto convert_index2 = [&expoly, &convert_index, outer_count, inner_count](int i)->int {
            return convert_index(i) +
                ((i < expoly.contour.points.size()) ?
                outer_count : inner_count);
        };
        auto add_offset = [&convert_index](const Domain::Index3& t) {
            return Domain::Index3{
                convert_index(t[0]),
                convert_index(t[1]),
                convert_index(t[2])};
            };
        auto add_offset_rev = [&convert_index2](const Domain::Index3& t) {
            return Domain::Index3{
                convert_index2(t[2]),
                convert_index2(t[1]),
                convert_index2(t[0])};
            };
        for (const Domain::Index3& t : indices)
            result.emplace_back(add_offset(t));
        for (const Domain::Index3& t : indices)
            result.emplace_back(add_offset_rev(t));
        // update offset for next iteration
        outer_vertex_offset += o.points.size();
    }
}

void add_vertices(std::vector<stl_vertex>& vertices, const Domain::Polygon& polygon, float z) {
    for (const Domain::Point& p : polygon.points)
        vertices.emplace_back(p.x() * Domain::SCALING_FACTOR, p.y() * Domain::SCALING_FACTOR, z);
}

void add_vertices(std::vector<stl_vertex>& vertices, const Domain::Polygons& polys, float z) {
    for (const Domain::Polygon& poly : polys)
        add_vertices(vertices, poly, z);
    for (const Domain::Polygon& poly : polys)
        add_vertices(vertices, poly, -z);
}

// Note: When you want to speed it up use Z coordinate in ClipperLib for index
indexed_triangle_set
its_create_torus(const Domain::Polygon& polygon, float radius, size_t steps = 20)
{
    ASSERT(steps >= 4);
    // quater step count
    int step_q = (steps - 4) / 4;
    double angle_step = M_PI_2 / step_q; // 0 - 90 DEG
    // p_ prefix is used for previous
    // _e suffix is used for expand
    // _s suffix is used for shrink
    Domain::Polygons p_polygons_e{ { polygon } };
    Domain::Polygons p_polygons_s{ { polygon } }; // copy
    
    indexed_triangle_set result;
    result.vertices.reserve(polygon.points.size() * 4 * step_q);
    result.indices.reserve(polygon.points.size() * 2 * 4 * step_q);
    add_vertices(result.vertices, p_polygons_e, radius);
    int p_vert_e_offset = 0;
    int p_vert_s_offset = 0;
    for (int i = 1; i <= step_q; ++i) {
        double angle = i * angle_step;
        float delta = static_cast<float>(radius * std::sin(angle) / Domain::SCALING_FACTOR);
        float offseted_z = static_cast<float>(radius * std::cos(angle));
        Domain::Polygons polygons_e = Biz::Algorithms::ClipperUtils::expand(polygon, delta);
        
        Domain::Polygons polygons_s = Biz::Algorithms::ClipperUtils::offset(polygon, -delta); // shrink

        int vert_e_offset = static_cast<int>(result.vertices.size());
        add_vertices(result.vertices, polygons_e, offseted_z);
        int vert_s_offset = static_cast<int>(result.vertices.size());
        add_vertices(result.vertices, polygons_s, offseted_z);

        add_triangles(result.indices, polygons_s, vert_s_offset, p_polygons_s, p_vert_s_offset);
        add_triangles(result.indices, p_polygons_e, p_vert_e_offset, polygons_e, vert_e_offset);
        
        p_vert_e_offset = vert_e_offset;
        p_vert_s_offset = vert_s_offset;
        p_polygons_e = std::move(polygons_e);
        p_polygons_s = std::move(polygons_s);
    }
    return result;
}

// select closest contour for each line
Biz::Emboss::TextLines select_closest_contour(const std::vector<Domain::Polygons>& line_contours)
{
    Biz::Emboss::TextLines result;
    result.reserve(line_contours.size());
    Domain::Vec2d zero(0., 0.);
    for (const Domain::Polygons& polygons : line_contours) {
        if (polygons.empty()) {
            result.emplace_back();
            continue;
        }
        // Improve: use int values and polygons only
        // Slic3r::Polygons polygons = union_(polygons);
        // std::vector<Slic3r::Line> lines = to_lines(polygons);
        // AABBTreeIndirect::Tree<2, Point> tree;
        // size_t line_idx;
        // Point hit_point;
        // Point::Scalar distance = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, point, line_idx, hit_point);

        Domain::ExPolygons expolygons = Biz::Algorithms::ClipperUtils::union_ex(polygons);
        Domain::Line2ds linesf     = Biz::Algorithms::ExPolygon::to_linesf(expolygons);
        Biz::Algorithms::AABBTreeIndirect::Tree2d tree = Biz::Algorithms::AABBTreeLines::build_aabb_tree_over_indexed_lines(linesf);

        size_t line_idx = 0;
        Domain::Vec2d hit_point;
        // double distance =
        Biz::Algorithms::AABBTreeLines::squared_distance_to_indexed_lines(linesf, tree, zero, line_idx, hit_point);

        // conversion between index of point and expolygon
        Domain::ExPolygonsIndices cvt(expolygons);
        Domain::ExPolygonsIndex index = cvt.cvt(static_cast<uint32_t>(line_idx));

        const Domain::Polygon& polygon = index.is_contour() ?
            expolygons[index.expolygons_index].contour :
            expolygons[index.expolygons_index].holes[index.hole_index()];

        Domain::Point hit_point_int = hit_point.cast<Domain::coord_t>();
        Biz::Emboss::TextLine tl{polygon, Biz::Emboss::PolygonPoint{index.point_index, hit_point_int}};
        result.emplace_back(tl);
    }
    return result;
}

inline Eigen::AngleAxis<double> get_rotation()
{
    return Eigen::AngleAxis(-M_PI_2, Domain::Vec3d::UnitX());
}

indexed_triangle_set create_its(const Biz::Emboss::TextLines& lines, float radius)
{
    indexed_triangle_set its;
    // create model from polygons
    for (const Biz::Emboss::TextLine& line : lines) {
        const Domain::Polygon& polygon = line.polygon;
        if (polygon.empty())
            continue;
        indexed_triangle_set line_its = its_create_torus(polygon, radius);
        auto transl                   = Eigen::Translation3d(0., line.y, 0.);
        Domain::Transform3d tr        = transl * get_rotation();
        its_transform(line_its, tr);
        Domain::its_merge(its, line_its);
    }
    return its;
}

struct TextLineNodeTag {};

// Used to move slice (text line) on place where is approx vertical center of text
// When copy value const double ASCENT_CENTER from Emboss.cpp and Vertical align is center than
// text line will cross object center
const double ascent_ratio_offset = 1 / 3.;

double calc_line_height_in_mm(const Domain::FontFile& ff, const Domain::FontProp& fp)
{
    int line_height = Biz::Emboss::get_line_height(ff, fp); // In shape size
    double scale    = Biz::Emboss::get_text_shape_scale(fp, ff);
    return line_height * scale;
}
} // namespace

namespace Slic3r::Biz::Emboss {
TextLines create_text_lines(
    const Domain::Transform3d& text_tr,
    const Domain::ModelVolumePtrs& volumes_to_slice,
    const Domain::FontFile& ff,
    const Domain::FontProp& fp,
    unsigned count_lines,
    double* line_height_mm_ptr
)
{
    Domain::FontProp::VerticalAlign align = fp.align.vertical;

    double line_height_mm = calc_line_height_in_mm(ff, fp);
    assert(line_height_mm > 0);
    if (line_height_mm <= 0)
        return {};

    // size_in_mm .. contain volume scale and should be ascent value in mm
    double line_offset       = fp.size_in_mm * ascent_ratio_offset;
    double first_line_center = line_offset + get_align_y_offset_in_mm(align, count_lines, ff, fp);
    std::vector<float> line_centers(count_lines);
    for (size_t i = 0; i < count_lines; ++i)
        line_centers[i] = static_cast<float>(first_line_center - i * line_height_mm);

    // contour transformation
    Domain::Transform3d c_trafo     = text_tr * get_rotation();
    Domain::Transform3d c_trafo_inv = c_trafo.inverse();

    std::vector<Domain::Polygons> line_contours(count_lines);
    for (const Domain::ModelVolume* volume : volumes_to_slice) {
        MeshSlicingParams slicing_params;
        slicing_params.trafo = c_trafo_inv * volume->get_matrix();
        for (size_t i = 0; i < count_lines; ++i) {
            const Domain::Polygons polys =
                Slic3r::slice_mesh(volume->mesh().its, line_centers[i], slicing_params);
            if (polys.empty())
                continue;
            Domain::Polygons& contours = line_contours[i];
            contours.insert(contours.end(), polys.begin(), polys.end());
        }
    }

    // fix for text line out of object
    // When move text close to edge - line center could be out of object
    for (Domain::Polygons& contours : line_contours) {
        if (!contours.empty())
            continue;

        // use line center at zero, there should be some contour.
        float line_center = 0.f;
        for (const Domain::ModelVolume* volume : volumes_to_slice) {
            MeshSlicingParams slicing_params;
            slicing_params.trafo = c_trafo_inv * volume->get_matrix();
            const Domain::Polygons polys =
                Slic3r::slice_mesh(volume->mesh().its, line_center, slicing_params);
            if (polys.empty())
                continue;
            contours.insert(contours.end(), polys.begin(), polys.end());
        }
    }

    TextLines result = select_closest_contour(line_contours);
    assert(result.size() == count_lines);
    assert(line_centers.size() == count_lines);
    // Fill centers
    for (size_t i = 0; i < count_lines; ++i)
        result[i].y = line_centers[i];

    if (line_height_mm_ptr != nullptr)
        *line_height_mm_ptr = line_height_mm;

    return result;
}

TextLinesModel::TextLinesModel(TextPresetManager& preset_manager,
    Biz::ProjectInteractor& project_interactor,
    App::Plater::PlaterScenePresenter& scene_presenter,
    App::Render::Device& device
    ):
    m_preset_manager(preset_manager),
    m_project_interactor(project_interactor),
    m_scene_presenter(scene_presenter),
    m_device(device),
    m_proj_ctxs(project_interactor)
{
    Domain::ColorRGBA gray_color = Domain::ColorRGBA::LIGHT_GRAY();
    gray_color.a(.7f);
    m_material = App::Render::Material{}
        .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", gray_color)
        .set_transparent(true);
}

const TextLines& TextLinesModel::get_lines()
{
    return m_proj_ctxs.selected().lines;
}

bool TextLinesModel::exist_lines() const
{ 
    return !m_proj_ctxs.selected().lines.empty();
}

void TextLinesModel::create_text_lines(unsigned count_lines, const Domain::Transform3d* text_tr)
{
    reset();

    Domain::Transform3d text_tr_data; // text volume transformation for text_tr pointer
    Domain::ModelVolumePtrs volumes_to_slice; // object parts to slice

    App::Scene::Scene& scene = m_scene_presenter.scene();
    App::Scene::Node* object_node = nullptr; // Place to append visualzation
    if (text_tr == nullptr) {
        // volume with text is selected and keep transformation
        const Domain::ModelVolume* text_volume_ptr = get_selected_text_volume(m_project_interactor).volume;
        if (text_volume_ptr == nullptr)
            return;
        text_tr_data = text_volume_ptr->get_matrix(); // copy transformation
        text_tr = &text_tr_data;
        volumes_to_slice = prepare_volumes_to_slice(*text_volume_ptr);

        size_t instance_id = m_project_interactor.scene_interactor()
            .object_selection().elements.front().instance_id;
        size_t object_id = text_volume_ptr->get_object()->id().id;
        object_node = scene.root().query_first([object_id, instance_id](const App::Scene::Node* n){
            const auto* tag = n->tag_of_type<App::Scene::SceneNodeTag>();
            return tag != nullptr && 
                tag->volume_id == 0 &&
                tag->object_id == object_id &&
                tag->instance_id == instance_id;
            });
    } else {
        // before create of the text volume is known only future transformation
        const Domain::Project& project = m_project_interactor.selected_project();
        const Biz::Scene::ObjectSelection& selection =
            m_project_interactor.scene_interactor().object_selection();
        if (selection.elements.empty())
            return; // no object for create slice
        const Domain::ElementRef& el = selection.elements.front();
        const Domain::ModelObject * object = project.find_object_by_id(el.object_id);

        volumes_to_slice = prepare_volumes_to_slice(*object);

        object_node = scene.root().query_first([&el](const App::Scene::Node* n) {
            const auto* tag = n->tag_of_type<App::Scene::SceneNodeTag>();
            return tag != nullptr &&
                tag->volume_id == 0 &&
                tag->object_id == el.object_id &&
                tag->instance_id == el.instance_id;
            });
    }    

    const auto& ffc = m_preset_manager.get_font_file_with_cache();
    assert(ffc.has_value());
    if (!ffc.has_value())
        return;
    const auto& ff_ptr = ffc.font_file;
    assert(ff_ptr != nullptr);
    if (ff_ptr == nullptr)
        return;
    const Domain::FontFile& ff = *ff_ptr;
    const Domain::FontProp& fp = m_preset_manager.get_font_prop();

    double line_height_mm;
    ProjectContext& proj_ctx = m_proj_ctxs.selected();
    proj_ctx.lines = Emboss::create_text_lines(
            *text_tr,
            volumes_to_slice,
            ff,
            fp,
            count_lines,
            &line_height_mm);
    if (proj_ctx.lines.empty())
        return;

    bool is_mirrored = has_reflection(*text_tr);
    float radius = static_cast<float>(line_height_mm / 20.);
    
    // create node and append into scene
    indexed_triangle_set its = create_its(proj_ctx.lines, radius);

    proj_ctx.geometry = App::Render::geometry_from_triangle_mesh(m_device, its);

    TextLineNodeTag tag{};
    int layer_index = int(App::Plater::PlaterSceneLayer::DocumentObjects);
    App::Scene::NodeBuilder builder{scene};
    builder.set_debug_name("Text lines")
        .set_transform(*text_tr)
        .set_tag(tag)
        .set_mesh(proj_ctx.geometry.get(), m_material, layer_index);
    proj_ctx.m_text_line_node = builder.build().release();
    scene.add_child(proj_ctx.m_text_line_node, object_node);
}

void TextLinesModel::reset()
{
    ProjectContext& proj_ctx = m_proj_ctxs.selected();
    if (proj_ctx.lines.empty())
        return; // already reseted

    proj_ctx.lines.clear();
    proj_ctx.geometry = nullptr;

    if (proj_ctx.m_text_line_node != nullptr) {
        m_scene_presenter.scene().remove_child(proj_ctx.m_text_line_node);
        proj_ctx.m_text_line_node = nullptr;
    }
}

void TextLinesModel::set_visible(const bool visible)
{
    ProjectContext& proj_ctx = m_proj_ctxs.selected();
    if (proj_ctx.m_text_line_node != nullptr) {
        proj_ctx.m_text_line_node->set_enabled(visible);
    }
}

SelectedText get_selected_text_volume(
    const Domain::Project& project,
    const Domain::ElementRefs& selected_elements
)
{
    if (selected_elements.size() != 1)
        return {}; // multiple volumes selected

    const Domain::ElementRef& selected = selected_elements.front();
    SelectedText result;
    if (selected.has_volume()) {
        result = SelectedText{
            .volume      = project.find_volume_by_id(selected.object_id, selected.volume_id),
            .instance_id = selected.instance_id
        };
    } else {
        // Check is selected object contain only volume with text
        const Domain::ModelObject* object_ptr = project.find_object_by_id(selected.object_id);
        if (object_ptr == nullptr)
            return {}; // after delete volume
        if (object_ptr->volumes.size() != 1)
            return {};
        result = SelectedText{
            .volume      = object_ptr->volumes.front(),
            .instance_id = selected.instance_id
        };
    }

    if (result.volume == nullptr)
        return {};

    if (!result.volume->text_configuration.has_value())
        return {}; // selected volume is not text

    return result;
}

SelectedText get_selected_text_volume(const Biz::ProjectInteractor& project_interactor) {
    const Domain::Project& project = project_interactor.selected_project();
    const Biz::Scene::ObjectSelection &selection = 
        project_interactor.scene_interactor().object_selection();
    return get_selected_text_volume(project, selection.elements);
}


} // namespace Slic3r::Biz::Emboss
