#include <catch2/catch_test_macros.hpp>

#include "CLITestUtils.hpp"
#include "Slic3r/App/CLI/CLIApp.hpp"
#include <boost/beast/core/detail/base64.hpp>
#include <sstream>
#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include <qoi.h>
#include <cstdlib>
#include "Slic3r/Domain/Workbench.hpp"

#include <chrono>
#include <fstream>
#include <optional>

using namespace Slic3r;
using namespace Slic3r::Domain;
using namespace Slic3r::Biz;
using namespace Slic3r::App::CLI;
using namespace Slic3r::App::CLI::Test;

namespace {

struct ThumbnailFixture
{
    ScopedTempDir temp;
    Workbench workbench;
    SelectionId project_id = workbench.next_project_id();
    Project& project       = workbench.projects().try_emplace(project_id).first->second;
    CLIThumbnailImageGenerator generator{workbench};
    BedInstance* bed;

    ThumbnailFixture()
    {
        auto& services = Platform::PlatformServices::instance();
        services.set_job_manager(nullptr);
        services.set_app_instance_message_handler(nullptr);
        services.set_main_thread_dispatcher(
            std::make_unique<App::Platform::StdMainThreadDispatcher>()
        );
        auto physical_bed = std::make_unique<Bed>(Bed::create(
            {.type             = BedType::Rectangle,
             .contour          = {{0, 0}, {200, 0}, {200, 200}, {0, 200}},
             .max_print_height = 200}
        ));
        auto config       = std::make_unique<ConfigContainer>();
        config->set_bed(*physical_bed);
        bed = &config->add_bed_instance();
        project.bed_container().beds().push_back(std::move(physical_bed));
        project.config_containers().push_back(std::move(config));
        auto* object = Algorithms::Model::add_object(
            &project.model(),
            "cube",
            "",
            Algorithms::TriangleMesh::make_cube(20, 20, 20)
        );
        bed->model_instances.push_back(object->add_instance());
    }

    ~ThumbnailFixture()
    {
        Platform::PlatformServices::instance().main_thread_dispatcher().close();
    }

    Slicing::ThumbnailImageRequest request() const
    {
        return {
            ThumbnailType::SlicingBed,
            {.project_id              = project_id,
             .bed_instance_id         = bed->id().id,
             .bed_instance_with_error = false,
             .sizes                   = {{16, 16}, {64, 48}, {48, 64}}}
        };
    }

    Slicing::ThumbnailImageResults generate(const Slicing::ThumbnailImageRequests& requests)
    {
        auto future = generator.enqueue_thumbnail_requests(requests);
        Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_enqueued();
        REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        return future.get();
    }

    // The generator needs only the ZIP preview: the live project above models
    // the geometry already loaded by the CLI, without coupling tests to presets.
    void archive(const std::optional<std::string>& preview, const char* name = "cube.3mf")
    {
        const auto path = temp.path() / name;
        Algorithms::MZ_Archive zip;
        REQUIRE(Algorithms::open_zip_writer(&zip.arch, path.string()));
        if (preview) {
            REQUIRE(mz_zip_writer_add_mem(
                &zip.arch,
                "Metadata/thumbnail.png",
                preview->data(),
                preview->size(),
                MZ_DEFAULT_COMPRESSION
            ));
        } else {
            REQUIRE(mz_zip_writer_add_mem(&zip.arch, "test", "", 0, MZ_DEFAULT_COMPRESSION));
        }
        REQUIRE(mz_zip_writer_finalize_archive(&zip.arch));
        REQUIRE(Algorithms::close_zip_writer(&zip.arch));
        project.set_file_path(path);
    }

    std::string preview_png()
    {
        // Asymmetric image detects accidental flips in the 2.9 -> 3.0 port.
        std::vector<uint8_t> pixels(64 * 48 * 3, 0);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
                pixels[3 * (y * 64 + x) + (y < 24 ? 0 : 2)] = 255;
        const auto path = (temp.path() / "preview.png").string();
        REQUIRE(png::write_rgb_to_file(path, 64, 48, pixels));
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
};

void check_images(
    const Slicing::ThumbnailImageResults& results,
    const Slicing::ThumbnailImageRequest& request
)
{
    REQUIRE(results.size() == 1);
    const auto& result = results.front();
    CHECK(result.type == request.type);
    CHECK(result.project_id == request.params.project_id);
    CHECK(result.bed_instance_id == request.params.bed_instance_id);
    REQUIRE(result.images.size() == request.params.sizes.size());
    for (size_t i = 0; i < result.images.size(); ++i) {
        const auto& image = result.images[i];
        CHECK(image.width() == request.params.sizes[i].width);
        CHECK(image.height() == request.params.sizes[i].height);
        CHECK(image.format() == PixelFormat::RGBA8);
        bool opaque      = false;
        bool transparent = false;
        bool lit         = false;
        for (size_t p = 0; p < image.pixels.size(); p += 4) {
            opaque |= image.pixels[p + 3] == 255;
            transparent |= image.pixels[p + 3] == 0;
            lit |= image.pixels[p + 3] == 255 && image.pixels[p] > 0;
        }
        CHECK(opaque);
        CHECK(transparent);
        CHECK(lit); // Catch missing 3.0 shader lighting uniforms.
    }
}

} // namespace

TEST_CASE("CLI preserves a valid stored 3MF preview and its orientation", "[cli][thumbnails]")
{
    ThumbnailFixture f;
    f.archive(f.preview_png(), "cube.3MF");
    auto request         = f.request();
    request.params.sizes = {{64, 48}};
    const auto results   = f.generate({request});
    REQUIRE(results.size() == 1);
    REQUIRE(results.front().images.size() == 1);
    const auto& image = results.front().images.front();
    REQUIRE(image.width() == 64);
    REQUIRE(image.height() == 48);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const size_t p = 4 * (y * 64 + x);
            REQUIRE(image.pixels[p] == (y < 24 ? 255 : 0));
            REQUIRE(image.pixels[p + 1] == 0);
            REQUIRE(image.pixels[p + 2] == (y < 24 ? 0 : 255));
            REQUIRE(image.pixels[p + 3] == 255);
        }
    }
}

TEST_CASE("CLI skips disabled and stale thumbnail requests", "[cli][thumbnails]")
{
    ThumbnailFixture f;
    auto request = f.request();
    SECTION("no requests")
    {
        CHECK(f.generate({}).empty());
    }
    SECTION("no sizes")
    {
        request.params.sizes.clear();
        CHECK(f.generate({request}).empty());
    }
    SECTION("removed project")
    {
        request.params.project_id = INVALID_ID;
        CHECK(f.generate({request}).empty());
    }
    SECTION("removed bed")
    {
        request.params.bed_instance_id = INVALID_ID;
        CHECK(f.generate({request}).empty());
    }
}

// Opt in explicitly on a machine with working CGL/EGL/WGL:
// slic3r-app-cli-tests '[.cli-thumbnail-gl]'
TEST_CASE("CLI renders missing and damaged 3MF previews", "[.cli-thumbnail-gl]")
{
    ThumbnailFixture f;
    SECTION("mesh input")
    {
        f.project.set_file_path("cube.stl");
    }
    SECTION("missing preview")
    {
        f.archive(std::nullopt);
    }
    SECTION("empty preview")
    {
        f.archive(std::string{});
    }
    SECTION("invalid PNG")
    {
        f.archive("not a PNG");
    }
    SECTION("uppercase extension")
    {
        f.archive(std::nullopt, "cube.3MF");
    }
    const auto request = f.request();
    check_images(f.generate({request}), request);
}

TEST_CASE("CLI renders only the requested bed in a multi-bed 3MF", "[.cli-thumbnail-gl]")
{
    ThumbnailFixture f;
    const auto request   = f.request();
    const auto reference = f.generate({request});
    check_images(reference, request);
    auto& other_bed = f.project.config_containers().front()->add_bed_instance();
    auto* object    = Algorithms::Model::
        add_object(&f.project.model(), "tall", "", Algorithms::TriangleMesh::make_cube(10, 10, 80));
    auto* instance = object->add_instance();
    instance->set_offset(Vec3d(100, 100, 0));
    other_bed.model_instances.push_back(instance);
    f.archive(f.preview_png());
    auto other_request                   = request;
    other_request.params.bed_instance_id = other_bed.id().id;
    const auto results                   = f.generate({request, other_request});
    REQUIRE(results.size() == 2);
    CHECK(results[0].bed_instance_id == request.params.bed_instance_id);
    CHECK(results[1].bed_instance_id == other_request.params.bed_instance_id);
    REQUIRE(results[0].images.size() == 3);
    REQUIRE(results[1].images.size() == 3);
    CHECK(results[0].images[1].pixels == reference[0].images[1].pixels);
    CHECK(results[1].images[1].pixels != reference[0].images[1].pixels);
}

TEST_CASE(
    "CLI embeds PNG and QOI from a 3MF without changing print commands",
    "[.cli-thumbnail-gl][timeout]"
)
{
    const ScopedTempDir temp;
    const auto mesh          = write_cube_stl(temp.path(), "cube.stl", 20);
    const auto project_path  = temp.path() / "cube.3mf";
    auto params              = make_params(resolved_profile_sets().front());
    params.input.input_files = {mesh.string()};
    params.action.export_3mf = true;
    params.misc.output       = project_path.string();
    REQUIRE(App::CLI::run(params) == EXIT_SUCCESS);

    // Keep this an actual fallback test if CLI project export later grows previews.
    Algorithms::MZ_Archive archive;
    REQUIRE(Algorithms::open_zip_reader(&archive.arch, project_path.string()));
    CHECK(mz_zip_reader_locate_file(&archive.arch, "Metadata/thumbnail.png", nullptr, 0) < 0);
    REQUIRE(Algorithms::close_zip_reader(&archive.arch));

    auto slice = [&](const std::string& thumbnails, const char* name)
    {
        App::InitParams slice_params;
        slice_params.input.input_files   = {project_path.string()};
        slice_params.action.export_gcode = true;
        slice_params.misc.output         = (temp.path() / name).string();
        PrinterSettings printer;
        printer.items.opt("binary_gcode").set(false);
        printer.items.opt("thumbnails").set(thumbnails);
        slice_params.config_overrides =
            {printer.items.opt("binary_gcode"), printer.items.opt("thumbnails")};
        REQUIRE(App::CLI::run(slice_params) == EXIT_SUCCESS);
        std::ifstream input(*slice_params.misc.output, std::ios::binary);
        REQUIRE(input.good());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto enabled  = slice("16x16/QOI,64x48/PNG", "with.gcode");
    const auto disabled = slice("", "without.gcode");
    CHECK(enabled.find("; thumbnail_QOI begin 16x16 ") != std::string::npos);
    CHECK(enabled.find("; thumbnail begin 64x48 ") != std::string::npos);
    CHECK(disabled.find("; thumbnail begin ") == std::string::npos);
    CHECK(disabled.find("; thumbnail_QOI begin ") == std::string::npos);

    // Decode the payloads, not just their header comments.
    auto payload = [&](const std::string& tag, const std::string& size)
    {
        const auto begin = enabled.find("; " + tag + " begin " + size + " ");
        REQUIRE(begin != std::string::npos);
        std::istringstream lines(enabled.substr(enabled.find('\n', begin) + 1));
        std::string line;
        std::string encoded;
        while (std::getline(lines, line) && line != "; " + tag + " end") {
            REQUIRE(line.starts_with("; "));
            encoded += line.substr(2);
        }
        std::string decoded(boost::beast::detail::base64::decoded_size(encoded.size()), '\0');
        const auto [written, read] =
            boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
        decoded.resize(written);
        return decoded;
    };
    const auto qoi = payload("thumbnail_QOI", "16x16");
    REQUIRE(qoi.size() > 22);
    qoi_desc descriptor{};
    const std::unique_ptr<void, decltype(&std::free)> qoi_pixels(
        qoi_decode(qoi.data(), static_cast<int>(qoi.size()), &descriptor, 4),
        &std::free
    );
    REQUIRE(qoi_pixels != nullptr);
    CHECK(descriptor.width == 16);
    CHECK(descriptor.height == 16);
    const auto* rgba = static_cast<const uint8_t*>(qoi_pixels.get());
    bool opaque      = false;
    bool transparent = false;
    for (size_t i = 0; i < descriptor.width * descriptor.height * 4; i += 4) {
        opaque |= rgba[i + 3] == 255;
        transparent |= rgba[i + 3] == 0;
    }
    CHECK(opaque);
    CHECK(transparent);
    const auto decoded = payload("thumbnail", "64x48");
    std::vector<unsigned char> pixels;
    unsigned width  = 0;
    unsigned height = 0;
    REQUIRE(png::decode_png(decoded, pixels, width, height));
    CHECK(width == 64);
    CHECK(height == 48);

    auto commands = [](const std::string& gcode)
    {
        std::vector<std::string> result;
        std::istringstream lines(gcode);
        std::string line;
        while (std::getline(lines, line)) {
            line = line.substr(0, line.find(';'));
            if (line.find_first_not_of(" \t\r") != std::string::npos)
                result.push_back(line);
        }
        return result;
    };
    const auto without_commands = commands(disabled);
    REQUIRE(!without_commands.empty());
    CHECK(commands(enabled) == without_commands);
}

TEST_CASE("CLI thumbnail visibility matches printable model parts", "[.cli-thumbnail-gl]")
{
    ThumbnailFixture fixture;
    auto request   = fixture.request();
    auto* instance = fixture.bed->model_instances.front();
    SECTION("outside bed")
    {
        instance->print_volume_state           = ModelInstancePVS_Fully_Outside;
        request.type                           = ThumbnailType::SceneBed;
        request.params.bed_instance_with_error = true;
        check_images(fixture.generate({request}), request);
    }
    SECTION("partly outside bed")
    {
        instance->print_volume_state = ModelInstancePVS_Partly_Outside;
        check_images(fixture.generate({request}), request);
    }
    SECTION("not printable")
    {
        instance->printable = false;
        CHECK(fixture.generate({request}).empty());
    }
    SECTION("modifier")
    {
        instance->get_object()->volumes.front()->set_type(ModelVolumeType::PARAMETER_MODIFIER);
        CHECK(fixture.generate({request}).empty());
    }
}

TEST_CASE("CLI thumbnail futures settle when the dispatcher closes", "[cli][thumbnails]")
{
    ThumbnailFixture fixture;
    fixture.archive(fixture.preview_png());
    auto& dispatcher = Platform::PlatformServices::instance().main_thread_dispatcher();
    auto pending     = fixture.generator.enqueue_thumbnail_requests({fixture.request()});
    dispatcher.close();
    REQUIRE(pending.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(pending.get().size() == 1);
    auto rejected = fixture.generator.enqueue_thumbnail_requests({fixture.request()});
    REQUIRE(rejected.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(rejected.get().empty());
}

TEST_CASE("CLI falls back for oversized ZIP preview metadata", "[.cli-thumbnail-gl]")
{
    ThumbnailFixture fixture;
    fixture.archive("not a PNG");
    const auto filename = fixture.project.loaded_file_path().string();
    std::ifstream input(filename, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const auto directory = bytes.find(std::string("PK\1\2", 4));
    REQUIRE(directory != std::string::npos);
    // 2 GiB in the central directory used to narrow to a negative int and throw.
    bytes.replace(directory + 24, 4, std::string("\0\0\0\x80", 4));
    {
        std::ofstream output(filename, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto request = fixture.request();
    const auto results = fixture.generate({request, request});
    REQUIRE(results.size() == 2);
    check_images({results[0]}, request);
    check_images({results[1]}, request);
}
