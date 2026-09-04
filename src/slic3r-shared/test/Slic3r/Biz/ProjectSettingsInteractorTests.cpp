#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/IMdb.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/nowide/filesystem.hpp>

namespace Slic3r::Biz::Mock {

struct ColorsChangedListener : public IColorsChangedListener
{
    MAKE_MOCK3(
        on_colors_changed,
        void(Domain::SelectionId, Domain::SelectionId, const std::vector<Domain::ColorRGB>&)
    );
};

} // namespace Slic3r::Biz::Mock

namespace Slic3r {

// ---------------------------------------------------------------------------
// palette_color tests — pure function, no workbench needed
// ---------------------------------------------------------------------------

TEST_CASE("palette_color returns non-empty strings", "[ProjectSettingsInteractor]")
{
    using namespace Slic3r::Biz;

    for (int slot = 0; slot < 32; ++slot) {
        const std::string color = ProjectSettingsInteractor::palette_color(slot);
        REQUIRE(!color.empty());
        REQUIRE(color.front() == '#');
    }
}

TEST_CASE("palette_color wraps around every 16 slots", "[ProjectSettingsInteractor]")
{
    using namespace Slic3r::Biz;

    for (int slot = 0; slot < 16; ++slot) {
        REQUIRE(
            ProjectSettingsInteractor::palette_color(slot)
            == ProjectSettingsInteractor::palette_color(slot + 16)
        );
    }
}

// ---------------------------------------------------------------------------
// Integration tests using a real ProjectInteractor fixture
// ---------------------------------------------------------------------------

struct ProjectSettingsInteractorFixture
{
    ProjectSettingsInteractorFixture()
    {
        using namespace Slic3r::Biz;
        namespace fs = boost::filesystem;

        boost::nowide::nowide_filesystem();

        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));
        Slic3r::set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );
    }

    ~ProjectSettingsInteractorFixture()
    {
        dispatcher.close();
    }

    std::unique_ptr<Slic3r::Biz::SecretStoreDummy> store_dummy =
        std::make_unique<Slic3r::Biz::SecretStoreDummy>();
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Domain::Workbench workbench;
    Slic3r::Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
};

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Colors are initialized when a new project is created",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;
    using namespace trompeloeil;

    Mock::ColorsChangedListener listener;
    project_interactor.project_settings_interactor()
        .add_listener<IColorsChangedListener>(&listener);

    std::vector<Domain::ColorRGB> received_colors;
    ALLOW_CALL(listener, on_colors_changed(_, _, _))
        .LR_SIDE_EFFECT(received_colors = _3);

    const Domain::SelectionId project_id = project_interactor.new_project();
    (void)project_id;

    // After creating a new project, at least one notification should have been
    // sent with non-empty colors.
    REQUIRE(!received_colors.empty());
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "set_color_from_user locks the slot",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;

    const Domain::SelectionId project_id = project_interactor.new_project();
    (void)project_id;

    const Domain::SelectionId cc_id =
        project_interactor.selected_config_container_id();
    REQUIRE(cc_id != Domain::INVALID_ID);

    auto& psi = project_interactor.project_settings_interactor();

    // Get initial colors.
    std::vector<Domain::ColorRGB> initial_colors = psi.get_colors(cc_id);
    REQUIRE(!initial_colors.empty());

    // Lock slot 0 with a custom color.
    const std::string custom_hex = "#ABCDEF";
    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    psi.set_color_from_user(cc_id, 0, custom_hex);

    const std::vector<Domain::ColorRGB> after_lock = psi.get_colors(cc_id);
    REQUIRE(!after_lock.empty());
    REQUIRE(after_lock[0] == custom_color);

    // Unlock slot 0 by passing empty string -> should restore auto color.
    psi.set_color_from_user(cc_id, 0, "");

    const std::vector<Domain::ColorRGB> after_unlock = psi.get_colors(cc_id);
    REQUIRE(!after_unlock.empty());
    // The color must differ from the user-chosen value (it's auto-resolved).
    REQUIRE(after_unlock[0] != custom_color);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Multiple config containers have independent color state",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;

    project_interactor.new_project();

    auto& psi = project_interactor.project_settings_interactor();
    const Domain::SelectionId cc0_id = project_interactor.selected_config_container_id();
    REQUIRE(cc0_id != Domain::INVALID_ID);

    // Set a custom color on the first container's slot 0.
    const std::string custom_hex = "#112233";
    psi.set_color_from_user(cc0_id, 0, custom_hex);

    // Add a second config container.
    const Domain::SelectionId cc1_id = project_interactor.add_config_container();
    REQUIRE(cc1_id != Domain::INVALID_ID);
    REQUIRE(cc1_id != cc0_id);

    // The second container should have its own colors (auto-resolved, not the custom one).
    const auto colors_cc1 = psi.get_colors(cc1_id);
    REQUIRE(!colors_cc1.empty());

    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    REQUIRE(colors_cc1[0] != custom_color);

    // The first container should still have the custom color.
    const auto colors_cc0 = psi.get_colors(cc0_id);
    REQUIRE(!colors_cc0.empty());
    REQUIRE(colors_cc0[0] == custom_color);
}

} // namespace Slic3r
