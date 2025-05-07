#include <catch2/catch_test_macros.hpp>

#include <libslic3r/CutSurface.hpp>
#include <libslic3r/TriangleMesh.hpp> // its_make_cube + its_merge

using namespace Slic3r;
TEST_CASE("Cut character from surface", "[Emboss]")
{
    std::string font_path = std::string(TEST_DATA_DIR) +
                            "/../../resources/fonts/NotoSans-Regular.ttf";
    char         letter     = '%';
    float        flatness   = 2.;
    unsigned int font_index = 0;    // collection
    double        z_depth    = 50.f; // projection size

    auto font = Emboss::create_font_file(font_path.c_str());
    REQUIRE(font != nullptr);
    std::optional<Emboss::Glyph> glyph = 
        Emboss::letter2glyph(*font, font_index, letter, flatness);
    REQUIRE(glyph.has_value());
    ExPolygons shapes = glyph->shape;
    REQUIRE(!shapes.empty());

    Transform3d tr = Transform3d::Identity();
    tr.translate(Vec3d(0., 0., -z_depth));
    double text_shape_scale = 0.001; // Emboss.cpp --> SHAPE_SCALE
    tr.scale(text_shape_scale);
    Emboss::OrthoProject cut_projection(tr, Vec3d(0., 0., z_depth));

    auto object = its_make_cube(782 - 49 + 50, 724 + 10 + 50, 5);
    its_translate(object, Vec3f(49 - 25, -10 - 25, -40));
    auto cube2 = object; // copy
    its_translate(cube2, Vec3f(100, -40, 7.5));
    its_merge(object, std::move(cube2));

    std::vector<indexed_triangle_set> objects{object};
    // Call core function for cut surface
    auto surfaces = cut_surface(shapes, objects, cut_projection, 0.5);
    CHECK(!surfaces.empty());

    Emboss::OrthoProject projection(Transform3d::Identity(),
                                    Vec3d(0.f, 0.f, 10.f));
    its_translate(surfaces, Vec3f(0.f, 0.f, 10));

    indexed_triangle_set its = cut2model(surfaces, projection);
    CHECK(!its.empty());
    // its_write_obj(its, "C:/data/temp/projected.obj");
}

//#define DEBUG_3MF
#ifdef DEBUG_3MF

// Test load of 3mf
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Model.hpp"

static std::vector<indexed_triangle_set> transform_volumes(ModelVolume *mv) {
    const auto &volumes = mv->get_object()->volumes;
    std::vector<indexed_triangle_set> results;
    results.reserve(volumes.size());

    // Improve create object from part or use gl_volume
    // Get first model part in object
    for (const ModelVolume *v : volumes) {
        if (v->id() == mv->id()) continue;
        if (!v->is_model_part()) continue;
        const TriangleMesh &tm = v->mesh();
        if (tm.empty()) continue;
        if (tm.its.empty()) continue;
        results.push_back(tm.its); // copy: indexed_triangle_set
        indexed_triangle_set& its = results.back();
        its_transform(its,v->get_matrix());
    }
    return results;
}

static Emboss::OrthoProject create_projection_for_cut(
    Transform3d                    tr,
    double                         shape_scale,
    const BoundingBox             &shape_bb,
    const std::pair<float, float> &z_range)
{
    // create sure that emboss object is bigger than source object
    const float safe_extension = 1.0f;
    double      min_z          = z_range.first - safe_extension;
    double      max_z          = z_range.second + safe_extension;
    assert(min_z < max_z);
    // range between min and max value
    double   projection_size           = max_z - min_z;
    Matrix3d transformation_for_vector = tr.linear();
    // Projection must be negative value.
    // System of text coordinate
    // X .. from left to right
    // Y .. from bottom to top
    // Z .. from text to eye
    Vec3d untransformed_direction(0., 0., projection_size);
    Vec3d project_direction = transformation_for_vector * untransformed_direction;

    // Projection is in direction from far plane
    tr.translate(Vec3d(0., 0., min_z));

    tr.scale(shape_scale);
    // Text alignemnt to center 2D
    Vec2d move = -(shape_bb.max + shape_bb.min).cast<double>() / 2.;
    // Vec2d move = -shape_bb.center().cast<double>(); // not precisse
    tr.translate(Vec3d(move.x(), move.y(), 0.));
    return Emboss::OrthoProject(tr, project_direction);
}

TEST_CASE("CutSurface in 3mf", "[Emboss]")
{
    //std::string path_to_3mf = "C:/Users/Filip Sykala/Downloads/EmbossFromMultiVolumes.3mf";
    //int         object_id      = 0;
    //int         text_volume_id = 2;

    //std::string path_to_3mf = "C:/Users/Filip Sykala/Downloads/treefrog.3mf";    
    //int object_id      = 0;
    //int text_volume_id = 1;

    std::string path_to_3mf = "C:/Users/Filip Sykala/Downloads/cube_test.3mf";    
    int object_id      = 1;
    int text_volume_id = 2;

    Model model;
    DynamicPrintConfig config;
    ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};
    CHECK(load_3mf(path_to_3mf.c_str(), config, ctxt, &model, false));
    CHECK(object_id >= 0);
    CHECK((size_t)object_id < model.objects.size());
    ModelObject* mo = model.objects[object_id];
    CHECK(mo != nullptr);
    CHECK(text_volume_id >= 0);
    CHECK((size_t)text_volume_id < mo->volumes.size());
    ModelVolume *mv_text = mo->volumes[text_volume_id];
    CHECK(mv_text != nullptr);
    CHECK(mv_text->text_configuration.has_value());
    TextConfiguration &tc = *mv_text->text_configuration;
    /* // Need GUI to load font by wx
    std::optional<wxFont> wx_font = GUI::WxFontUtils::load_wxFont(tc.style.path);
    CHECK(wx_font.has_value());
    Emboss::FontFileWithCache ff(GUI::WxFontUtils::create_font_file(*wx_font));
    CHECK(ff.font_file != nullptr);
    /*/  // end use GUI
    // start use fake font
    std::string font_path = std::string(TEST_DATA_DIR) +
                            "/../../resources/fonts/NotoSans-Regular.ttf";
    Emboss::FontFileWithCache ff(Emboss::create_font_file(font_path.c_str()));
    // */ // end use fake font
    CHECK(ff.has_value());
    std::vector<indexed_triangle_set> its = transform_volumes(mv_text);
    BoundingBoxf3 bb;
    for (auto &i : its) bb.merge(Slic3r::bounding_box(i));

    Transform3d cut_projection_tr = mv_text->get_matrix() * tc.fix_3mf_tr->inverse();
    Transform3d emboss_tr = cut_projection_tr.inverse();
    BoundingBoxf3 mesh_bb_tr = bb.transformed(emboss_tr);

    std::pair<float, float> z_range{mesh_bb_tr.min.z(), mesh_bb_tr.max.z()};

    FontProp fp = tc.style.prop;
    ExPolygons shapes = Emboss::text2shapes(ff, tc.text.c_str(), fp);
    double shape_scale = Emboss::get_text_shape_scale(fp, *ff.font_file);

    Emboss::OrthoProject projection = create_projection_for_cut(
        cut_projection_tr, shape_scale, get_extents(shapes), z_range);

    float projection_ratio = -z_range.first / (z_range.second - z_range.first);
    SurfaceCut cut = cut_surface(shapes, its, projection, projection_ratio); 
    its_write_obj(cut, "C:/data/temp/cutSurface/result_cut.obj");
}

#endif // DEBUG_3MF
