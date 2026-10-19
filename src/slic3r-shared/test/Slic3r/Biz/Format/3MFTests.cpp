#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"

#include <boost/filesystem.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <sstream>

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

TEST_CASE("3MF round trip restores the per-object settings of both technologies", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    Domain::ModelObject& object = *project.model().objects.front();
    object.object_settings.overrides.set("fill_density", Domain::Percentage{42.});
    object.object_settings_sla.overrides.set("pad_enable", false);
    object.object_settings_sla.overrides.set("support_object_elevation", 7.5);

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "object_settings_round_trip.3mf";
    store_3mf(file_path.string(), project);

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 1);
    const Domain::ModelObject& loaded_object = *loaded.model.objects.front();

    const std::optional<Domain::ConfigItem> fill_density{
        loaded_object.object_settings.overrides.get("fill_density")};
    REQUIRE(fill_density);
    CHECK(fill_density->get<Domain::Percentage>() == Domain::Percentage{42.});

    CHECK(loaded_object.object_settings_sla.overrides.size() == 2);
    const std::optional<Domain::ConfigItem> pad_enable{
        loaded_object.object_settings_sla.overrides.get("pad_enable")};
    REQUIRE(pad_enable);
    CHECK_FALSE(pad_enable->get<bool>());
    const std::optional<Domain::ConfigItem> elevation{
        loaded_object.object_settings_sla.overrides.get("support_object_elevation")};
    REQUIRE(elevation);
    CHECK(elevation->get<double>() == 7.5);
}

TEST_CASE("3MF round trip keeps support points user modified", "[3mf]")
{
    using Domain::SLA::DrainHole;
    using Domain::SLA::PointsStatus;
    using Domain::SLA::SupportPoint;
    using Domain::SLA::SupportPointType;
    using Domain::Vec3f;

    Project project;
    project.model() = Test::generate_cubes(2, 2);

    // Only the first object has (user modified) points, the second one has generated points.
    // The first object has a second, transformed part with its own point and drain hole.
    Domain::ModelObject& edited = *project.model().objects.front();
    Domain::ModelVolume& part = *Algorithms::ModelObject::add_volume(&edited, edited.volumes.front()->mesh());
    part.set_scaling_factor(Vec3d{2., 2., 2.});
    part.set_offset(Vec3d{30., 0., 0.});
    edited.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{10.f, 10.f, 0.f}, 0.2f, SupportPointType::island},
        SupportPoint{Vec3f{0.f, 10.f, 10.f}, 0.3f, SupportPointType::manual_add},
    };
    part.sla_support_points = {SupportPoint{Vec3f{1.f, 2.f, 3.f}, 0.4f}};
    part.sla_drain_holes    = {DrainHole{Vec3f{4.f, 5.f, 6.f}, Vec3f{0.f, 1.f, 0.f}, 1.5f, 3.f}};
    edited.sla_points_status = PointsStatus::UserModified;
    project.model().objects.back()->sla_points_status = PointsStatus::AutoGenerated;

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "support_points_round_trip.3mf";
    store_3mf(file_path.string(), project);

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 2);
    const Domain::ModelObject& loaded_edited = *loaded.model.objects.front();
    CHECK(loaded_edited.sla_points_status == PointsStatus::UserModified);
    REQUIRE(loaded_edited.volumes.size() == 2);
    const ModelVolume& loaded_first = *loaded_edited.volumes.front();
    REQUIRE(loaded_first.sla_support_points.size() == 2);
    CHECK(loaded_first.sla_support_points[0].is_island());
    CHECK(loaded_first.sla_support_points[1].head_front_radius == 0.3f);
    CHECK(loaded_first.sla_drain_holes.empty());
    // Stored per volume, in its mesh coordinates.
    const ModelVolume& loaded_part = *loaded_edited.volumes.back();
    CHECK(loaded_part.sla_support_points == part.sla_support_points);
    CHECK(loaded_part.sla_drain_holes == part.sla_drain_holes);

    const Domain::ModelObject& loaded_generated = *loaded.model.objects.back();
    CHECK(loaded_generated.sla_points_status == PointsStatus::NoPoints);
    CHECK(loaded_generated.volumes.front()->sla_support_points.empty());
}

namespace {

// Appends an axis aligned box as 8 vertices and 12 triangles to the 3mf mesh streams.
void append_box(std::ostream& vertices, std::ostream& triangles, int first_vertex_id, const Vec3d& min, const Vec3d& max)
{
    for (int i = 0; i < 8; ++i) {
        const Vec3d v{(i & 1) ? max.x() : min.x(), (i & 2) ? max.y() : min.y(), (i & 4) ? max.z() : min.z()};
        vertices << "<vertex x=\"" << v.x() << "\" y=\"" << v.y() << "\" z=\"" << v.z() << "\"/>\n";
    }
    static constexpr int faces[12][3] = {
        {0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4},
        {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    for (const auto& f : faces)
        triangles << "<triangle v1=\"" << first_vertex_id + f[0] << "\" v2=\"" << first_vertex_id + f[1]
                  << "\" v3=\"" << first_vertex_id + f[2] << "\"/>\n";
}

// Writes a 3mf with a single object made of a part and a modifier, laid out the way
// PrusaSlicer < 2.3 stored it: only "slic3rpe:Version3mf" (no "Application") metadata
// in the model file, volume types in Metadata/Slic3r_PE_model.config.
// With prusaslicer_data == false, only a plain 3mf with the same geometry is written.
void write_pre_2_3_3mf(const fs::path& file_path, bool prusaslicer_data)
{
    std::ostringstream vertices, triangles;
    append_box(vertices, triangles, 0, Vec3d{0., 0., 0.}, Vec3d{20., 20., 20.});
    append_box(vertices, triangles, 8, Vec3d{5., 5., 5.}, Vec3d{15., 15., 25.});

    const std::string content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    const std::string relationships =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel-1\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";
    const std::string model =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<model unit=\"millimeter\" xml:lang=\"en-US\" "
        "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" "
        "xmlns:slic3rpe=\"http://schemas.slic3r.org/3mf/2017/06\">\n" +
        std::string(prusaslicer_data ? "<metadata name=\"slic3rpe:Version3mf\">1</metadata>\n" : "") +
        "<resources>\n<object id=\"1\" type=\"model\">\n<mesh>\n"
        "<vertices>\n" + vertices.str() + "</vertices>\n"
        "<triangles>\n" + triangles.str() + "</triangles>\n"
        "</mesh>\n</object>\n</resources>\n"
        "<build>\n<item objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 100 100 10\"/>\n</build>\n"
        "</model>\n";
    const std::string model_config =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config>\n"
        "<object id=\"1\" instances_count=\"1\">\n"
        "<metadata type=\"object\" key=\"name\" value=\"box\"/>\n"
        "<volume firstid=\"0\" lastid=\"11\">\n"
        "<metadata type=\"volume\" key=\"name\" value=\"box\"/>\n"
        "</volume>\n"
        "<volume firstid=\"12\" lastid=\"23\">\n"
        "<metadata type=\"volume\" key=\"name\" value=\"modifier\"/>\n"
        "<metadata type=\"volume\" key=\"modifier\" value=\"1\"/>\n"
        "</volume>\n"
        "</object>\n"
        "</config>\n";

    Algorithms::ZipArchive archive = Algorithms::ZipArchive::open(file_path.string(), Algorithms::ZipArchive::Mode::Write);
    REQUIRE(archive.is_open());
    REQUIRE(archive.add_entry("[Content_Types].xml", content_types.data(), content_types.size()));
    REQUIRE(archive.add_entry("_rels/.rels", relationships.data(), relationships.size()));
    REQUIRE(archive.add_entry("3D/3dmodel.model", model.data(), model.size()));
    if (prusaslicer_data)
        REQUIRE(archive.add_entry("Metadata/Slic3r_PE_model.config", model_config.data(), model_config.size()));
    REQUIRE(archive.close());
}

// Returns the type of the fatal issue thrown by load_3mf(), std::nullopt if it did not throw.
std::optional<Read3mfIssueType> load_3mf_fatal_issue(const fs::path& file_path)
{
    try {
        load_3mf(file_path.string());
    } catch (const Loaded3MFException& e) {
        return e.issue.type;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("3MF saved by PrusaSlicer < 2.3 is loaded by the legacy loader with modifiers kept", "[3mf]")
{
    // PrusaSlicer < 2.3 did not store the "Application" metadata, which the new loader used
    // to detect old PrusaSlicer 3mfs. These were then loaded as generic 3mfs, ignoring
    // the volume types stored in Metadata/Slic3r_PE_model.config (PD-603).
    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-pre-2-3-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "pre_2_3.3mf";
    write_pre_2_3_3mf(file_path, true);

    // The new loader must hand the file over to the legacy one (see load_from_project()).
    const std::optional<Read3mfIssueType> fatal_issue = load_3mf_fatal_issue(file_path);
    const Domain::Model legacy_model = load_with_legacy_importer(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(fatal_issue.has_value());
    CHECK(*fatal_issue == Read3mfIssueType::legacy_loader_required);

    REQUIRE(legacy_model.objects.size() == 1);
    REQUIRE(legacy_model.objects[0]->volumes.size() == 2);
    CHECK(legacy_model.objects[0]->volumes[0]->is_model_part());
    CHECK(legacy_model.objects[0]->volumes[1]->is_modifier());
}

TEST_CASE("3MF without PrusaSlicer metadata is not handed over to the legacy loader", "[3mf]")
{
    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-generic-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "generic.3mf";
    write_pre_2_3_3mf(file_path, false);

    const std::optional<Read3mfIssueType> fatal_issue = load_3mf_fatal_issue(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    CHECK_FALSE(fatal_issue.has_value());
}

namespace {

// Writes a 3mf whose root model file is not well-formed XML: an unescaped '&' in the "Title"
// metadata, as PrusaSlicer 2.3.0-rc1 wrote object names (#5577). The "Application" metadata
// is written either before or after the broken line.
void write_3mf_with_invalid_xml(const fs::path& file_path, bool application_first)
{
    const std::string content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    const std::string relationships =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel-1\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";
    const std::string application = "<metadata name=\"Application\">PrusaSlicer-2.3.0-rc1+win64</metadata>\n";
    const std::string title = "<metadata name=\"Title\">Michael & Leah</metadata>\n";
    const std::string model =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<model unit=\"millimeter\" xml:lang=\"en-US\" "
        "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" "
        "xmlns:slic3rpe=\"http://schemas.slic3r.org/3mf/2017/06\">\n" +
        (application_first ? application + title : title + application) +
        "<resources>\n</resources>\n<build>\n</build>\n</model>\n";

    Algorithms::ZipArchive archive = Algorithms::ZipArchive::open(file_path.string(), Algorithms::ZipArchive::Mode::Write);
    REQUIRE(archive.is_open());
    REQUIRE(archive.add_entry("[Content_Types].xml", content_types.data(), content_types.size()));
    REQUIRE(archive.add_entry("_rels/.rels", relationships.data(), relationships.size()));
    REQUIRE(archive.add_entry("3D/3dmodel.model", model.data(), model.size()));
    REQUIRE(archive.close());
}

} // namespace

TEST_CASE("3MF with invalid XML in the root model reports the parse error", "[3mf]")
{
    // The parse error used to be dropped, the file was loaded as an empty generic 3mf
    // and the user only learned that the project is empty.
    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-invalid-xml-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "invalid_xml.3mf";
    write_3mf_with_invalid_xml(file_path, false);

    std::optional<Read3mfIssue> fatal_issue;
    try {
        load_3mf(file_path.string());
    } catch (const Loaded3MFException& e) {
        fatal_issue = e.issue;
    }
    const tl::expected<Domain::Model, std::string> imported =
        FileLoadingLogic::read_model_from_file(file_path.string(), nullptr);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(fatal_issue.has_value());
    CHECK(fatal_issue->type == Read3mfIssueType::expat_parse_error);
    CHECK_THAT(fatal_issue->msg, Catch::Matchers::ContainsSubstring("3D/3dmodel.model"));
    CHECK_THAT(fatal_issue->msg, Catch::Matchers::ContainsSubstring("at line 3"));

    REQUIRE_FALSE(imported.has_value());
    CHECK_THAT(imported.error(), Catch::Matchers::ContainsSubstring("at line 3"));
}

TEST_CASE("3MF failing in the legacy loader reports the reason", "[3mf]")
{
    // The new loader hands the file over to the legacy one once it reads the "Application"
    // metadata. The parse error found by the legacy loader used to be only logged.
    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-invalid-xml-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "invalid_xml_legacy.3mf";
    write_3mf_with_invalid_xml(file_path, true);

    const std::optional<Read3mfIssueType> fatal_issue = load_3mf_fatal_issue(file_path);

    Domain::Model legacy_model;
    Domain::ConfigPack cfg;
    Biz::LegacyPresetMetadata preset_metadata;
    boost::optional<Slic3r::Semver> generator_version;
    Domain::WipeTowersOnBeds wipe_towers;
    Domain::CustomGCodesOnBeds custom_gcodes;
    Biz::VirtualExtrudersConfig virtual_extruders_config;
    std::string legacy_error;
    const bool legacy_loaded_ok = Slic3rLegacy::load_3mf_legacy(
        file_path.string().c_str(),
        cfg,
        preset_metadata,
        &legacy_model,
        false,
        generator_version,
        wipe_towers,
        custom_gcodes,
        virtual_extruders_config,
        &legacy_error
    );
    const tl::expected<Domain::Model, std::string> imported =
        FileLoadingLogic::read_model_from_file(file_path.string(), nullptr);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(fatal_issue.has_value());
    CHECK(*fatal_issue == Read3mfIssueType::legacy_loader_required);

    CHECK_FALSE(legacy_loaded_ok);
    CHECK_THAT(legacy_error, Catch::Matchers::ContainsSubstring("3D/3dmodel.model' at line 4"));

    REQUIRE_FALSE(imported.has_value());
    CHECK_THAT(imported.error(), Catch::Matchers::ContainsSubstring("at line 4"));
}
