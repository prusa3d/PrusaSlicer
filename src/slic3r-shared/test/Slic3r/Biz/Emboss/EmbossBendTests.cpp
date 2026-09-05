#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <sstream>
#include <cereal/archives/binary.hpp>
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Emboss/TextPresetSerialization.hpp"

#include "Slic3r/Biz/Emboss/EmbossJob.hpp"
#include "Slic3r/Biz/Emboss/TextShapeProvider.hpp"
#include "Slic3r/Biz/Emboss/TextBender.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz::Emboss;

namespace {
// Supply a deterministic glyph outline so generation/deformation tests do not
// depend on installed fonts. Deformation uses the real TextShapeProvider path.
class RectangularTextProvider : public TextShapeProvider {
public:
    RectangularTextProvider(const Domain::TextConfiguration& text, FontFileWithCache& font, int width, double depth = 2.0) :
        TextShapeProvider(text, Domain::EmbossProjection{depth, false}, {}, font), m_width(width)
    {
        m_shape.scale = 1.0;
    }

    bool create_shape() override
    {
        m_shape.final_shape = {Domain::ExPolygons{Domain::ExPolygon{
            {-m_width / 2, -10}, {m_width / 2, -10}, {m_width / 2, 10}, {-m_width / 2, 10}}}, true};
        return true;
    }

    void deform_mesh(Domain::TriangleMesh& mesh) const override
    {
        ++deformations;
        TextShapeProvider::deform_mesh(mesh);
    }

    mutable int deformations = 0;

private:
    int m_width;
};

// Measure the generated triangles, not just paired analytical bend points.
// The closest hit in each direction from inside the solid gives its local depth.
double distance_to_surface(const indexed_triangle_set& mesh, const Domain::Vec3d& origin,
    const Domain::Vec3d& direction)
{
    double closest = std::numeric_limits<double>::infinity();
    for (const auto& triangle : mesh.indices) {
        const Domain::Vec3d a = mesh.vertices[triangle[0]].cast<double>();
        const Domain::Vec3d ab = mesh.vertices[triangle[1]].cast<double>() - a;
        const Domain::Vec3d ac = mesh.vertices[triangle[2]].cast<double>() - a;
        const Domain::Vec3d p = direction.cross(ac);
        const double determinant = ab.dot(p);
        if (std::abs(determinant) < 1e-12)
            continue;
        const Domain::Vec3d offset = origin - a;
        const double u = offset.dot(p) / determinant;
        const Domain::Vec3d q = offset.cross(ab);
        const double v = direction.dot(q) / determinant;
        if (u < -1e-8 || v < -1e-8 || u + v > 1.0 + 1e-8)
            continue;
        const double distance = ac.dot(q) / determinant;
        if (distance > 0.0)
            closest = std::min(closest, distance);
    }
    return closest;
}
}

TEST_CASE("Text generation applies bend once and rebuilds mesh statistics", "[EmbossBend]")
{
    FontFileWithCache font;
    font.font_file = std::make_shared<Domain::FontFile>(
        std::make_unique<std::vector<unsigned char>>(1, 0),
        std::vector<Domain::FontFile::Info>{{800, -200, 0, 1000}});
    Domain::TextConfiguration text;
    text.style.prop.bend_horizontal = GENERATE(-2.5f, 0.f, 1.0f);
    text.style.prop.bend_vertical = 0.7f;
    text.style.prop.bend_arc = GENERATE(-1.3f, 0.f, 1.3f);
    text.bend_reference = Domain::BoundingBox3d{{-3., -3., 0.}, {3., 3., 2.}};
    const int width = GENERATE(40, 100);
    TriMeshBaseData input;
    auto provider = std::make_unique<RectangularTextProvider>(text, font, width);
    const auto* provider_ptr = provider.get();
    input.shape_provider = std::move(provider);
    const auto result = create_mesh(input);
    REQUIRE(result.has_value());
    REQUIRE(provider_ptr->deformations == 1);
    const auto bounds = result->bounding_box();
    const auto actual_bounds = Domain::bounding_box(result->its);
    CHECK(bounds.min.isApprox(actual_bounds.min, 1e-6));
    CHECK(bounds.max.isApprox(actual_bounds.max, 1e-6));
    CHECK(bounds.min.allFinite());
    CHECK(bounds.max.allFinite());
    CHECK((bounds.max.array() > bounds.min.array()).all());
    Domain::Model model;
    auto* volume = Biz::Algorithms::ModelObject::add_volume(model.add_object(), *result);
    provider_ptr->write(*volume);
    REQUIRE(volume->text_configuration.has_value());
    const auto& reference = volume->text_configuration->bend_reference;
    REQUIRE(reference.has_value());
    CHECK_THAT(reference->max.x() - reference->min.x(), Catch::Matchers::WithinAbs(width, 1e-6));
    CHECK_THAT(reference->max.y() - reference->min.y(), Catch::Matchers::WithinAbs(20., 1e-6));
}

TEST_CASE("Generated embossed mesh keeps its raised depth at the middle and both ends", "[EmbossBend][EmbossDepth]")
{
    FontFileWithCache font;
    font.font_file = std::make_shared<Domain::FontFile>(
        std::make_unique<std::vector<unsigned char>>(1, 0),
        std::vector<Domain::FontFile::Info>{{800, -200, 0, 1000}});
    const double depth = GENERATE(0.4, 2.0, 5.0);
    const BendParams params = GENERATE(
        BendParams{}, BendParams{3.1415927f, 0.f, 0.f}, BendParams{-3.1415927f, 0.f, 0.f},
        BendParams{0.f, 3.1415927f, 0.f}, BendParams{0.f, -3.1415927f, 0.f},
        BendParams{0.f, 0.f, 3.1415927f}, BendParams{0.f, 0.f, -3.1415927f},
        BendParams{1.0f, 0.7f, 1.3f}, BendParams{-1.0f, -0.7f, -1.3f},
        BendParams{2.6f, -2.4f, 2.7f}, BendParams{-2.6f, 2.4f, -2.7f});
    CAPTURE(depth, params.horizontal_bend, params.vertical_curl, params.vertical_arc);
    Domain::TextConfiguration text;
    text.style.prop.bend_horizontal = params.horizontal_bend;
    text.style.prop.bend_vertical = params.vertical_curl;
    text.style.prop.bend_arc = params.vertical_arc;
    TriMeshBaseData input;
    input.shape_provider = std::make_unique<RectangularTextProvider>(text, font, 100, depth);
    const auto result = create_mesh(input);
    REQUIRE(result.has_value());
    Domain::Model model;
    auto* volume = Biz::Algorithms::ModelObject::add_volume(model.add_object(), *result);
    input.shape_provider->write(*volume);
    const auto& reference = volume->text_configuration->bend_reference;
    REQUIRE(reference.has_value());
    const Domain::BoundingBox3f bounds{reference->min.cast<float>(), reference->max.cast<float>()};
    CHECK_THAT(bounds.max.z() - bounds.min.z(), Catch::Matchers::WithinAbs(depth, 1e-6));
    const double middle_z = 0.5 * (bounds.min.z() + bounds.max.z());
    for (double x : {-48., -25., 0., 25., 48.}) {
        for (double y : {-8., 0., 8.}) {
            CAPTURE(x, y);
            const Domain::Vec3d middle{x, y, middle_z};
            const Domain::Vec3d origin = TextBender::bend_point(middle, params, bounds);
            constexpr double step = 1e-3;
            const Domain::Vec3d tangent_x = TextBender::bend_point(middle + Domain::Vec3d{step, 0., 0.}, params, bounds)
                - TextBender::bend_point(middle - Domain::Vec3d{step, 0., 0.}, params, bounds);
            const Domain::Vec3d tangent_y = TextBender::bend_point(middle + Domain::Vec3d{0., step, 0.}, params, bounds)
                - TextBender::bend_point(middle - Domain::Vec3d{0., step, 0.}, params, bounds);
            const Domain::Vec3d normal = tangent_x.cross(tangent_y).normalized();
            const double measured = distance_to_surface(result->its, origin, normal)
                + distance_to_surface(result->its, origin, -normal);
            CHECK_THAT(measured, Catch::Matchers::WithinAbs(depth, 0.02 * depth));
        }
    }
}

TEST_CASE("Text arc is included in style comparisons and undo serialization", "[EmbossBend]")
{
    Domain::FontProp original;
    original.bend_horizontal = 0.6f;
    original.bend_vertical = -0.7f;
    original.bend_arc = GENERATE(-1.8f, 0.f, 2.3f);
    Domain::FontProp restored = original;
    restored.bend_arc.reset();
    REQUIRE_FALSE(restored == original);
    std::stringstream stream;
    {
        cereal::BinaryOutputArchive output(stream);
        output(original);
    }
    {
        cereal::BinaryInputArchive input(stream);
        input(restored);
    }
    CHECK(restored == original);
    CHECK(restored.bend_arc == original.bend_arc);
}

TEST_CASE("Saved text presets preserve arcs and load the previous archive format", "[EmbossBend]")
{
    TextPresetManager::PresetsObj original{};
    for (float bend : {-0.8f, 1.2f}) {
        TextPresetManager::Preset preset;
        preset.emboss_style.descriptor = {"Saved text", "font.ttf", Domain::FontDescriptor::Type::file_path};
        preset.emboss_style.prop.bend_horizontal = bend;
        preset.emboss_style.prop.bend_vertical = -bend;
        preset.emboss_style.prop.char_gap = 12;
        preset.projection = {2.5, true};
        preset.distance = 0.3f;
        preset.angle = 0.7f;
        original.presets.push_back(preset);
    }
    original.current_index = 1;
    std::stringstream stream;
    SECTION("current format retains the Z arc") {
        for (auto& preset : original.presets)
            preset.emboss_style.prop.bend_arc = 1.4f;
        cereal::BinaryOutputArchive output(stream);
        output(original);
    }
    SECTION("version one retains the older bends and subsequent fields") {
        // Write a historical v1 fixture: it contains no arc field in FontProp.
        cereal::BinaryOutputArchive output(stream);
        const std::uint32_t version = 1;
        output(version, cereal::make_size_tag(static_cast<cereal::size_type>(original.presets.size())));
        for (const auto& preset : original.presets) {
            const auto& prop = preset.emboss_style.prop;
            output(preset.emboss_style.descriptor, prop.size_in_mm, prop.per_glyph, prop.align,
                prop.char_gap, prop.line_gap, prop.boldness, prop.skew, prop.collection_number,
                prop.bend_horizontal, prop.bend_vertical, preset.projection, preset.distance, preset.angle);
        }
        output(original.current_index);
    }
    TextPresetManager::PresetsObj restored{};
    cereal::BinaryInputArchive input(stream);
    input(restored);
    CHECK(restored.current_index == 1);
    REQUIRE(restored.presets.size() == 2);
    CHECK(restored.presets == original.presets);
}
