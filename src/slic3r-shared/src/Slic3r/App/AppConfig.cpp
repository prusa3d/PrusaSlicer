#include "Slic3r/App/AppConfig.hpp"

#include "Slic3r/Semver.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"

#include "Slic3r/App/Theme.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Version.hpp"

#include "nlohmann/json.hpp"
#include "boost/filesystem.hpp"
#include "boost/system/error_code.hpp"
#include "boost/nowide/fstream.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App {

    void appconfig_config_init_fn(Domain::ConfigDefinitions& defs);

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

    def = defs.add("reverse_mouse_wheel_zoom", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Reverse direction of zoom with mouse wheel");
    def->tooltip = L("If enabled, reverses the direction of zoom with mouse wheel");
    def->category = Domain::ConfigItemDef::Category::AppConfig_General;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;
    def->init_fn = []() { return Domain::ConfigValue(false); };

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
    def = defs.add("enable_prusa_account", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Enable Prusa Account");
    def->tooltip = L("Enable logging to Prusa Account, Connect tab and uploading files to Connect.");
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup;
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def = defs.add("enable_printables", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->gui_type = GUIType::checkbox;
    def->label = L("Enable Printables");
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

    // Settings for open link in browser

    def = defs.add("show_open_browser_warning_dialog", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_General;
    def->gui_type = GUIType::checkbox;
    def->label = L("Show warning dialog before opening a link in default browser");
    def->init_fn = []() { return Domain::ConfigValue(true); };

    def = defs.add("suppress_hyperlinks", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::AppConfig_Services;
    def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_Services_General;
    def->gui_type = GUIType::checkbox;
    def->label = L("Suppress opening hyperlinks in browser");
    def->init_fn = []() { return Domain::ConfigValue(false); };

    def           = defs.add("favorite_params", typeid(std::vector<std::string>));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []()
    {
        return Domain::ConfigValue(
            std::vector<std::string>{
                "perimeters",
                "fill_pattern",
                "fill_density",
                "brim_type",
                "support_material",
                "support_material_style"
            }
        );
    };

    def           = defs.add("printers_only_favorites", typeid(bool));
    def->location = Domain::AppConfigLocation{};
    def->category = Domain::ConfigItemDef::Category::Hidden;
    def->init_fn  = []() { return Domain::ConfigValue(false); };

    def           = defs.add("materials_only_favorites", typeid(bool));
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
    const boost::optional<Semver> semver{Semver::parse(app_config.get<std::string>("version"))};
    if (!semver.has_value()) {
        return;
    }

    const Semver version300_alpha9{3, 0, 0, nullptr, "alpha9"};
    if (semver <= version300_alpha9) {
        // reset font size to new default value
        const Domain::ConfigItem* font_size_item =
            app_config.get_config_box().items.find("font_size");
        ASSERT(font_size_item);
        app_config.set("font_size", font_size_item->def().init_fn());
    }
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

bool AppConfig::is_prusa_account_enabled() const
{
    if (!is_webkit_available()) {
        return false;
    }

    return get<bool>("enable_prusa_account");
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
