#include "Slic3r/Biz/PresetUpdater/PresetUpdaterTestFixture.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::PresetUpdater::TestSupport;
using namespace std::chrono_literals;
namespace fs = boost::filesystem;

TEST_CASE("PresetUpdater performs a new-vendor install", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.reconfigurations().has_value());
    REQUIRE(listener.reconfigurations()->new_vendors().size() == 1);
    CHECK(listener.reconfigurations()->new_vendors().front().recommended_version == Slic3r::Semver{1, 0, 0});

    const auto reconfigurations = *listener.reconfigurations();

    listener.reset();
    fx.interactor().perform_reconfigurations(reconfigurations);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    CHECK(listener.performed());

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    CHECK(fs::exists(installed_vendor_dir / "vendor.yaml"));
    CHECK(fs::exists(installed_vendor_dir / "printer.yaml"));
    CHECK(fs::exists(fx.installed_path / k_repo_name / (k_vendor_name + ".idx")));
    CHECK(read_file(installed_vendor_dir / "vendor.yaml").find("1.0.0") != std::string::npos);

    listener.reset();
    fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    REQUIRE(fx.wait(listener));
    REQUIRE(listener.reconfigurations().has_value());
    CHECK(listener.reconfigurations()->empty());
}

TEST_CASE("PresetUpdater lists sources and persists selection", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    fx.interactor().list_repositories(true);
    REQUIRE(fx.wait(listener));
    REQUIRE(listener.repos().size() == 1);
    const auto& info = listener.repos().front();
    CHECK(info.descriptor.id == k_repo_name);
    CHECK(info.selected);
    const std::string uuid = info.descriptor.uuid;

    PresetUpdater::SharedPresetUpdaterRepositoryInfoVector desired;
    desired.push_back(PresetUpdater::SharedPresetUpdaterRepositoryInfo{info.descriptor, false});

    listener.reset();
    fx.interactor().apply_repository_selection(desired);
    REQUIRE(fx.wait(listener));
    REQUIRE(listener.repos().size() == 1);
    CHECK_FALSE(listener.repos().front().selected);

    const std::string manifest = read_file(fx.shared_runtime_path / "RepositoryManifest.json");
    CHECK(manifest.find("\"selected\":false") != std::string::npos);
    CHECK(manifest.find(uuid) != std::string::npos);
}

TEST_CASE("PresetUpdater refreshes sources from the server", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    fx.interactor().list_repositories(true, true);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.repos().size() == 1);
    CHECK(listener.repos().front().descriptor.id == k_repo_name);
}

TEST_CASE("PresetUpdater adds and removes a local repository", "[preset_updater]")
{
    Fixture fx;

    CaptureListener listener(fx.interactor());

    const fs::path local_zip = fx.resources_profile_path / "local_repo.zip";
    REQUIRE(fs::exists(local_zip));

    fx.interactor().add_local_repository(local_zip);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());

    std::string local_uuid;
    for (const auto& repo : listener.repos()) {
        if (!repo.descriptor.unzipped_data_path.empty()) {
            local_uuid = repo.descriptor.uuid;
        }
    }
    REQUIRE_FALSE(local_uuid.empty());
    CHECK(fs::exists(fx.data_dir / "local_repositories" / local_uuid));

    listener.reset();
    fx.interactor().remove_local_repository(local_uuid);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    for (const auto& repo : listener.repos()) {
        CHECK(repo.descriptor.uuid != local_uuid);
    }
    CHECK_FALSE(fs::exists(fx.data_dir / "local_repositories" / local_uuid));
}

TEST_CASE("PresetUpdater verifies downloaded file hashes", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");
    fx.set_mutator([](const std::string& url, std::string& body, unsigned&) {
        if (url.ends_with("printer.yaml")) {
            body += "\n# corrupted by test\n";
        }
    });

    CaptureListener listener(fx.interactor());

    SECTION("ignore_hash=true stages the vendor despite corruption")
    {
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->new_vendors().size() == 1);
    }

    SECTION("ignore_hash=false rejects the vendor with a warning")
    {
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, false);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->new_vendors().empty());
        CHECK_FALSE(listener.warnings().empty());
    }
}

TEST_CASE("PresetUpdater classifies multiple vendors in one repository", "[preset_updater]")
{
    Fixture fx;
    fx.put_resources("100");
    copy_dir_content_local(
        fx.resources_profile_path / "second_vendor" / k_repo_name,
        fx.resources_repo_path
    );
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.reconfigurations().has_value());

    const auto& new_vendors = listener.reconfigurations()->new_vendors();
    REQUIRE(new_vendors.size() == 2);

    std::vector<std::string> ids;
    for (const auto& reconf : new_vendors) {
        ids.push_back(reconf.vendor_id);
    }
    CHECK(std::find(ids.begin(), ids.end(), k_vendor_name) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), k_second_vendor) != ids.end());
}

TEST_CASE("PresetUpdater installs reconfigurations over an installed vendor", "[preset_updater]")
{
    Fixture fx;
    CaptureListener listener(fx.interactor());

    std::string target_version;

    SECTION("regular update raises the installed version")
    {
        fx.put_installed("100");
        fx.put_server("101");
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->regular_updates().size() == 1);
        CHECK(listener.reconfigurations()->regular_updates().front().recommended_version == Slic3r::Semver{1, 0, 1});
        target_version = "1.0.1";
    }

    SECTION("forced update replaces an app-incompatible installed version")
    {
        fx.put_installed("200");
        fx.put_server("201");
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->forced_updates().size() == 1);
        CHECK(listener.reconfigurations()->forced_updates().front().recommended_version == Slic3r::Semver{2, 0, 1});
        target_version = "2.0.1";
    }

    SECTION("forced downgrade replaces an app-incompatible newer installed version")
    {
        fx.put_installed("301");
        fx.put_server("300");
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->forced_downgrades().size() == 1);
        CHECK(listener.reconfigurations()->forced_downgrades().front().recommended_version == Slic3r::Semver{3, 0, 0});
        target_version = "3.0.0";
    }

    const auto reconfigurations = *listener.reconfigurations();

    listener.reset();
    fx.interactor().perform_reconfigurations(reconfigurations);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    CHECK(listener.performed());

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    REQUIRE(fs::exists(installed_vendor_dir / "vendor.yaml"));
    CHECK(fs::exists(fx.installed_path / k_repo_name / (k_vendor_name + ".idx")));
    CHECK(read_file(installed_vendor_dir / "vendor.yaml").find(target_version) != std::string::npos);

    listener.reset();
    fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    REQUIRE(fx.wait(listener));
    REQUIRE(listener.reconfigurations().has_value());
    CHECK(listener.reconfigurations()->empty());
}

TEST_CASE("PresetUpdater checks forced reconfigurations of installed profiles", "[preset_updater]")
{
    Fixture fx;
    CaptureListener listener(fx.interactor());

    SECTION("an app-compatible installed vendor needs no forced reconfiguration")
    {
        fx.put_installed("100");
        fx.interactor().check_forced_reconfigurations();
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->empty());
    }

    SECTION("an app-incompatible newer installed vendor is a forced downgrade")
    {
        fx.put_installed("301");
        fx.interactor().check_forced_reconfigurations();
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->forced_downgrades().size() == 1);
        CHECK(listener.reconfigurations()->forced_downgrades().front().recommended_version == Slic3r::Semver{3, 0, 0});
        CHECK(listener.reconfigurations()->regular_updates().empty());
        CHECK(listener.reconfigurations()->forced_updates().empty());
        CHECK(listener.reconfigurations()->new_vendors().empty());
    }

    SECTION("a deselected source is checked, because its presets are installed all the same")
    {
        fx.put_installed("301");
        deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");
        fx.interactor().check_forced_reconfigurations();
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->forced_downgrades().size() == 1);
        CHECK(listener.reconfigurations()->forced_downgrades().front().vendor_repo_id == k_repo_name);
    }

    SECTION("presets of a source no repository offers are skipped without complaint")
    {
        fx.put_installed_as("vanished_repo", "301");
        fx.interactor().check_forced_reconfigurations();
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->empty());
        CHECK(listener.warnings().empty());
    }

    SECTION("an unreadable index does not silence the other sources")
    {
        fx.put_installed("301");
        add_source_to_manifest(fx.shared_runtime_path / "RepositoryManifest.json", "broken_repo");

        const fs::path broken = fx.installed_path / "broken_repo";
        fs::create_directories(broken);
        {
            boost::nowide::ofstream file(broken / "Garbage.idx");
            file << "this is not an index";
        }

        fx.interactor().check_forced_reconfigurations();
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->forced_downgrades().size() == 1);
        CHECK(listener.reconfigurations()->forced_downgrades().front().vendor_repo_id == k_repo_name);
    }
}

TEST_CASE("PresetUpdater offers to remove unusable presets no source can replace", "[preset_updater]")
{
    Fixture fx;
    CaptureListener listener(fx.interactor());

    fx.put_installed("301");

    SECTION("a check that is not told about the source reports nothing for it")
    {
        deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");
        fx.put_server("300");
        fx.interactor().build_update_sync_and_reconfiguration_check(
            true, PresetUpdater::VerboseStyle::NoProgress, true,
            PresetUpdater::SourceListSync::UseStored
        );
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->empty());
    }

    SECTION("a required deselected source is never fetched, so removal is the only offer")
    {
        deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");
        fx.put_server("300");
        fx.interactor().build_update_sync_and_reconfiguration_check(
            true, PresetUpdater::VerboseStyle::NoProgress, true,
            PresetUpdater::SourceListSync::UseStored, {k_repo_name}
        );
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());

        CHECK_FALSE(fs::exists(fx.staged_path / k_repo_name / k_vendor_name));
        CHECK_FALSE(fs::exists(fx.staged_path / k_repo_name / (k_vendor_name + ".idx")));
        CHECK(listener.reconfigurations()->forced_downgrades().empty());
        REQUIRE(listener.reconfigurations()->removals().size() == 1);
        const auto& removal = listener.reconfigurations()->removals().front();
        CHECK(removal.vendor_repo_id == k_repo_name);
        CHECK(removal.vendor_id == k_vendor_name);
        CHECK(removal.current_version == Slic3r::Semver{3, 0, 1});

        const auto reconfigurations = *listener.reconfigurations();
        listener.reset();
        fx.interactor().perform_reconfigurations(reconfigurations);
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        CHECK(listener.performed());

        CHECK_FALSE(fs::exists(fx.installed_path / k_repo_name / k_vendor_name));
        CHECK_FALSE(fs::exists(fx.installed_path / k_repo_name / (k_vendor_name + ".idx")));
    }

    SECTION("a selected source that can stage nothing is offered the same removal")
    {
        fx.interactor().build_update_sync_and_reconfiguration_check(
            false, PresetUpdater::VerboseStyle::NoProgress, true,
            PresetUpdater::SourceListSync::UseStored
        );
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        REQUIRE(listener.reconfigurations()->removals().size() == 1);
        CHECK(listener.reconfigurations()->removals().front().vendor_id == k_vendor_name);
    }

    SECTION("a source whose sync failed is left alone until it can be reached")
    {
        fx.interactor().build_update_sync_and_reconfiguration_check(
            true, PresetUpdater::VerboseStyle::NoProgress, true,
            PresetUpdater::SourceListSync::UseStored
        );
        REQUIRE(fx.wait(listener));
        REQUIRE_FALSE(listener.got_error());
        REQUIRE(listener.reconfigurations().has_value());
        CHECK(listener.reconfigurations()->removals().empty());
        CHECK(fs::exists(fx.installed_path / k_repo_name / k_vendor_name));
    }
}

TEST_CASE("PresetUpdater does not offer removal while the presets are usable", "[preset_updater]")
{
    Fixture fx;
    CaptureListener listener(fx.interactor());

    fx.put_installed("100");
    deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");

    fx.interactor().build_update_sync_and_reconfiguration_check(
        true, PresetUpdater::VerboseStyle::NoProgress, true,
        PresetUpdater::SourceListSync::UseStored, {k_repo_name}
    );
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.reconfigurations().has_value());
    CHECK(listener.reconfigurations()->empty());
}

TEST_CASE("PresetUpdater drops what a source staged once it is switched off", "[preset_updater]")
{
    Fixture fx;
    CaptureListener listener(fx.interactor());

    fx.put_installed("301");
    fx.put_server("300");

    fx.interactor().build_update_sync_and_reconfiguration_check(
        true, PresetUpdater::VerboseStyle::NoProgress, true,
        PresetUpdater::SourceListSync::UseStored, {k_repo_name}
    );
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.reconfigurations().has_value());
    REQUIRE(listener.reconfigurations()->forced_downgrades().size() == 1);
    REQUIRE(fs::exists(fx.staged_path / k_repo_name / (k_vendor_name + ".idx")));

    deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");
    listener.reset();

    fx.interactor().build_update_sync_and_reconfiguration_check(
        true, PresetUpdater::VerboseStyle::NoProgress, true,
        PresetUpdater::SourceListSync::UseStored, {k_repo_name}
    );
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    REQUIRE(listener.reconfigurations().has_value());

    CHECK_FALSE(fs::exists(fx.staged_path / k_repo_name / (k_vendor_name + ".idx")));
    CHECK(listener.reconfigurations()->forced_downgrades().empty());
    REQUIRE(listener.reconfigurations()->removals().size() == 1);
    CHECK(listener.reconfigurations()->removals().front().vendor_id == k_vendor_name);
}


TEST_CASE("PresetUpdater cleans up staged files and keeps installed profiles", "[preset_updater]")
{
    Fixture fx;
    fx.put_installed("100");
    fx.put_staged("100");

    const fs::path stray = fx.staged_path / "stray.tmp";
    {
        boost::nowide::ofstream f(stray);
        f << "leftover";
    }
    REQUIRE(fs::exists(fx.staged_path / k_repo_name));
    REQUIRE(fs::exists(stray));

    CaptureListener listener(fx.interactor());

    fx.interactor().cleanup_update_sync();
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());

    CHECK_FALSE(fs::exists(fx.staged_path / k_repo_name));
    CHECK_FALSE(fs::exists(stray));
    CHECK(fs::is_empty(fx.staged_path));

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    CHECK(fs::exists(installed_vendor_dir / "vendor.yaml"));
    CHECK(fs::exists(fx.installed_path / k_repo_name / (k_vendor_name + ".idx")));
}

TEST_CASE("PresetUpdater stays off the network when the app config forbids it", "[preset_updater]")
{
    Fixture fx;
    // The server offers 1.0.1, what came with the installation only 1.0.0. Anything the check
    // reports above 1.0.0 could only have been downloaded.
    fx.put_resources("100");
    fx.put_server("101");

    CaptureListener listener(fx.interactor());

    fx.interactor().build_update_sync_and_reconfiguration_check(
        false, PresetUpdater::VerboseStyle::NoProgress, true
    );
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    // A source that is deliberately not contacted is not a source that failed.
    CHECK(listener.warnings().empty());
    REQUIRE(listener.reconfigurations().has_value());
    REQUIRE(listener.reconfigurations()->new_vendors().size() == 1);
    CHECK(
        listener.reconfigurations()->new_vendors().front().recommended_version
        == Slic3r::Semver{1, 0, 0}
    );

    const auto reconfigurations = *listener.reconfigurations();

    listener.reset();
    fx.interactor().perform_reconfigurations(reconfigurations);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    CHECK(listener.performed());

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    CHECK(fs::exists(installed_vendor_dir / "vendor.yaml"));
    CHECK(read_file(installed_vendor_dir / "vendor.yaml").find("1.0.0") != std::string::npos);
}

TEST_CASE("PresetUpdater manages local sources when the app config forbids the network", "[preset_updater]")
{
    Fixture fx;

    CaptureListener listener(fx.interactor());

    const fs::path local_zip = fx.resources_profile_path / "local_repo.zip";
    REQUIRE(fs::exists(local_zip));

    fx.interactor().add_local_repository(local_zip);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());

    std::string local_uuid;
    for (const auto& repo : listener.repos()) {
        if (!repo.descriptor.unzipped_data_path.empty()) {
            local_uuid = repo.descriptor.uuid;
        }
    }
    REQUIRE_FALSE(local_uuid.empty());

    listener.reset();
    fx.interactor().list_repositories(false);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    CHECK(listener.warnings().empty());
    CHECK(
        std::any_of(
            listener.repos().begin(),
            listener.repos().end(),
            [&local_uuid](const PresetUpdater::SharedPresetUpdaterRepositoryInfo& repo)
            { return repo.descriptor.uuid == local_uuid; }
        )
    );

    listener.reset();
    fx.interactor().remove_local_repository(local_uuid);
    REQUIRE(fx.wait(listener));
    REQUIRE_FALSE(listener.got_error());
    CHECK_FALSE(fs::exists(fx.data_dir / "local_repositories" / local_uuid));
}

TEST_CASE("PresetUpdater reports an unhandled worker exception as an error", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");

    CaptureListener listener(fx.interactor());

    SECTION("a std::exception is surfaced with its message")
    {
        fx.set_mutator([](const std::string&, std::string&, unsigned&) {
            throw std::runtime_error("injected worker failure");
        });
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE(listener.got_error());
        CHECK(listener.error_body().find("injected worker failure") != std::string::npos);
    }

    SECTION("a non-standard exception is surfaced as unknown")
    {
        fx.set_mutator([](const std::string&, std::string&, unsigned&) {
            throw 42;
        });
        fx.interactor().build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
        REQUIRE(fx.wait(listener));
        REQUIRE(listener.got_error());
        CHECK(listener.error_body().find("unknown exception") != std::string::npos);
    }
}

TEST_CASE("PresetUpdater cancels a running operation and stays reusable", "[preset_updater]")
{
    Fixture fx;
    fx.put_server("100");

    std::atomic<bool> first_request_seen{false};
    fx.set_mutator([&first_request_seen](const std::string&, std::string&, unsigned&) {
        if (!first_request_seen.exchange(true)) {
            // Hold the worker inside its first network request so the main thread has a
            // deterministic window to request a stop before the post-sync checkpoint.
            std::this_thread::sleep_for(500ms);
        }
    });

    CaptureListener listener(fx.interactor());

    const PresetUpdater::JobId canceled = fx.interactor()
        .build_update_sync_and_reconfiguration_check(true, PresetUpdater::VerboseStyle::NoProgress, true);
    std::this_thread::sleep_for(100ms);
    fx.interactor().on_notification_cancel();

    // on_notification_cancel no longer joins the worker, so the job reports Canceled once it
    // unwinds. It must never produce a payload after being canceled.
    REQUIRE(fx.wait_for_finished(listener, 1));
    CHECK(first_request_seen.load());
    CHECK(listener.state_of(canceled) == PresetUpdater::JobState::Canceled);
    CHECK_FALSE(listener.has_result());

    fx.set_mutator({});
    listener.reset();
    fx.interactor().list_repositories(true);
    REQUIRE(fx.wait(listener));
    CHECK_FALSE(listener.got_error());
    CHECK(listener.repos().size() == 1);
}
