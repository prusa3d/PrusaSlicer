#include "Slic3r/App/AppConfig.hpp"

#include "Slic3r/Semver.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/Network/ServerTrustStore.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Version.hpp"

#include "nlohmann/json.hpp"
#include "boost/filesystem.hpp"
#include "boost/system/error_code.hpp"
#include "boost/nowide/fstream.hpp"

#include <algorithm>

using namespace Slic3r::Biz;

namespace Slic3r::App {

    void appconfig_config_init_fn(Domain::ConfigDefinitions& defs);

// The default favorites of SLA printers. Configs saved before they were added get them once, see
// AppConfig::handle_legacy_config().
static const std::vector<std::string> sla_favorite_params{
    "supports_enable",
    "support_tree_type",
    "support_points_density_relative",
    "pad_enable",
    "hollowing_enable"
};

AppSettings::AppSettings(const Domain::ConfigDefinitions& defs_appconfig) : ConfigBox(defs_appconfig, Domain::AppConfigLocation{}) {}

void appconfig_config_init_fn(Domain::ConfigDefinitions& defs)
{
    Domain::ConfigItemDef* def = nullptr;

    using Category = Domain::ConfigItemDef::Category;
    using GUIType = Domain::ConfigItemDef::GUIType;

    def = defs.add("show_splash_screen", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Show splashscreen");
    def->tooltip = L("Show splashscreen during application start.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("restore_win_position", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Restore window position on start");
    def->tooltip = L("Restore window position on start.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("crash_reason", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    def = defs.add("mainframe_window_metrics", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    def               = defs.add("theme", typeid(Domain::EnumWrapper));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Theme");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = GUIType::combobox;
    def->tooltip      = L("Application theme");
    def->init_fn      = Domain::init_with(
        Theme::Style::Dark,
        {
            {int(Theme::Style::Dark), "dark", def->L_CONTEXT("Dark", "Theme")},
            {int(Theme::Style::Light), "light", def->L_CONTEXT("Light", "Theme")},
        }
    );

    def               = defs.add("mouse_navigation_scheme", typeid(Domain::EnumWrapper));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Mouse navigation");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = GUIType::combobox;
    def->tooltip      = L("Controls how mouse buttons orbit, pan and select in the 3D scene");
    def->init_fn      = Domain::init_with(
        MouseNavigationScheme::PrusaSlicer,
        {
            {int(MouseNavigationScheme::PrusaSlicer),
             "prusaslicer",
             def->L_CONTEXT("PrusaSlicer", "Mouse navigation scheme")},
            {int(MouseNavigationScheme::Tinkercad),
             "tinkercad",
             def->L_CONTEXT("Tinkercad", "Mouse navigation scheme")},
            {int(MouseNavigationScheme::Blender),
             "blender",
             def->L_CONTEXT("Blender", "Mouse navigation scheme")},
            {int(MouseNavigationScheme::SolidWorks),
             "solidworks",
             def->L_CONTEXT("SolidWorks", "Mouse navigation scheme")},
            {int(MouseNavigationScheme::Fusion),
             "fusion",
             def->L_CONTEXT("Fusion", "Mouse navigation scheme")},
        }
    );

    def               = defs.add("transparent_window_alpha", typeid(double));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Sidebars transparency");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = Domain::ConfigItemDef::GUIType::f_enum_open;
    def->tooltip      = L(
        "Sidebar windows transparency. Select from the predefined values or input the needed value.\n"
             "Allowed alpha range is [0.5; 1.0]."
    );
    def->min = 0.5;
    def->max = 1.;
    def->choices =
        {{1., L("Solid")}, {0.85, L("Subtle")}, {0.7, L("Balanced")}, {0.55, L("Glass")}};
    def->init_fn = Domain::init_with(1.);

    def               = defs.add("camera_projection_type", typeid(Domain::EnumWrapper));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Camera projection");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = GUIType::combobox;
    def->tooltip      = L("Default camera projection type");
    def->init_fn      = Domain::init_with(
        Scene::CameraProjectionType::Perspective,
        {
            {int(Scene::CameraProjectionType::Perspective), "perspective", def->L_CONTEXT("Perspective", "Camera projection")},
            {int(Scene::CameraProjectionType::Orthographic), "orthographic", def->L_CONTEXT("Orthographic", "Camera projection")},
        }
    );

    def = defs.add("reverse_mouse_wheel_zoom", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Reverse direction of zoom with mouse wheel");
    def->tooltip = L("If enabled, reverses the direction of zoom with mouse wheel");
    def->category = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def           = defs.add("scroll_wheel_changes_value", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label    = L("Scroll wheel changes value");
    def->tooltip  = L(
        "If enabled, the mouse scroll wheel changes the value of the input field or combo box "
        "under the cursor."
    );
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn      = []() { return Domain::ConfigValue(true); };

    def           = defs.add("use_free_camera", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label    = L("Use free camera");
    def->tooltip  = L(
        "If enabled, the mouse rotates the camera freely in all directions (trackball). "
         "If not enabled, the camera keeps the Z axis pointing up (turntable)."
    );
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_translation_speed", typeid(double));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::textfield;
    def->label        = L("Translation");
    def->tooltip      = L("Speed of moving the view with the 3D mouse.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Speed;
    def->order        = 0;
    def->min          = 0.1;
    def->max          = 30.;
    def->init_fn      = []() { return Domain::ConfigValue(1.); };

    def               = defs.add("mouse_3d_rotation_speed", typeid(double));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::textfield;
    def->label        = L("Rotation");
    def->tooltip      = L("Speed of rotating the view with the 3D mouse.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Speed;
    def->order        = 1;
    def->min          = 0.1;
    def->max          = 30.;
    def->init_fn      = []() { return Domain::ConfigValue(1.); };

    def               = defs.add("mouse_3d_zoom_speed", typeid(double));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::textfield;
    def->label        = L("Zoom");
    def->tooltip      = L("Speed of zooming the view with the 3D mouse.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Speed;
    def->order        = 2;
    def->min          = 0.1;
    def->max          = 30.;
    def->init_fn      = []() { return Domain::ConfigValue(1.); };

    def           = defs.add("mouse_3d_translation_deadzone", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::textfield;
    def->label    = L("Translation/Zoom");
    def->tooltip =
        L("Motions of the 3D mouse moving or zooming the view below this value are ignored.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Deadzone;
    def->order        = 0;
    def->min          = 0.;
    def->max          = 0.2;
    def->init_fn      = []() { return Domain::ConfigValue(0.); };

    def           = defs.add("mouse_3d_rotation_deadzone", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::textfield;
    def->label    = L("Rotation");
    def->tooltip  = L("Motions of the 3D mouse rotating the view below this value are ignored.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Deadzone;
    def->order        = 1;
    def->min          = 0.;
    def->max          = 0.2;
    def->init_fn      = []() { return Domain::ConfigValue(0.); };

    def           = defs.add("mouse_3d_swap_yz", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label    = L("Swap Y/Z axes");
    def->tooltip =
        L("Swaps the Y and Z axes of the 3D mouse for both moving and rotating the view.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 0;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_x", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert X axis");
    def->tooltip      = L("Inverts pushing the cap of the 3D mouse left and right.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 1;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_y", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert Y axis");
    def->tooltip      = L("Inverts pushing the cap of the 3D mouse forward and backward.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 2;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_z", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert Z axis");
    def->tooltip      = L("Inverts pulling the cap of the 3D mouse up and pushing it down.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 3;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_yaw", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert yaw axis");
    def->tooltip      = L("Inverts twisting the cap of the 3D mouse.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 4;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_pitch", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert pitch axis");
    def->tooltip      = L("Inverts tilting the cap of the 3D mouse forward and backward.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 5;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("mouse_3d_invert_roll", typeid(bool));
    def->location     = Domain::AppConfigLocation{};
    def->gui_type     = GUIType::checkbox;
    def->label        = L("Invert roll axis");
    def->tooltip      = L("Inverts tilting the cap of the 3D mouse left and right.");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Mouse3D_Options;
    def->order        = 6;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def               = defs.add("graphics_quality", typeid(Domain::EnumWrapper));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Graphics quality");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = GUIType::combobox;
    def->tooltip      = L("Controls rendering quality and performance of the 3D scene");
    def->init_fn      = Domain::init_with(
        App::GraphicsQuality::High,
        {
            {int(App::GraphicsQuality::Legacy),
             "legacy",
             def->L_CONTEXT("Legacy", "Graphics quality")},
            {int(App::GraphicsQuality::Low), "low", def->L_CONTEXT("Low", "Graphics quality")},
            {int(App::GraphicsQuality::Medium),
             "medium",
             def->L_CONTEXT("Medium", "Graphics quality")},
            {int(App::GraphicsQuality::High), "high", def->L_CONTEXT("High", "Graphics quality")},
        }
    );

    def               = defs.add("translation_language", typeid(std::string));
    def->location     = Domain::AppConfigLocation{};
    def->label        = L("Language");
    def->category     = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->gui_type     = GUIType::language_selection;
    def->tooltip =
        L("Selection of the application language.\n"
          "Note: Language selection will be applied on the next application start.");
    def->init_fn = Domain::init_with("en");

    def = defs.add("font_size", typeid(int));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::spinbox;
    def->label = L("Application font size");
    def->tooltip = L("Base font size of text in points.");
    def->units = {"pt"};
    def->category = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->min = 8;
    def->max = 18;
    def->init_fn = []() { return Domain::ConfigValue{11}; };

#ifdef SLIC3R_HAS_WEBKIT
    def           = defs.add("enable_prusa_account", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label    = L("Prusa Account and Prusa Connect");
    def->tooltip  = L(
        "Enables signing in to Prusa Account, the Connect tab with Prusa Connect and uploading files to it."
    );
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn      = []() { return Domain::ConfigValue(false); };

    def           = defs.add("enable_connect_local", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label    = L("Prusa Connect (local)");
    def->tooltip  = L(
        "Enables signing into your own local Prusa Connect server, the Connect tab with its printers and uploading files to it. The server is set up in the Connect tab."
    );
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn      = []() { return Domain::ConfigValue(true); };

    def           = defs.add("account_service", typeid(Domain::EnumWrapper));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = Domain::init_with(
        AccountService::PrusaAccount,
        {
            {int(AccountService::None), "none", "none"},
            {int(AccountService::PrusaAccount), "prusa_account", "prusa_account"},
            {int(AccountService::ConnectLocal), "connect_local", "connect_local"},
        }
    );

    def           = defs.add("account_services_split", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("account_service_migrated", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("confirm_account_service_switch", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(true); };

    def           = defs.add("connect_mode", typeid(Domain::EnumWrapper));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = Domain::init_with(
        Network::ServiceMode::Cloud,
        {
            {int(Network::ServiceMode::Cloud), "online", "online"},
            {int(Network::ServiceMode::ConnectLocal), "offline", "offline"},
        }
    );

    def           = defs.add("connect_local_url", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(std::string()); };

    def           = defs.add("connect_local_trust", typeid(Domain::EnumWrapper));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn  = Domain::init_with(
        Network::ServerTrustKind::System,
        {
            {int(Network::ServerTrustKind::System),
             "system",
             def->L_CONTEXT("System trust store", "Connect Local certificate")},
            {int(Network::ServerTrustKind::PinnedKey),
             "pinned",
             def->L_CONTEXT(
                 "Trusted server certificate (pinned key)",
                 "Connect Local certificate"
             )},
            {int(Network::ServerTrustKind::Certificate),
             "certificate",
             def->L_CONTEXT("Loaded certificate", "Connect Local certificate")},
        }
    );

    def           = defs.add("connect_local_trust_url", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(std::string()); };

    def           = defs.add("connect_local_trust_pin", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(std::string()); };

    def = defs.add("enable_printables", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Printables");
    def->tooltip = L("Enable Printables tab.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn = []() { return Domain::ConfigValue(false); };
#endif

    def = defs.add("enable_preset_update", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Update presets over the internet");
    def->tooltip = L("Let the preset updater contact online sources. When off, presets are still installed from the files that come with the application and from local sources.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_General;
    def->init_fn = []() { return Domain::ConfigValue(false); }; // off by default, so it does not fire before wizard on the first run!

    def = defs.add("layout_main_left_column_width", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(300.); };

    def = defs.add("layout_main_right_column_width", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(280.); };

    def = defs.add("last_used_directory", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    def = defs.add("last_used_extension", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    def = defs.add("last_used_physical_printer", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    def = defs.add("last_used_export_destination", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(std::string()); };

    // TODO: This option needs check after changed field in Preferences.
    def = defs.add("downloads_directory", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::textfield;
    def->label = L("Downloads directory");
    def->tooltip = L("Sets directory for downloads such as objects from Printables service or app updates. Must be valid path.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->full_width = true;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn = []() { return Domain::ConfigValue(system_downloads_dir().string()); };

    // Settings for LayersDoubleSlider
    def = defs.add("show_estimated_times_in_dbl_slider", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("show_ruler_in_dbl_slider", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_ruler_bg_in_dbl_slider", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("seq_top_layer_only", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    // Preview options visibility (Slic3r::App::libvgcode::Settings::options_visibility)
    def = defs.add("show_travels_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_wipes_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_retractions_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_unretractions_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_seams_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_tool_changes_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_color_changes_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_pause_prints_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_custom_gcodes_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_center_of_gravity_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_tool_marker_in_preview", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("use_default_colors_in_dbl_slider", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("show_step_import_parameters", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_General;
    def->category = Category::AppConfig_General;
    def->gui_type = GUIType::checkbox;
    def->label = L("Show STEP import parameters dialog");
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("step_linear_precision", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(0.005); };

    def = defs.add("step_angle_precision", typeid(double));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn = []() { return Domain::ConfigValue(1.); };

    def               = defs.add("open_hyperlink_policy", typeid(Domain::EnumWrapper));
    def->location     = Domain::AppConfigLocation{};
    def->category     = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_General;
    def->gui_type     = GUIType::combobox;
    // TRN Label of a Preferences combobox choosing what happens when a hyperlink is clicked.
    def->label        = L("Open hyperlinks in web browser");
    // TRN Tooltip of the "Open hyperlinks in web browser" Preferences combobox.
    def->tooltip      = L("Choose what happens when you click a link in PrusaSlicer: "
                          "ask for confirmation, open it in your default web browser right away, or do nothing.");
    def->init_fn      = Domain::init_with(
        HyperlinkPolicy::Ask,
        {
            // TRN Value of the "Open hyperlinks in web browser" Preferences combobox.
            {int(HyperlinkPolicy::Ask), "ask", L("Ask before opening")},
            // TRN Value of the "Open hyperlinks in web browser" Preferences combobox.
            {int(HyperlinkPolicy::AlwaysOpen), "always", L("Always open")},
            // TRN Value of the "Open hyperlinks in web browser" Preferences combobox.
            {int(HyperlinkPolicy::NeverOpen), "never", L("Never open")},
        }
    );

    def           = defs.add("favorite_params", typeid(std::vector<std::string>));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []()
    {
        // One list for both technologies, the sidebar shows the ones of the selected printer.
        std::vector<std::string> favorites{
            "perimeters",
            "fill_pattern",
            "fill_density",
            "brim_type",
            "support_material",
            "support_material_style"
        };
        favorites.insert(favorites.end(), sla_favorite_params.begin(), sla_favorite_params.end());
        return Domain::ConfigValue(std::move(favorites));
    };

    // Whether the SLA defaults were added to favorite_params. A config without it was saved
    // before they existed.
    // TEMPORARY: only migrates app configs from 3.0.0-alpha11, so they get the SLA favorites
    // added in 3.0.0-alpha13. Once configs older than 3.0.0-alpha13 are phased out, remove this
    // option together with its handling in AppConfig::handle_legacy_config().
    def           = defs.add("favorite_params_sla_added", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("printers_only_favorites", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("materials_only_favorites", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("matdb_materials_only_favorites", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("auto_reslice", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(true); };

    def           = defs.add("version", typeid(std::string));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(Slic3r::VERSION); };

#ifdef SLIC3R_SENTRY
    def           = defs.add("sentry", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Enable crash reporting");
    def->tooltip = L("Automatically send crash reports to Prusa3D when the application crashes");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn  = []() { return Domain::ConfigValue(false); };
#endif

    def           = defs.add("initialized", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue{false}; };

    def           = defs.add("hide_sidebars", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue{false}; };

    def = defs.add("last_material_selection_dialog_tab", typeid(int));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue{0}; };

    def = defs.add("last_printer_settings_dialog_filter", typeid(int));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue{0}; };
}

tl::expected<std::unique_ptr<AppConfig>, std::string> AppConfig::load_appconfig(const std::string& filename)
{
    namespace fs = boost::filesystem;
    auto app_config = std::make_unique<AppConfig>();
    try {
        if (fs::exists(fs::path(filename))) {
            boost::nowide::ifstream file_stream(filename);
            auto json_str = std::string(
                (std::istreambuf_iterator<char>(file_stream)),
                std::istreambuf_iterator<char>()
            );
            nlohmann::ordered_json json = nlohmann::json::parse(json_str);
            std::string loc =
                Domain::get_location_name(Domain::ConfigLocation(Domain::AppConfigLocation()));
            if (json.contains(loc)) {
                auto load_issues = Biz::Config::load_box(json[loc], app_config->m_app_settings);
            }
            app_config->m_app_settings_advanced =
                json["app_settings_advanced"].get<AppSettingsAdvanced>();
        }
    } catch (...) {
        return tl::unexpected(L("Unable to read application settings."));
    }

    AppConfig::handle_legacy_config(*app_config.get());

    return std::move(app_config);
}

void AppConfig::handle_legacy_config(AppConfig& app_config)
{
    // A stored favorite_params replaces the default, so configs saved before the SLA favorites
    // existed would never show them. Add them once; when the user unstars them later, they stay
    // removed. A new config has them already, so nothing is added there.
    if (!app_config.get<bool>("favorite_params_sla_added")) {
        auto favorites{app_config.get<std::vector<std::string>>("favorite_params")};
        for (const std::string& key : sla_favorite_params) {
            if (std::ranges::find(favorites, key) == favorites.end()) {
                favorites.push_back(key);
            }
        }
        app_config.set("favorite_params", favorites);
        app_config.set("favorite_params_sla_added", true);
    }

#ifdef SLIC3R_HAS_WEBKIT
    if (!app_config.get<bool>("account_services_split")) {
        if (app_config.get<bool>("account_service_migrated")) {
            const AccountService chosen = app_config.get<AccountService>("account_service");
            app_config.set("enable_prusa_account", chosen == AccountService::PrusaAccount);
        } else if (app_config.get<bool>("enable_prusa_account")
            && app_config.get<Network::ServiceMode>("connect_mode") == Network::ServiceMode::ConnectLocal)
        {
            app_config.set("account_service", AccountService::ConnectLocal);
        }
        app_config.set("enable_connect_local", true);
        if (app_config.get<AccountService>("account_service") == AccountService::None) {
            app_config.set("account_service", AccountService::PrusaAccount);
        }
        app_config.set("account_services_split", true);
    }
#endif

    const boost::optional<Semver> semver{Semver::parse(app_config.get<std::string>("version"))};
    if (semver.has_value() && *semver < Semver("3.0.0-alpha13")) {
        app_config.set("initialized", false);
    }
    app_config.set("version", std::string(Slic3r::VERSION));
}

AppConfig::AppConfig() :
    m_defs_appconfig({Domain::AppConfigLocation{}}, appconfig_config_init_fn),
    m_app_settings(m_defs_appconfig)
{}

std::unique_ptr<AppConfig> AppConfig::create_app_config()
{
    std::unique_ptr<AppConfig> app_config;
    const boost::filesystem::path appconfig_filename = boost::filesystem::path(Slic3r::data_dir()) / "shared_runtime" / "PrusaSlicer.json";
    if (auto apc = AppConfig::load_appconfig(appconfig_filename.string()); apc)
         app_config = std::move(apc.value());
    else {
        SPDLOG_ERROR("Failed to parse app config. Creating a new one from default.");
        app_config = std::make_unique<AppConfig>();
        AppConfig::handle_legacy_config(*app_config);
    }
    app_config->set_filename(appconfig_filename.string());
    return app_config;
}

bool AppConfig::save() const
{
    if (! m_filename.empty()) {
        try {
            nlohmann::ordered_json json;
            json[Domain::get_location_name(m_app_settings.location)] = m_app_settings;
            json["app_settings_advanced"] = m_app_settings_advanced;
            boost::nowide::ofstream out(m_filename);
            out << json.dump(2);
        } catch (...) {
            return false;
        }
    }
    return true;
}

const AppSettingsAdvanced& AppConfig::app_settings_advanced() const
{
    return m_app_settings_advanced;
}

AppSettingsAdvanced& AppConfig::app_settings_advanced()
{
    return m_app_settings_advanced;
}

bool AppConfig::is_webkit_available() const {
#ifdef SLIC3R_HAS_WEBKIT
    return true;
#else
    return false;
#endif
}

bool AppConfig::is_printables_enabled() const
{
    if (!is_webkit_available()) {
        return false;
    }
    return get<bool>("enable_printables");
}

bool AppConfig::is_account_service_enabled() const
{
    return is_webkit_available() && account_service() != AccountService::None;
}

AccountService AppConfig::account_service() const
{
#ifdef SLIC3R_HAS_WEBKIT
    const bool prusa_account = get<bool>("enable_prusa_account");
    const bool connect_local    = get<bool>("enable_connect_local");
    if (!prusa_account && !connect_local) {
        return AccountService::None;
    }
    if (!prusa_account) {
        return AccountService::ConnectLocal;
    }
    if (!connect_local) {
        return AccountService::PrusaAccount;
    }
    return get<AccountService>("account_service") == AccountService::ConnectLocal ?
        AccountService::ConnectLocal :
        AccountService::PrusaAccount;
#else
    return AccountService::None;
#endif
}

bool AppConfig::are_both_account_services_enabled() const
{
#ifdef SLIC3R_HAS_WEBKIT
    return get<bool>("enable_prusa_account") && get<bool>("enable_connect_local");
#else
    return false;
#endif
}

bool AppConfig::is_account_usable() const
{
    if (!is_account_service_enabled()) {
        return false;
    }
    return service_mode() == Network::ServiceMode::Cloud || connect_local_server().has_value();
}

std::optional<std::string> AppConfig::connect_local_server() const
{
    return Network::ServiceConfig::normalize_connect_local_server_url(connect_local_url());
}

Network::ServiceMode AppConfig::service_mode() const
{
    return account_service() == AccountService::ConnectLocal ? Network::ServiceMode::ConnectLocal :
                                                            Network::ServiceMode::Cloud;
}

std::string AppConfig::connect_local_url() const
{
#ifdef SLIC3R_HAS_WEBKIT
    return get<std::string>("connect_local_url");
#else
    return {};
#endif
}

void AppConfig::apply_service_mode() const
{
    Network::ServiceConfig::instance().set_mode(service_mode(), connect_local_url());
    AppServices::instance().server_trust_store().apply(connect_local_trust());
}

Network::ServerTrust AppConfig::connect_local_trust() const
{
    Network::ServerTrust trust;
#ifdef SLIC3R_HAS_WEBKIT
    trust.kind       = get<Network::ServerTrustKind>("connect_local_trust");
    trust.server_url = get<std::string>("connect_local_trust_url");
    trust.pin        = get<std::string>("connect_local_trust_pin");
    if (trust.kind == Network::ServerTrustKind::Certificate) {
        trust.pem_path = connect_local_trust_pem_path();
    }
    const bool complete = !trust.server_url.empty()
        && (trust.kind != Network::ServerTrustKind::PinnedKey || trust.pin.starts_with("sha256//"));
    if (!complete) {
        trust = Network::ServerTrust{};
    }
#endif
    return trust;
}

std::string AppConfig::connect_local_trust_pem_path()
{
    return (boost::filesystem::path(data_dir()) / "connect_local_certificate.pem").string();
}

bool AppConfig::is_sentry_enabled() const
{
    if (!is_sentry_available()) {
        return false;
    }
    return get<bool>("sentry");
}

bool AppConfig::is_sentry_available() const
{
#ifdef SLIC3R_SENTRY
    return true;
#else
    return false;
#endif
}

void
AppConfig::record_crash(const std::string& crash_reason, const std::string& disabled_option_key)
{
    set("crash_reason", crash_reason);
    set(disabled_option_key, false);
    save();
}

void AppConfig::resolve_crash(const std::string& resolved_crash_reason, const std::string& restored_option_key)
{
    set("crash_reason", resolved_crash_reason);
    if (!restored_option_key.empty()) {
        set(restored_option_key, true);
    }
    save();
}

} // namespace Slic3r::App
