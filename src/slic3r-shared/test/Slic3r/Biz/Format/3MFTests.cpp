#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"

#include <boost/filesystem.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace fs = boost::filesystem;

using Slic3r::Domain::FacetsAnnotation;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

static inline std::string test_3mf_path(const char* path)
{
    return std::string(TEST_DATA_DIR) + "/test_3mf/" + path;
}

TEST_CASE("3MF production extension - component without p:path resolves within same file", "[3mf]")
{
    // 3MF Production Extension allows geometry in sub-model files.
    // Per spec (Chapter 2): "Only a component element in the root model file MAY contain a path
    // attribute" and "Non-root model file components MUST only reference objects in the same
    // model file." So components in sub-models reference local objects via objectid alone,
    // without p:path. The parser must resolve such local references correctly.
    const Loaded3MF loaded         = load_3mf(test_3mf_path("production_ext.3mf"));
    const ModelObjectPtrs& objects = loaded.model.objects;

    // Must produce exactly 1 object (not 2 - no ghost empty object, no extra nonprintable object)
    REQUIRE(objects.size() == 1);

    // The single object must have geometry (1 volume of type MODEL_PART)
    REQUIRE(objects[0]->volumes.size() == 1);
    CHECK(objects[0]->volumes[0]->is_model_part());

    // The object must have 1 instance with the build item transform (translation 50,50,0)
    REQUIRE(objects[0]->instances.size() == 1);
    Vec3d offset = objects[0]->instances[0]->get_offset();
    CHECK(Domain::is_approx(offset.x(), 50.0));
    CHECK(Domain::is_approx(offset.y(), 50.0));

    // The mesh must have actual geometry (8 vertices, 12 triangles of a 10x10x10 cube)
    CHECK(objects[0]->volumes[0]->mesh().its.vertices.size() == 8);
    CHECK(objects[0]->volumes[0]->mesh().its.indices.size() == 12);
}

namespace {

constexpr size_t STATE_TYPE_NONE = static_cast<size_t>(TriangleStateType::NONE);

void paint_facets(
    ModelVolume& volume,
    FacetsAnnotation ModelVolume::* facets_member,
    const int painted_facets_count,
    const TriangleStateType state
)
{
    Algorithms::TriangleSelector selector{volume.mesh()};
    for (int facet_idx = 0; facet_idx < painted_facets_count; ++facet_idx) {
        selector.set_facet(facet_idx, state);
    }

    (volume.*facets_member).triangle_splitting_data = selector.serialize();
}

} // namespace

TEST_CASE("3MF round trip restores used_states of painted volumes - NONE", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(3, 3);

    ModelVolume* partially_painted_volume = project.model().objects[0]->volumes.front();
    ModelVolume* fully_painted_volume     = project.model().objects[1]->volumes.front();
    ModelVolume* seam_painted_volume      = project.model().objects[2]->volumes.front();

    const int facets_count = static_cast<int>(partially_painted_volume->mesh().facets_count());

    paint_facets(
        *partially_painted_volume,
        &ModelVolume::mm_segmentation_facets,
        facets_count - 1,
        TriangleStateType::Extruder2
    );

    paint_facets(
        *fully_painted_volume,
        &ModelVolume::mm_segmentation_facets,
        facets_count,
        TriangleStateType::Extruder2
    );

    paint_facets(*seam_painted_volume, &ModelVolume::seam_facets, 1, TriangleStateType::ENFORCER);

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "painted_round_trip.3mf";
    store_3mf(file_path.string(), project);

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 3);
    REQUIRE(loaded.model.objects[0]->volumes.size() == 1);
    REQUIRE(loaded.model.objects[1]->volumes.size() == 1);
    REQUIRE(loaded.model.objects[2]->volumes.size() == 1);

    const ModelVolume& loaded_partially_painted = *loaded.model.objects[0]->volumes.front();
    const ModelVolume& loaded_fully_painted     = *loaded.model.objects[1]->volumes.front();
    const ModelVolume& loaded_seam_painted      = *loaded.model.objects[2]->volumes.front();

    // Partially painted: one facet has no serialized record, so NONE must be marked as used.
    CHECK(loaded_partially_painted.mm_segmentation_facets.get_data().used_states[STATE_TYPE_NONE]);
    CHECK_FALSE(loaded_partially_painted.is_fully_mm_painted());

    // Fully painted: every facet has a serialized record, so NONE must stay unused.
    CHECK_FALSE(
        loaded_fully_painted.mm_segmentation_facets.get_data().used_states[STATE_TYPE_NONE]
    );
    CHECK(loaded_fully_painted.is_fully_mm_painted());

    CHECK_FALSE(loaded_seam_painted.seam_facets.empty());
    CHECK(loaded_seam_painted.seam_facets.get_data().used_states[STATE_TYPE_NONE]);
}

namespace {

// Loads a 3mf written by the current (core-spec) writer using the legacy 2.x importer, which
// only understands MM segmentation stored inline as slic3rpe:mmu_segmentation <triangle>
// attributes - never Metadata/Slic3r_facets_annotation.json.
Domain::Model load_with_legacy_importer(const fs::path& file_path)
{
    Domain::Model legacy_model;
    Domain::ConfigPack cfg;
    Biz::LegacyPresetMetadata preset_metadata;
    boost::optional<Slic3r::Semver> generator_version;
    Domain::WipeTowersOnBeds wipe_towers;
    Domain::CustomGCodesOnBeds custom_gcodes;
    Biz::VirtualExtrudersConfig virtual_extruders_config;

    bool loaded_ok = Slic3rLegacy::load_3mf_legacy(
        file_path.string().c_str(),
        cfg,
        preset_metadata,
        &legacy_model,
        true,
        generator_version,
        wipe_towers,
        custom_gcodes,
        virtual_extruders_config
    );
    REQUIRE(loaded_ok);
    return legacy_model;
}

} // namespace

TEST_CASE("3MF dual-write: version-1 MM segmentation is readable by the legacy 2.x importer", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelVolume* volume = project.model().objects[0]->volumes.front();
    const int facets_count = static_cast<int>(volume->mesh().facets_count());
    // Extruder16 (value 16) is the highest state that still fits version 1 encoding.
    paint_facets(*volume, &ModelVolume::mm_segmentation_facets, facets_count, TriangleStateType::Extruder16);

    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-dualwrite-v1-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "dual_write_v1.3mf";
    store_3mf(file_path.string(), project);

    const Domain::Model legacy_model = load_with_legacy_importer(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(legacy_model.objects.size() == 1);
    REQUIRE(legacy_model.objects[0]->volumes.size() == 1);

    const ModelVolume& legacy_volume = *legacy_model.objects[0]->volumes.front();
    REQUIRE_FALSE(legacy_volume.mm_segmentation_facets.empty());
    for (int i = 0; i < facets_count; ++i) {
        CHECK(
            legacy_volume.mm_segmentation_facets.get_triangle_as_string(i) ==
            volume->mm_segmentation_facets.get_triangle_as_string(i)
        );
    }
}

TEST_CASE("3MF dual-write: version-2 MM segmentation is not dual-written for the legacy importer", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelVolume* volume = project.model().objects[0]->volumes.front();
    const int facets_count = static_cast<int>(volume->mesh().facets_count());
    // Any state beyond the named Extruder1..16 enumerators (17-255) forces version 2 encoding,
    // which the legacy inline attribute can't represent - a 2.x reader can't decode it anyway.
    paint_facets(*volume, &ModelVolume::mm_segmentation_facets, facets_count, static_cast<TriangleStateType>(20));

    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-dualwrite-v2-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "dual_write_v2.3mf";
    store_3mf(file_path.string(), project);

    const Domain::Model legacy_model = load_with_legacy_importer(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(legacy_model.objects.size() == 1);
    REQUIRE(legacy_model.objects[0]->volumes.size() == 1);

    // A 2.x reader only ever sees MM painting via the inline attribute, so when it's skipped
    // (version 2 data can't be dual-written) the legacy importer must find no MM painting at all.
    CHECK(legacy_model.objects[0]->volumes.front()->mm_segmentation_facets.empty());
}
