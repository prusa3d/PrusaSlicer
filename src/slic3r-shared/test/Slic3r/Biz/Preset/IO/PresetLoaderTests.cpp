#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/Preset/IO/BundleLoader.hpp"

#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <mutex>
#include <boost/nowide/fstream.hpp>
#include <boost/variant/get.hpp>

TEST_CASE("PresetLoader preset-filament-common.yaml", "[preset]")
{
    using namespace Slic3r::Biz::Preset::IO;
    using namespace Slic3r::Domain::Preset;
    namespace Yaml = Slic3r::Biz::Yaml;

    PresetLoader loader;
    try {
        for (auto filename : std::array{"preset-filament-common.yaml", "preset-filament-prusament-pla.yaml"}) {
            const std::string path = Tests::get_datadir().string() + "/presets/" + filename;
            std::mutex mutex;
            loader.load(path, mutex);
        }
    } catch (const Yaml::ParseError& e) {
        std::cout << e.what() << std::endl;
        FAIL();
    }

    const auto& presets = loader.presets();
    REQUIRE(presets.size() == 1);
    auto filament_presets_it = presets.find(PresetKind::FdmMaterial);
    REQUIRE(filament_presets_it != presets.end());
    const auto& filament_presets = filament_presets_it->second;
    REQUIRE(filament_presets.size() == 4);

    auto pla_preset_it = std::find_if(filament_presets.begin(), filament_presets.end(), [](const auto& preset) { return preset.id == "*PLA*"; });
    REQUIRE(pla_preset_it != filament_presets.end());
    const auto& pla_preset = *pla_preset_it;
    REQUIRE(pla_preset.kind == PresetKind::FdmMaterial);
    REQUIRE_FALSE(pla_preset.name.has_value());
    REQUIRE(pla_preset.inherits.size() == 0);
    REQUIRE(pla_preset.values.size() == 39);
    REQUIRE(pla_preset.variants.size() == 2);


    const auto& pla_v0 = pla_preset.variants[0];
    REQUIRE(boost::get<Slic3r::Domain::Expr::VarRef>(*pla_v0.condition.value().expr).name == "printer.planetary_gearbox");

    auto fcfs_it = pla_v0.values.find("filament_cooling_final_speed");
    REQUIRE((fcfs_it != pla_v0.values.end()));
    REQUIRE(std::holds_alternative<double>(fcfs_it->second));

    auto sfg_it = pla_v0.values.find("start_filament_gcode");
    REQUIRE(sfg_it != pla_v0.values.end());
    REQUIRE(std::holds_alternative<std::string>(sfg_it->second));

}



static void populate_directory_with_random_files(
    const boost::filesystem::path& dir_path,
    int num_files = 5,
    int file_size = 50)
{
    boost::uuids::random_generator uuid_gen;

    for (int i = 0; i < num_files; ++i) {
        std::string filename = boost::uuids::to_string(uuid_gen()) + ".txt";
        boost::filesystem::path file_path = dir_path / filename;
        std::vector<char> buffer(file_size);
        for (int j = 0; j < file_size; ++j)
            buffer[j] = 65 + rand() % 25; // A-Z
        boost::nowide::ofstream ofs(file_path.string(), std::ios::binary);
        if (!ofs)
            throw std::runtime_error("Failed to open file for writing: " + file_path.string());
        ofs.write(buffer.data(), buffer.size());
    }
}

TEST_CASE("PresetBundle caching - invalidation", "[preset]")
{
    using namespace Slic3r::Biz::Preset;
    using namespace Slic3r::Domain::Preset;
    namespace fs = boost::filesystem;

    Bundle bundle;

    Tests::TestTempDir temp_dir;
    std::string cache_file_fullpath = (temp_dir.path() / "test_cache").string();
    std::string dir1 = (temp_dir.path() / "dir1").string();
    std::string dir2 = (temp_dir.path() / "dir2").string();;
    fs::create_directory(dir1);
    fs::create_directory(dir2);
    populate_directory_with_random_files(temp_dir.path());
    populate_directory_with_random_files(dir1);
    populate_directory_with_random_files(dir2);

    std::string slicer_version = "1.2.3-alpha1";

    auto ser = [&]() {
        IO::serialize_bundle(cache_file_fullpath, bundle, {dir1, dir2}, slicer_version);
    };
    auto deser = [&]() -> std::optional<Bundle> {
        return IO::deserialize_bundle(cache_file_fullpath, {dir1, dir2}, slicer_version);
    };

    ser();
    REQUIRE(deser() != std::nullopt);
    {
        boost::nowide::ofstream out(dir1 + "/new_file");
    }    
    REQUIRE(deser() == std::nullopt);

    ser();
    REQUIRE(deser() != std::nullopt);
    {
        boost::nowide::ofstream out(dir2 + "/new_file");
    }
    REQUIRE(deser() == std::nullopt);

    ser();
    REQUIRE(deser() != std::nullopt);
    ASSERT(boost::filesystem::exists(dir2 + "/new_file"));
    {
        boost::nowide::ofstream out(dir2 + "/new_file", std::ios::app);
        out << "a";
    }
    REQUIRE(deser() == std::nullopt);

    ser();
    REQUIRE(deser() != std::nullopt);
    slicer_version = "1.2.3-alpha2";
    REQUIRE(deser() == std::nullopt);    
}
