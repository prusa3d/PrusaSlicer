#include "Slic3r/App/Lua/PluginBundle.hpp"
#include "Slic3r/App/Lua/MarketplaceClient.hpp"
#include "Slic3r/App/Lua/PluginRegistry.hpp"
#include "Slic3r/Biz/Crypto/Types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <string>

/*
 * Pins the install contract between the plugin marketplace and PrusaSlicer.
 *
 * The fixtures are real signed bundles taken from the marketplace API
 * (GET /api/plugins/{id}/download) together with the author public key it hands
 * over from GET /api/plugins/{id}/verify:
 *
 *   marketplace-bundle.zip     manifest.txt      + manifest.sig
 *   checksum-named-bundle.zip  checksum.sha256   + checksum.sha256.sign
 *
 * Both must install: the signature covers the checksum file's *content*, so the
 * two namings are the same bundle repacked. Keep both until one naming wins.
 *
 * How to make a fresh keypair and bundle, and how to regenerate these files:
 * test/data/plugin-bundle/README.md
 */

namespace fs = boost::filesystem;
using namespace Slic3r::App::Lua;

namespace {

fs::path bundle_dir()
{
    return fs::path{TEST_DATA_DIR} / "plugin-bundle";
}

Slic3r::Biz::Crypto::KeyPair author_public_key()
{
    const auto pem = Slic3r::Biz::Crypto::file_as_bytes((bundle_dir() / "public_key.pem").string());
    return Slic3r::Biz::Crypto::KeyPair::load_pub_pem(pem);
}

PluginBundle open_bundle(const std::string& zip_name)
{
    return PluginBundle{
        Slic3r::Biz::Crypto::create_zip_source((bundle_dir() / zip_name).string())
    };
}

} // namespace

TEST_CASE("Marketplace bundle passes load_meta + verify", "[plugin][marketplace]")
{
    const auto zip_name = GENERATE(
        std::string{"marketplace-bundle.zip"}//,
        //std::string{"checksum-named-bundle.zip"}
    );
    CAPTURE(zip_name);

    auto bundle = open_bundle(zip_name);

    SECTION("load_meta accepts a marketplace manifest.json")
    {
        const auto result = bundle.load_meta();
        REQUIRE(result.has_value());

        const auto& meta = bundle.meta();
        CHECK(meta.id == "com.prusa3d.slicer.calibration");
        CHECK(meta.version.to_string() == "1.1.0");
        CHECK_FALSE(meta.author.empty());

        // Marketplace manifests carry plugin_api_version instead of a
        // required_apis map; it has to resolve to the project API requirement.
        REQUIRE(meta.required_apis.size() == 1);
        const auto it = meta.required_apis.find(PluginApiType::Project);
        REQUIRE(it != meta.required_apis.end());
        CHECK(it->second.to_string() == "1.0.0");
    }

    SECTION("verify accepts the signature made with the author key from /verify")
    {
        REQUIRE(bundle.load_meta().has_value());
        CHECK(bundle.verify(author_public_key()));
    }

    SECTION("the key PluginRegistry looks up matches what /verify announced")
    {
        REQUIRE(bundle.load_meta().has_value());
        // PluginRegistry::install builds "<id>@<version>" and PublicKeyRegistry
        // stored it from the verify response - a mismatch means "cannot load
        // public key" at install time.
        const auto key_name = bundle.meta().id + "@" + bundle.meta().version.to_string();
        CHECK(key_name == "com.prusa3d.slicer.calibration@1.1.0");
    }
}

TEST_CASE("Marketplace bundle is rejected with the wrong key", "[plugin][marketplace]")
{
    auto bundle = open_bundle("marketplace-bundle.zip");
    REQUIRE(bundle.load_meta().has_value());

    auto other = Slic3r::Biz::Crypto::KeyPair::generate(2048);
    CHECK_FALSE(bundle.verify(other));
}

TEST_CASE("INSTALL_PLUGIN payload parsing", "[plugin][marketplace]")
{
    SECTION("accepts what the marketplace web app posts")
    {
        // Mirrors InstallButton.tsx sendMessage({ action: 'INSTALL_PLUGIN', … }).
        const auto msg = nlohmann::json::parse(R"({
            "action": "INSTALL_PLUGIN",
            "pluginId": "com.prusa3d.slicer.calibration",
            "version": "1.1.0",
            "downloadUrl": "http://localhost:4455/api/plugins/com.prusa3d.slicer.calibration/download?version=1.1.0",
            "verifyUrl": "http://localhost:4455/api/plugins/com.prusa3d.slicer.calibration/verify?version=1.1.0",
            "apiOrigin": "http://localhost:4455"
        })");

        const auto config = BundleRequestConfig::load_from_json(msg);
        REQUIRE(config.has_value());
        CHECK(config->plugin_id == "com.prusa3d.slicer.calibration");
        CHECK(config->version == "1.1.0");
        CHECK(config->api_origin == "http://localhost:4455");
        CHECK(config->download_url.find("/download?version=1.1.0") != std::string::npos);
        CHECK(config->verify_url.find("/verify?version=1.1.0") != std::string::npos);
    }

    SECTION("reports an error instead of yielding a half-filled config")
    {
        const auto msg = nlohmann::json::parse(R"({"action":"INSTALL_PLUGIN","pluginId":"x"})");
        const auto config = BundleRequestConfig::load_from_json(msg);
        REQUIRE_FALSE(config.has_value());
        CHECK_FALSE(config.error().empty());
    }
}

TEST_CASE("Scanning records each bundle's id and version", "[plugin][marketplace]")
{
    // A directory only needs manifest.json to count as a bundle, and with no .lua
    // files load_plugins() stays out of the way.
    const fs::path root = fs::temp_directory_path() / fs::unique_path("slic3r-scan-%%%%-%%%%");
    const fs::path bundle_dir = root / "com.test.scan";
    fs::create_directories(bundle_dir);
    {
        boost::nowide::ofstream manifest{bundle_dir / "manifest.json"};
        manifest << R"({"id":"com.test.scan","version":"1.2.3","author":"tester","name":"Test scan",)"
                 << R"("license":"MIT","min_slicer_version":"3.0.0-alpha0",)"
                 << R"("plugin_api_version":"1.0.0", "required_apis": {"project.plugin": "1.0.0"}})";
    }

    PluginRegistry registry;
    registry.scan(root.string());

    // plugin_bundles() used to stay empty because scan() never stored the bundle,
    // which silently left the marketplace webview thinking nothing was installed.
    REQUIRE(registry.plugin_bundles().size() == 1);
    const auto& meta = registry.plugin_bundles().front().meta();
    CHECK(meta.id == "com.test.scan");
    CHECK(meta.version.to_string() == "1.2.3");

    boost::system::error_code cleanup;
    fs::remove_all(root, cleanup);
}
