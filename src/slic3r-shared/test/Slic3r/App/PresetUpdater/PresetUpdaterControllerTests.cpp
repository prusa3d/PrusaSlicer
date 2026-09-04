#include "Slic3r/Biz/PresetUpdater/PresetUpdaterTestFixture.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterController.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

using namespace Slic3r::Biz::PresetUpdater::TestSupport;
using namespace std::chrono_literals;

using Slic3r::Semver;
using Slic3r::App::AppServices;
using Slic3r::App::PopNotification::PopNotificationCenter;
using Slic3r::App::PresetUpdater::ControllerActivity;
using Slic3r::App::PresetUpdater::ControllerEnvironment;
using Slic3r::App::PresetUpdater::InstallState;
using Slic3r::App::PresetUpdater::IPresetUpdaterControllerListener;
using Slic3r::App::PresetUpdater::PresetUpdaterController;
using Slic3r::App::PresetUpdater::SourceRowState;
using Slic3r::Biz::PresetUpdater::VendorReconfigurationState;

namespace fs = boost::filesystem;

namespace {

/// PopNotificationObservableList asks for a render whenever it updates an entry in place.
struct RenderRequestScope : Slic3r::Biz::Platform::IRenderRequestHandler
{
    RenderRequestScope()
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_render_request_handler(this);
    }

    ~RenderRequestScope() override
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_render_request_handler(nullptr);
    }

    void request_render() override {}
};

struct NotificationCenterScope
{
    explicit NotificationCenterScope(Slic3r::Biz::ProjectInteractor& project_interactor)
    {
        AppServices::instance().set_pop_notification_center(
            std::make_unique<PopNotificationCenter>(project_interactor)
        );
    }

    ~NotificationCenterScope()
    {
        AppServices::instance().set_pop_notification_center(nullptr);
    }

    NotificationCenterScope(const NotificationCenterScope&)            = delete;
    NotificationCenterScope& operator=(const NotificationCenterScope&) = delete;
};

struct ChangeCounter : IPresetUpdaterControllerListener
{
    size_t changes{0};

    void on_preset_updater_changed() override { ++changes; }
};

bool notification_shown(Slic3r::App::PopNotification::PopNotificationType type)
{
    using namespace Slic3r::App::PopNotification;

    PopNotificationObservableList& list =
        AppServices::instance().pop_notification_center().observable_list();
    for (size_t index = 0; index < list.size(); ++index) {
        if (list.at(index).type == type) {
            return true;
        }
    }
    return false;
}

bool status_notification_shown()
{
    return notification_shown(Slic3r::App::PopNotification::PopNotificationType::PresetUpdaterStatus);
}

bool updates_notification_shown()
{
    return notification_shown(
        Slic3r::App::PopNotification::PopNotificationType::PresetUpdateAvailable
    );
}

/// Member order matters: the controller is destroyed before the services it reports through.
struct ControllerFixture : Fixture
{
    explicit ControllerFixture(bool with_timer_queue = false) :
        Fixture("controller"),
        notification_center(project_interactor)
    {
        if (with_timer_queue) {
            timer_queue.emplace(dispatcher);
        }

        ControllerEnvironment environment;
        environment.timer_queue    = timer_queue.has_value() ? &*timer_queue : nullptr;
        environment.online_allowed = [this]() { return online; };
        environment.request_render = [this]() { ++render_requests; };

        controller.emplace(interactor(), std::move(environment));
        controller->add_listener<IPresetUpdaterControllerListener>(&counter);
        controller->set_forced_state_callback(
            [this](bool has_forced)
            {
                forced_answer = has_forced;
                ++forced_answers;
            }
        );
        controller->set_show_dialog_callback([this]() { ++dialog_requests; });
        controller->set_presets_installed_callback([this]() { ++install_reports; });
    }

    /// Pumps until nothing the controller submitted is still running.
    bool settle(std::chrono::seconds timeout = 20s)
    {
        return pump([this]() { return !controller->busy(); }, timeout);
    }

    const SourceRowState& online_row(size_t index = 0)
    {
        return controller->online_sources().at(index);
    }

    bool online{true};
    size_t dialog_requests{0};
    size_t install_reports{0};
    size_t render_requests{0};
    size_t forced_answers{0};
    std::optional<bool> forced_answer;

    ChangeCounter counter;
    RenderRequestScope render_scope;
    NotificationCenterScope notification_center;
    std::optional<Slic3r::Biz::Platform::TimerQueue> timer_queue;
    std::optional<PresetUpdaterController> controller;
};

} // namespace

TEST_CASE("PresetUpdaterController answers the launch check when nothing is forced", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->start();
    REQUIRE(fx.settle());

    REQUIRE(fx.forced_answer.has_value());
    CHECK_FALSE(*fx.forced_answer);
    CHECK(fx.forced_answers == 1);
    CHECK_FALSE(fx.controller->forced_mode());
    CHECK_FALSE(fx.controller->warned());
    CHECK(fx.dialog_requests == 0);
    CHECK(fx.controller->activity() == ControllerActivity::None);

    // The launch check answers one question and asks no other: nothing was listed or checked.
    CHECK(fx.controller->online_sources().size() == 0);
    CHECK(fx.controller->local_sources().size() == 0);
    CHECK_FALSE(fx.controller->has_actionable_updates());
}

TEST_CASE("PresetUpdaterController answers the launch check even when the work is refused", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.interactor().shutdown();

    fx.controller->start();

    REQUIRE(fx.forced_answer.has_value());
    CHECK_FALSE(*fx.forced_answer);
    CHECK(fx.controller->warned());
    CHECK(fx.controller->activity() == ControllerActivity::None);
    CHECK(fx.dialog_requests == 0);
}

TEST_CASE("PresetUpdaterController runs the background check without narrating it", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->start();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->online_sources().size() == 0);

    fx.controller->start_background_check();
    REQUIRE(fx.controller->activity() == ControllerActivity::Checking);
    CHECK_FALSE(status_notification_shown());

    REQUIRE(fx.settle());
    CHECK_FALSE(status_notification_shown());
    CHECK(updates_notification_shown());
}

TEST_CASE("PresetUpdaterController narrates a check the user asked for", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    CHECK(status_notification_shown());

    REQUIRE(fx.pump([&fx]() { return fx.controller->activity() == ControllerActivity::Checking; }));
    CHECK(status_notification_shown());

    REQUIRE(fx.settle());
    CHECK_FALSE(status_notification_shown());
}

TEST_CASE("PresetUpdaterController asks for the dialog when a forced reconfiguration is found", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    fx.controller->start();
    REQUIRE(fx.settle());

    REQUIRE(fx.forced_answer.has_value());
    CHECK(*fx.forced_answer);
    CHECK(fx.controller->forced_mode());
    CHECK(fx.dialog_requests == 1);
}

TEST_CASE("PresetUpdaterController holds the application for a source that is not selected", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");

    fx.controller->start();
    REQUIRE(fx.settle());
    REQUIRE(fx.forced_answer.has_value());
    REQUIRE(*fx.forced_answer);
    REQUIRE(fx.forced_answers == 1);

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK(fx.controller->forced_mode());
    CHECK_FALSE(fx.online_row().selected);
    CHECK(fx.online_row().update_state == SourceRowState::UpdateState::HasUpdates);
    CHECK(fx.online_row().counts.required == 1);
    CHECK(fx.controller->has_required_updates());
}

TEST_CASE("PresetUpdaterController removes unusable presets of a source that is not selected", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    deselect_every_source(fx.shared_runtime_path / "RepositoryManifest.json");

    fx.controller->start();
    REQUIRE(fx.settle());
    REQUIRE(*fx.forced_answer);

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->has_required_updates());

    fx.controller->update_required();
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.controller->forced_mode());
    REQUIRE(fx.forced_answers == 2);
    CHECK_FALSE(*fx.forced_answer);
    CHECK(fx.install_reports > 0);

    CHECK_FALSE(fs::exists(fx.installed_path / k_repo_name / k_vendor_name));
    CHECK_FALSE(fx.online_row().selected);
}

TEST_CASE("PresetUpdaterController holds the application while the forced state stands", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    fx.controller->start();
    REQUIRE(fx.settle());
    REQUIRE(*fx.forced_answer);

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK(fx.controller->forced_mode());
    CHECK(*fx.forced_answer);
    CHECK(fx.forced_answers == 1);
}

TEST_CASE("PresetUpdaterController lists and checks the sources when the dialog opens", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    CHECK(fx.controller->dialog_open());
    CHECK(fx.controller->busy());
    REQUIRE(fx.settle());

    CHECK(fx.controller->activity() == ControllerActivity::None);
    CHECK(fx.controller->fully_checked());
    CHECK_FALSE(fx.controller->warned());
    CHECK(fx.controller->has_actionable_updates());
    CHECK_FALSE(fx.controller->has_required_updates());
    CHECK(fx.counter.changes > 0);
    CHECK(fx.render_requests > 0);

    REQUIRE(fx.controller->online_sources().size() == 1);
    CHECK(fx.controller->local_sources().size() == 0);

    const SourceRowState& row = fx.online_row();
    CHECK(row.id == k_repo_name);
    CHECK(row.selected);
    CHECK_FALSE(row.install_locked);
    CHECK_FALSE(row.selection_locked);
    CHECK_FALSE(row.check_failed);
    CHECK(row.update_state == SourceRowState::UpdateState::HasUpdates);
    CHECK(row.counts.new_vendors == 1);
    CHECK(row.counts.pending() == 1);

    REQUIRE(row.vendors != nullptr);
    REQUIRE(row.vendors->size() == 1);
    CHECK(row.vendors->at(0).repo_id == k_repo_name);
    CHECK(row.vendors->at(0).vendor_id == k_vendor_name);
    CHECK(row.vendors->at(0).state == VendorReconfigurationState::NewVendor);
    CHECK(row.vendors->at(0).install_state == InstallState::Idle);
    CHECK(row.vendors->at(0).recommended_version == Semver{1, 0, 0});
}

TEST_CASE("PresetUpdaterController installs everything on offer", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->has_actionable_updates());

    fx.controller->update_everything();
    CHECK(fx.controller->activity() == ControllerActivity::Installing);
    REQUIRE(fx.online_row().vendors->at(0).install_state != InstallState::Idle);
    REQUIRE(fx.settle());

    CHECK(fx.install_reports == 1);
    CHECK_FALSE(fx.controller->warned());
    CHECK_FALSE(fx.controller->has_actionable_updates());

    const SourceRowState& row = fx.online_row();
    CHECK(row.update_state == SourceRowState::UpdateState::UpToDate);
    CHECK(row.counts.pending() == 0);
    REQUIRE(row.vendors->size() == 1);
    CHECK(row.vendors->at(0).install_state == InstallState::Done);
    CHECK(row.vendors->at(0).error_text.empty());

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    CHECK(fs::exists(installed_vendor_dir / "vendor.yaml"));
    CHECK(read_file(installed_vendor_dir / "vendor.yaml").find("1.0.0") != std::string::npos);
}

TEST_CASE("PresetUpdaterController installs one vendor at a time", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.online_row().vendors->size() == 1);

    fx.controller->update_vendor(k_repo_name, k_vendor_name);
    REQUIRE(fx.settle());

    CHECK(fx.online_row().vendors->at(0).install_state == InstallState::Done);
    CHECK(fs::exists(fx.installed_path / k_repo_name / k_vendor_name / "vendor.yaml"));
}

TEST_CASE("PresetUpdaterController counts down what one source has left", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_resources("100");
    copy_dir_content_local(
        fx.resources_profile_path / "second_vendor" / k_repo_name, fx.resources_repo_path
    );
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.online_row().vendors->size() == 2);
    REQUIRE(fx.online_row().counts.new_vendors == 2);

    fx.controller->update_vendor(k_repo_name, k_vendor_name);
    REQUIRE(fx.settle());

    CHECK(fx.online_row().counts.new_vendors == 1);
    CHECK(fx.online_row().counts.pending() == 1);

    fx.controller->on_dialog_closed();
    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK(fx.online_row().vendors->size() == 1);
    CHECK(fx.online_row().counts.new_vendors == 1);
}

TEST_CASE("PresetUpdaterController starts over when the dialog is reopened", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    fx.controller->update_everything();
    REQUIRE(fx.settle());
    REQUIRE(fx.online_row().vendors->at(0).install_state == InstallState::Done);

    fx.controller->on_dialog_closed();
    fx.controller->on_dialog_opened();

    CHECK(fx.controller->busy());
    CHECK_FALSE(fx.controller->fully_checked());
    CHECK(fx.online_row().update_state == SourceRowState::UpdateState::Waiting);
    CHECK(fx.online_row().vendors->size() == 0);

    REQUIRE(fx.settle());

    CHECK(fx.controller->fully_checked());
    CHECK_FALSE(fx.controller->warned());
    CHECK(fx.online_row().update_state == SourceRowState::UpdateState::UpToDate);
    CHECK(fx.online_row().vendors->size() == 0);
}

TEST_CASE("PresetUpdaterController releases the application once the forced vendors are installed", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    fx.controller->start();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->forced_mode());

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK(fx.controller->forced_mode());
    CHECK(fx.controller->has_required_updates());
    CHECK(fx.dialog_requests == 1);

    REQUIRE(fx.controller->online_sources().size() == 1);
    CHECK(fx.online_row().install_locked);
    CHECK_FALSE(fx.online_row().selection_locked);
    CHECK(fx.online_row().required);
    REQUIRE(fx.online_row().vendors->size() == 1);
    CHECK(fx.online_row().vendors->at(0).state == VendorReconfigurationState::ForcedDowngrade);
    CHECK(fx.online_row().vendors->at(0).install_locked);

    fx.controller->update_required();
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.controller->forced_mode());
    CHECK_FALSE(fx.controller->has_required_updates());
    CHECK(fx.install_reports == 1);
    CHECK(fx.online_row().vendors->at(0).install_state == InstallState::Done);
    CHECK_FALSE(fx.online_row().install_locked);
    CHECK_FALSE(fx.online_row().selection_locked);
    CHECK_FALSE(fx.online_row().required);

    const fs::path installed_vendor_dir = fx.installed_path / k_repo_name / k_vendor_name;
    CHECK(read_file(installed_vendor_dir / "vendor.yaml").find("3.0.0") != std::string::npos);
}

TEST_CASE("PresetUpdaterController refuses source-list changes while forced", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    fx.controller->start();
    REQUIRE(fx.settle());
    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->forced_mode());

    const std::string uuid = fx.online_row().uuid;
    fx.counter.changes     = 0;

    fx.controller->add_local_repository(fx.resources_profile_path / "local_repo.zip");
    fx.controller->remove_local_repository(uuid);

    CHECK(fx.controller->activity() == ControllerActivity::None);
    CHECK(fx.counter.changes == 0);
    CHECK(fx.controller->local_sources().size() == 0);
}

TEST_CASE("PresetUpdaterController offers a removal once the forced source is switched off", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_installed("301");
    fx.put_server("300");

    fx.controller->start();
    REQUIRE(fx.settle());
    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->forced_mode());
    REQUIRE(fx.online_row().vendors->size() == 1);
    REQUIRE(fx.online_row().vendors->at(0).state == VendorReconfigurationState::ForcedDowngrade);

    const std::string uuid = fx.online_row().uuid;
    fx.controller->set_source_selected(uuid, false);
    REQUIRE(fx.settle());

    CHECK(fx.controller->forced_mode());
    CHECK_FALSE(fx.online_row().selected);
    CHECK(fx.online_row().required);
    REQUIRE(fx.online_row().vendors->size() == 1);
    CHECK(fx.online_row().vendors->at(0).state == VendorReconfigurationState::RemoveVendor);
    CHECK(fx.controller->has_required_updates());

    fx.controller->update_required();
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.controller->forced_mode());
    CHECK(fx.forced_answers == 2);
    CHECK_FALSE(fs::exists(fx.installed_path / k_repo_name / k_vendor_name));
}

TEST_CASE("PresetUpdaterController writes a selection change through without a timer", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->online_sources().size() == 1);

    const std::string uuid = fx.online_row().uuid;
    fx.controller->set_source_selected(uuid, false);
    CHECK(fx.controller->busy());
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.online_row().selected);
    CHECK(fx.online_row().update_state == SourceRowState::UpdateState::NotChecked);
    CHECK(fx.online_row().vendors->size() == 0);
    CHECK_FALSE(fx.controller->has_actionable_updates());

    const std::string manifest = read_file(fx.shared_runtime_path / "RepositoryManifest.json");
    CHECK(manifest.find("\"selected\":false") != std::string::npos);
}

TEST_CASE("PresetUpdaterController debounces a burst of selection changes into one write", "[preset_updater][controller]")
{
    ControllerFixture fx(true);
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());
    REQUIRE(fx.controller->online_sources().size() == 1);

    const std::string uuid = fx.online_row().uuid;
    fx.controller->set_source_selected(uuid, false);
    fx.controller->set_source_selected(uuid, true);
    fx.controller->set_source_selected(uuid, false);

    CHECK(fx.controller->activity() == ControllerActivity::None);
    CHECK_FALSE(fx.online_row().selected);

    fx.controller->on_dialog_closed();
    CHECK(fx.controller->busy());
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.online_row().selected);
    const std::string manifest = read_file(fx.shared_runtime_path / "RepositoryManifest.json");
    CHECK(manifest.find("\"selected\":false") != std::string::npos);
}

TEST_CASE("PresetUpdaterController adds and removes a local source", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    const fs::path local_zip = fx.resources_profile_path / "local_repo.zip";
    REQUIRE(fs::exists(local_zip));

    fx.controller->add_local_repository(local_zip);
    CHECK(fx.controller->busy());
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.controller->warned());
    REQUIRE(fx.controller->local_sources().size() == 1);

    const std::string uuid = fx.controller->local_sources().at(0).uuid;
    REQUIRE_FALSE(uuid.empty());
    CHECK_FALSE(fx.controller->local_sources().at(0).zip_path.empty());
    CHECK(fs::exists(fx.data_dir / "local_repositories" / uuid));

    fx.controller->remove_local_repository(uuid);
    REQUIRE(fx.settle());

    CHECK(fx.controller->local_sources().size() == 0);
    CHECK_FALSE(fs::exists(fx.data_dir / "local_repositories" / uuid));
}

TEST_CASE("PresetUpdaterController reports a check it could not finish", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");
    fx.set_mutator(
        [](const std::string& url, std::string&, unsigned&)
        {
            if (!url.ends_with("v2/repos")) {
                throw std::runtime_error("injected check failure");
            }
        }
    );

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK(fx.controller->warned());
    CHECK_FALSE(fx.controller->fully_checked());
    REQUIRE(fx.controller->online_sources().size() == 1);
    CHECK(fx.online_row().update_state == SourceRowState::UpdateState::NotChecked);
    CHECK_FALSE(fx.controller->has_actionable_updates());
}

TEST_CASE("PresetUpdaterController stages from the installation when it may not go online", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.online = false;
    fx.put_resources("100");
    fx.put_server("101");
    fx.set_mutator(
        [](const std::string&, std::string&, unsigned&)
        { throw std::runtime_error("the network was used"); }
    );

    fx.controller->on_dialog_opened();
    REQUIRE(fx.settle());

    CHECK_FALSE(fx.controller->online_allowed());
    CHECK_FALSE(fx.controller->warned());
    CHECK(fx.controller->fully_checked());

    REQUIRE(fx.controller->online_sources().size() == 1);
    const SourceRowState& row = fx.online_row();
    CHECK(row.counts.new_vendors == 1);
    REQUIRE(row.vendors->size() == 1);
    CHECK(row.vendors->at(0).recommended_version == Semver{1, 0, 0});
}

TEST_CASE("PresetUpdaterController stays quiet when an operation changes nothing", "[preset_updater][controller]")
{
    ControllerFixture fx;

    fx.controller->on_dialog_closed();

    CHECK(fx.counter.changes == 0);
    CHECK(fx.render_requests == 0);
    CHECK_FALSE(fx.controller->dialog_open());
    CHECK(fx.controller->activity() == ControllerActivity::None);
}

TEST_CASE("PresetUpdaterController does nothing once it has shut down", "[preset_updater][controller]")
{
    ControllerFixture fx;
    fx.put_server("100");

    fx.controller->shutdown();
    fx.counter.changes = 0;

    fx.controller->start();
    fx.controller->on_dialog_opened();
    fx.controller->update_everything();
    fx.controller->add_local_repository(fx.resources_profile_path / "local_repo.zip");

    CHECK(fx.controller->activity() == ControllerActivity::None);
    CHECK(fx.counter.changes == 0);
    CHECK_FALSE(fx.forced_answer.has_value());
    CHECK(fx.dialog_requests == 0);
    CHECK(fx.controller->online_sources().size() == 0);
}
