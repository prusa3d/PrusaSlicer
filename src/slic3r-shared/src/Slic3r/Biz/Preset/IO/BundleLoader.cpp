#include "Slic3r/Biz/Preset/IO/BundleLoader.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>

#include <functional>
#include <numeric>

#include <cereal/archives/binary.hpp>

namespace Slic3r::Biz::Preset::IO {

namespace fs = boost::filesystem;

void populate_local_bundle(const BundlePaths& bundle_paths)
{
    for (const auto& repo_entry : fs::directory_iterator(bundle_paths.app_bundle_path)) {
        if (!repo_entry.is_directory()) {
            continue;
        }
        for (const auto& vendor_entry : fs::directory_iterator(repo_entry)) {
            const auto src_path = vendor_entry.path();

            if (!vendor_entry.is_directory() || !fs::exists(src_path / "vendor.yaml")) {
                continue;
            }
            const auto dest_path =
                bundle_paths.local_bundle_path / repo_entry.path().filename() / src_path.filename();
            if (!fs::exists(dest_path / "vendor.yaml")) {
                SPDLOG_INFO(
                    "Populate vendor {}/{}",
                    repo_entry.path().filename().string(),
                    dest_path.filename().string()
                );
                if (fs::exists(dest_path)) {
                    // clean up the old directory
                    fs::remove_all(dest_path);
                }
                fs::create_directories(dest_path);
                fs::copy(
                    src_path,
                    dest_path,
                    fs::copy_options::recursive | fs::copy_options::overwrite_existing
                );
                fs::copy_file(
                    src_path.string() + ".idx",
                    dest_path.string() + ".idx",
                    fs::copy_options::overwrite_existing
                );
            }
        }
    }
}


Domain::Preset::Bundle load_bundle(const BundlePaths& bundle_paths)
{
    Domain::Preset::Bundle bundle;
    HwConfigLoader config_loader;
    PresetLoader preset_loader;

    fs::path config_base{bundle_paths.user_config_path};

    std::set<std::pair<std::string, std::string>> repo_vendor_pairs;

    for (const auto& base_dir : {bundle_paths.local_bundle_path, bundle_paths.app_bundle_path}) {
        if (base_dir.empty()) {
            continue;
        }
        for (const auto& repo_dir : fs::directory_iterator(base_dir)) {
            if (!repo_dir.is_directory()) {
                continue;
            }
            for (const auto& vendor_dir : fs::directory_iterator(repo_dir)) {
                if (!vendor_dir.is_directory() || !fs::exists(vendor_dir.path() / fs::path{"vendor.yaml"})) {
                    continue;
                }
                repo_vendor_pairs.insert(std::make_pair(
                    repo_dir.path().filename().string(),
                    vendor_dir.path().filename().string()
                ));
            }
        }
    }


    for (const auto& [repo_id, vendor_id] : repo_vendor_pairs) {
        for (const auto& base_dir : {bundle_paths.local_bundle_path, bundle_paths.app_bundle_path}) {
            const fs::path vendor_dir{fs::path{base_dir} /  repo_id / vendor_id};
            const fs::path vendor_yaml_path{vendor_dir / "vendor.yaml"};

            if (!fs::exists(vendor_yaml_path)) {
                continue;
            }

            bool loaded = false;
            Domain::Preset::VendorBundle vendor_bundle;
            SPDLOG_INFO("Loading preset bundle vendor dir: {}", vendor_dir.string());

            try {

                config_loader.load(vendor_yaml_path.string());
                vendor_bundle.vendor_data = config_loader.release();
                vendor_bundle.vendor_data.info.repo_id = repo_id;

                preset_loader.load_dir(vendor_dir.string());
                loaded = true;
            }
            catch (Yaml::ParseError& e) {
                SPDLOG_ERROR("Loading bundle {} failed with error {}", vendor_dir.string(), e.what());
            }
            catch (fs::filesystem_error& e) {
                SPDLOG_ERROR("Loading bundle {} failed with error {}", vendor_dir.string(), e.what());
            }
            catch (std::exception& e) {
                SPDLOG_ERROR("Loading bundle {} failed with error {}", vendor_dir.string(), e.what());
            }

            if (loaded) {
                // read user presets
                const fs::path user_vendor_dir{bundle_paths.user_preset_dir_path(vendor_id, repo_id)};
                if (fs::exists(user_vendor_dir)) {
                    try {
                        preset_loader.load_dir(user_vendor_dir.string(), Domain::Preset::PresetOrigin::User);
                    }
                    catch (Yaml::ParseError& e) {
                        SPDLOG_ERROR("Loading user bundle {} failed with error {}", user_vendor_dir.string(), e.what());
                    }
                    catch (fs::filesystem_error& e) {
                        SPDLOG_ERROR("Loading user bundle {} failed with error {}", user_vendor_dir.string(), e.what());
                    }
                    catch (std::exception& e) {
                        SPDLOG_ERROR("Loading user bundle {} failed with error {}", user_vendor_dir.string(), e.what());
                    }
                }

                std::tie(vendor_bundle.presets, vendor_bundle.preset_names) = preset_loader.release();

                // TODO read/append user printer configs
                //vendor_bundle.printer_configs = load_vendor_user_configs((config_base / vendor_bundle.vendor_data.info.id).string(), vendor_bundle.vendor_data);

                bundle.vendor_bundles.emplace(vendor_bundle.vendor_data.info.id, std::move(vendor_bundle));

                // prevent continuing with fallback
                break;
            }
        }
    }

    return bundle;
}

void save_bundle_configs(const Domain::Preset::Bundle& bundle, const std::string& config_path)
{
    fs::path config_base{config_path};
    for (const auto& [_, vendor_bundle] : bundle.vendor_bundles)
        save_vendor_user_configs(
            vendor_bundle.printer_configs,
            (config_base / vendor_bundle.vendor_data.info.id).string(),
            vendor_bundle.vendor_data
        );
}


static size_t combine_hashes(size_t hash1, size_t hash2)
{
    return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
}

static size_t get_file_hash(const std::string& path)
{
    boost::nowide::ifstream file(path, std::ios::binary);
    if (! file)
        throw std::runtime_error(std::string("Unable to get hash for ") + path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::hash<std::string>{}(content);
}

static size_t hash_folder_recursive(const fs::path& path)
{
    size_t seed = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (fs::is_regular_file(entry))
                seed ^= combine_hashes(get_file_hash(entry.path().string()), seed);
        }
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error(std::string("Error iterating directory ") + path.string() + ": " + e.what());
    }
    return seed;
}

static size_t get_cache_footprint(const BundlePaths& bundle_paths, const std::string& slicer_version)
{
    size_t folder_hash = 0;
    boost::system::error_code ec;

    auto update_folder_hash = [&folder_hash, &ec](const std::string& path)
    {
        fs::path p{path};
        if (fs::exists(p, ec)) {
            folder_hash = combine_hashes(folder_hash, hash_folder_recursive(p));
        }
    };

    update_folder_hash(bundle_paths.app_bundle_path);
    update_folder_hash(bundle_paths.local_bundle_path);
    update_folder_hash(bundle_paths.user_bundle_path);
    // Combine the two hashes and a hash of slicer version
    size_t hash = combine_hashes(folder_hash, std::hash<std::string>{}(slicer_version));

    // Increment the following value to enforce invalidation of caches from older versions:
    size_t cache_epoch = 13;
    return combine_hashes(hash, std::hash<int>{}(cache_epoch));
}

void serialize_bundle(const std::string& filename, const Domain::Preset::Bundle& bundle,
    const BundlePaths& bundle_paths, const std::string& slicer_version)
{
    SPDLOG_DEBUG("Saving currently loaded bundle into cache file...");
    try {
        if (const auto dir = fs::path(filename).parent_path(); ! boost::filesystem::exists(dir))
            boost::filesystem::create_directory(dir);
    } catch (const boost::filesystem::filesystem_error&) {
        SPDLOG_ERROR("Unable to create bundle cache directory.");
    }
    boost::nowide::ofstream os(filename, std::ios::binary);
    if (os) {
        try {
            os << get_cache_footprint(bundle_paths, slicer_version);
        } catch (const std::runtime_error&) {
            SPDLOG_ERROR("Unable to calculate current bundle hash.");
        }
        cereal::BinaryOutputArchive archive(os);
        archive(bundle);
        SPDLOG_DEBUG("Loaded bundle cached successfully.");
    } else
        SPDLOG_ERROR("Unable to create bundle cache file.");
}

std::optional<Domain::Preset::Bundle> deserialize_bundle(const std::string& filename,
    const BundlePaths& bundle_paths, const std::string& slicer_version)
{
    SPDLOG_DEBUG("Running in debug mode - will try to recover bundle from cache...");
    if (boost::nowide::ifstream is(filename, std::ios::binary); is) {
        SPDLOG_DEBUG("Bundle cache file found: {}", filename);
        size_t cache_hash = 0;
        is >> cache_hash;
        size_t cur_hash = 0;
        try {
            cur_hash = get_cache_footprint(bundle_paths, slicer_version);
        } catch (const std::runtime_error&) {
            SPDLOG_ERROR("Unable to calculate current bundle hash.");
            return std::nullopt;
        }
        if (cache_hash != cur_hash) {
            SPDLOG_DEBUG("Bundle hashes do NOT match.");
            return std::nullopt;
        }            
        SPDLOG_DEBUG("Bundle hashes match. Deserializing bundle from cache...");
        try {
            cereal::BinaryInputArchive archive(is);
            Domain::Preset::Bundle bundle;
            archive(bundle);
            SPDLOG_DEBUG("Sucessfully recovered bundle from cache.");
            return std::make_optional(std::move(bundle));
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unable to deserialize bundle from cache: {}", ex.what());
        }        
    } else
        SPDLOG_DEBUG("Bundle cache file not found.");
    return std::nullopt;
}

}
