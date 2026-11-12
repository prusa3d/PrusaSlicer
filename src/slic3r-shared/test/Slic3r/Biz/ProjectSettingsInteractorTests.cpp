#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
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

#include <boost/filesystem.hpp>
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

    // Selects the first printer whose hw config matches the predicate.
    template<typename Predicate>
    void select_printer(Predicate&& predicate)
    {
        auto& preset_interactor = project_interactor.preset_interactor();
        const auto& printer_items = preset_interactor.printer_configs().items();
        for (size_t i = 0, n = printer_items.size(); i < n; ++i) {
            const auto& item = printer_items.at(i);
            const auto& hw_config =
                workbench.preset_bundle().printer_configs.at(item.hw_printer_config_id);
            if (predicate(hw_config)) {
                preset_interactor.select_printer_config(item.hw_printer_config_id);
                return;
            }
        }
        FAIL("No printer matches the predicate");
    }

    void select_printer_with_technology(Domain::PrinterTechnology technology)
    {
        select_printer([technology](const Domain::Preset::HwPrinterConfig& hw_config)
                       { return hw_config.technology == technology; });
    }

    std::vector<std::string> stored_colors()
    {
        return project_interactor.selected_config_container()
            .project_settings()
            .items.opt("extruder_colour")
            .get<std::vector<std::string>>();
    }

    std::string stored_resin_color()
    {
        return project_interactor.selected_config_container()
            .sla_project_settings()
            .items.opt("resin_colour")
            .get<std::string>();
    }

    std::string sla_material_color()
    {
        return std::get<Domain::SLAMaterialSettings>(
                   project_interactor.preset_interactor().selected_printer_preset().materials.at(0).values
        )
            .items.opt("material_colour")
            .get<std::string>();
    }
};

Domain::ColorRGB decoded(const std::string& hex)
{
    Domain::ColorRGB color;
    REQUIRE(Biz::Algorithms::Color::decode_color(hex, color));
    return color;
}

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

    const Domain::SelectionId cc_id =
        project_interactor.selected_config_container_id();
    REQUIRE(cc_id != Domain::INVALID_ID);

    auto& psi = project_interactor.project_settings_interactor();

    // Get initial colors.
    std::vector<Domain::ColorRGB> initial_colors = psi.get_colors(project_id, cc_id);
    REQUIRE(!initial_colors.empty());

    // Lock slot 0 with a custom color.
    const std::string custom_hex = "#ABCDEF";
    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    psi.set_color_from_user(project_id, cc_id, 0, custom_hex);

    const std::vector<Domain::ColorRGB> after_lock = psi.get_colors(project_id, cc_id);
    REQUIRE(!after_lock.empty());
    REQUIRE(after_lock[0] == custom_color);

    // Unlock slot 0 by passing empty string -> should restore auto color.
    psi.set_color_from_user(project_id, cc_id, 0, "");

    const std::vector<Domain::ColorRGB> after_unlock = psi.get_colors(project_id, cc_id);
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

    const Domain::SelectionId project_id = project_interactor.new_project();

    auto& psi = project_interactor.project_settings_interactor();
    const Domain::SelectionId cc0_id = project_interactor.selected_config_container_id();
    REQUIRE(cc0_id != Domain::INVALID_ID);

    // Set a custom color on the first container's slot 0.
    const std::string custom_hex = "#112233";
    psi.set_color_from_user(project_id, cc0_id, 0, custom_hex);

    // Add a second config container.
    const Domain::SelectionId cc1_id = project_interactor.add_config_container();
    REQUIRE(cc1_id != Domain::INVALID_ID);
    REQUIRE(cc1_id != cc0_id);

    // The second container should have its own colors (auto-resolved, not the custom one).
    const auto colors_cc1 = psi.get_colors(project_id, cc1_id);
    REQUIRE(!colors_cc1.empty());

    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    REQUIRE(colors_cc1[0] != custom_color);

    // The first container should still have the custom color.
    const auto colors_cc0 = psi.get_colors(project_id, cc0_id);
    REQUIRE(!colors_cc0.empty());
    REQUIRE(colors_cc0[0] == custom_color);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "User color survives config container switch",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;

    const Domain::SelectionId project_id = project_interactor.new_project();

    auto& psi = project_interactor.project_settings_interactor();
    const Domain::SelectionId cc0_id = project_interactor.selected_config_container_id();
    REQUIRE(cc0_id != Domain::INVALID_ID);

    const std::string custom_hex = "#123456";
    psi.set_color_from_user(project_id, cc0_id, 0, custom_hex);

    const Domain::SelectionId cc1_id = project_interactor.add_config_container();
    REQUIRE(cc1_id != Domain::INVALID_ID);
    project_interactor.select_config_container(cc1_id);
    project_interactor.select_config_container(cc0_id);
    REQUIRE(project_interactor.selected_config_container_id() == cc0_id);

    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    const auto colors = psi.get_colors(project_id, cc0_id);
    REQUIRE(!colors.empty());
    REQUIRE(colors[0] == custom_color);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "User color survives material preset selection",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;

    const Domain::SelectionId project_id = project_interactor.new_project();

    auto& psi = project_interactor.project_settings_interactor();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    REQUIRE(cc_id != Domain::INVALID_ID);

    const std::string custom_hex = "#654321";
    psi.set_color_from_user(project_id, cc_id, 0, custom_hex);

    const Domain::ConfigContainer* cc =
        project_interactor.project(project_id).find_config_container(cc_id);
    REQUIRE(cc != nullptr);
    const std::string material_id = cc->selected_preset().materials.at(0).id;

    // Triggers the reconcile step, which must not overwrite non-empty colors.
    project_interactor.preset_interactor().select_material_preset(0, material_id);

    Domain::ColorRGB custom_color;
    Biz::Algorithms::Color::decode_color(custom_hex, custom_color);
    const auto colors = psi.get_colors(project_id, cc_id);
    REQUIRE(!colors.empty());
    REQUIRE(colors[0] == custom_color);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "reset_color_to_auto replaces the user color",
    "[ProjectSettingsInteractor]"
)
{
    using namespace Slic3r::Biz;

    const Domain::SelectionId project_id = project_interactor.new_project();

    auto& psi = project_interactor.project_settings_interactor();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    REQUIRE(cc_id != Domain::INVALID_ID);

    const auto initial_colors = psi.get_colors(project_id, cc_id);
    REQUIRE(!initial_colors.empty());

    const std::string custom_hex = "#ABCDEF";
    psi.set_color_from_user(project_id, cc_id, 0, custom_hex);
    psi.reset_color_to_auto(project_id, cc_id, 0);

    const auto colors = psi.get_colors(project_id, cc_id);
    REQUIRE(!colors.empty());
    REQUIRE(colors[0] == initial_colors[0]);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Automatic color is the filament color of the material preset, then the palette",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();
    auto& preset_interactor = project_interactor.preset_interactor();

    // The test material has no color, so the palette is used.
    const auto filament_colour = [&]()
    {
        return std::get<Domain::FilamentSettings>(
                   preset_interactor.selected_printer_preset().materials.at(0).values
        )
            .items.opt("filament_colour")
            .get<std::string>();
    };
    REQUIRE(filament_colour().empty());
    CHECK(stored_colors() == std::vector<std::string>{Biz::ProjectSettingsInteractor::palette_color(0)});

    preset_interactor.set_preset_value(
        Domain::FDMConfigLocation::Filament,
        0,
        "filament_colour",
        [](Domain::ConfigItem& item) { item.set(std::string{"#123456"}); }
    );
    REQUIRE(filament_colour() == "#123456");

    // A stored color stays until it is reset.
    CHECK(stored_colors() == std::vector<std::string>{Biz::ProjectSettingsInteractor::palette_color(0)});
    psi.set_color_from_user(project_id, cc_id, 0, "");
    CHECK(stored_colors() == std::vector<std::string>{"#123456"});
    CHECK(psi.get_colors(project_id, cc_id) == std::vector<Domain::ColorRGB>{decoded("#123456")});
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "set_colors_from_connect updates the slots it has colors for",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    const std::vector<std::string> before = stored_colors();
    REQUIRE(before.size() == 1);

    // An empty color keeps the slot.
    psi.set_colors_from_connect(project_id, cc_id, {""});
    CHECK(stored_colors() == before);

    // Colors beyond the slot count are ignored.
    psi.set_colors_from_connect(project_id, cc_id, {"#010203", "#040506"});
    CHECK(stored_colors() == std::vector<std::string>{"#010203"});
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Colors of an FFF bed are kept while it is SLA",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    psi.set_color_from_user(project_id, cc_id, 0, "#ABCDEF");

    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    REQUIRE(
        project_interactor.selected_config_container().print_technology()
        == Domain::PrinterTechnology::SLA
    );
    CHECK(stored_colors() == std::vector<std::string>{"#ABCDEF"});

    // The FFF colors can't be changed on an SLA bed.
    psi.set_color_from_user(project_id, cc_id, 0, "#111111");
    psi.set_colors_from_connect(project_id, cc_id, {"#222222"});
    CHECK(stored_colors() == std::vector<std::string>{"#ABCDEF"});

    select_printer_with_technology(Domain::PrinterTechnology::FFF);
    REQUIRE(
        project_interactor.selected_config_container().print_technology()
        == Domain::PrinterTechnology::FFF
    );
    CHECK(stored_colors() == std::vector<std::string>{"#ABCDEF"});
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Resin color of an SLA bed",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    REQUIRE(
        project_interactor.selected_config_container().print_technology()
        == Domain::PrinterTechnology::SLA
    );

    // The automatic color is the one of the material preset.
    const std::string material_color = sla_material_color();
    REQUIRE(!material_color.empty());
    CHECK(stored_resin_color() == material_color);
    CHECK(psi.get_colors(project_id, cc_id) == std::vector<Domain::ColorRGB>{decoded(material_color)});

    psi.set_color_from_user(project_id, cc_id, 0, "#ABCDEF");
    CHECK(stored_resin_color() == "#ABCDEF");
    CHECK(psi.get_colors(project_id, cc_id) == std::vector<Domain::ColorRGB>{decoded("#ABCDEF")});

    // There is one slot only.
    psi.set_color_from_user(project_id, cc_id, 1, "#111111");
    CHECK(stored_resin_color() == "#ABCDEF");

    psi.set_colors_from_connect(project_id, cc_id, {"#010203", "#040506"});
    CHECK(stored_resin_color() == "#010203");

    psi.set_color_from_user(project_id, cc_id, 0, "");
    CHECK(stored_resin_color() == material_color);
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "FFF and SLA colors are kept when a bed switches technology",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    psi.set_color_from_user(project_id, cc_id, 0, "#ABCDEF");

    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    psi.set_color_from_user(project_id, cc_id, 0, "#123456");

    select_printer_with_technology(Domain::PrinterTechnology::FFF);
    CHECK(stored_colors() == std::vector<std::string>{"#ABCDEF"});
    CHECK(psi.get_colors(project_id, cc_id) == std::vector<Domain::ColorRGB>{decoded("#ABCDEF")});

    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    CHECK(stored_resin_color() == "#123456");
    CHECK(psi.get_colors(project_id, cc_id) == std::vector<Domain::ColorRGB>{decoded("#123456")});
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "A copied project keeps the colors of both technologies",
    "[ProjectSettingsInteractor]"
)
{
    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    psi.set_color_from_user(project_id, cc_id, 0, "#ABCDEF");
    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    psi.set_color_from_user(project_id, cc_id, 0, "#123456");

    const Domain::Project copy{project_interactor.selected_project()};
    REQUIRE(copy.config_containers().size() == 1);
    const Domain::ConfigContainer& cc = *copy.config_containers().front();
    CHECK(
        cc.project_settings().items.opt("extruder_colour").get<std::vector<std::string>>()
        == std::vector<std::string>{"#ABCDEF"}
    );
    CHECK(cc.sla_project_settings().items.opt("resin_colour").get<std::string>() == "#123456");
}

TEST_CASE_METHOD(
    ProjectSettingsInteractorFixture,
    "Resin color survives a 3MF round trip",
    "[ProjectSettingsInteractor]"
)
{
    namespace fs = boost::filesystem;

    const Domain::SelectionId project_id = project_interactor.new_project();
    const Domain::SelectionId cc_id = project_interactor.selected_config_container_id();
    auto& psi = project_interactor.project_settings_interactor();

    select_printer_with_technology(Domain::PrinterTechnology::SLA);
    psi.set_color_from_user(project_id, cc_id, 0, "#123456");

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-resin-color-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "resin_color.3mf";
    store_3mf(file_path.string(), project_interactor.selected_project());

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.config_containers_data.size() == 1);
    const auto* config_pack =
        std::get_if<Domain::ConfigPackSLA>(&loaded.config_containers_data.front().config_pack);
    REQUIRE(config_pack != nullptr);
    CHECK(
        config_pack->sla_project_settings.items.opt("resin_colour").get<std::string>() == "#123456"
    );
}

} // namespace Slic3r
