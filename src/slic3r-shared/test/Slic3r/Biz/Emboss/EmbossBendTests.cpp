#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <sstream>
#include <cereal/archives/binary.hpp>
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Emboss/TextPresetSerialization.hpp"

#include "Slic3r/Biz/Emboss/EmbossJob.hpp"
#include "Slic3r/Biz/Emboss/TextShapeProvider.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz::Emboss;

namespace {
// Supply a deterministic glyph outline so generation/deformation tests do not
// depend on installed fonts. Deformation uses the real TextShapeProvider path.
class RectangularTextProvider : public TextShapeProvider {
public:
    RectangularTextProvider(const Domain::TextConfiguration& text, FontFileWithCache& font, int width) :
        TextShapeProvider(text, Domain::EmbossProjection{2.0, false}, {}, font), m_width(width)
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
    const double angle = *text.style.prop.bend_horizontal;
    const double depth_bent_width = angle == 0. ? width : 2. * width / angle * std::sin(angle / 2.);
    const double arc = *text.style.prop.bend_arc;
    const double expected_width = arc == 0. ? depth_bent_width : 2. * depth_bent_width / arc * std::sin(arc / 2.);
    const double arc_height = arc == 0. ? 0. : std::abs(2. * depth_bent_width / arc * std::pow(std::sin(arc / 4.), 2));
    CHECK_THAT(bounds.max.x() - bounds.min.x(), Catch::Matchers::WithinAbs(expected_width, 1e-4));
    CHECK_THAT(bounds.max.y() - bounds.min.y(), Catch::Matchers::WithinAbs(40. / 0.7 * std::sin(0.35) + arc_height, 1e-4));
    Domain::Model model;
    auto* volume = Biz::Algorithms::ModelObject::add_volume(model.add_object(), *result);
    provider_ptr->write(*volume);
    REQUIRE(volume->text_configuration.has_value());
    const auto& reference = volume->text_configuration->bend_reference;
    REQUIRE(reference.has_value());
    CHECK_THAT(reference->max.x() - reference->min.x(), Catch::Matchers::WithinAbs(width, 1e-6));
    CHECK_THAT(reference->max.y() - reference->min.y(), Catch::Matchers::WithinAbs(20., 1e-6));
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
