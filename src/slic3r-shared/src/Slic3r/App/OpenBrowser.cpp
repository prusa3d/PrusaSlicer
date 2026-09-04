#include "Slic3r/App/OpenBrowser.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Localization.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <map>

namespace Slic3r::App {

// Maps UI language to supported website locale codes.
static std::string current_language_code_safe()
{
    // Translate the language code to a code, for which Prusa Research maintains translations.
    const std::map<std::string, std::string> mapping{
        {
            "cs",
            "cs_CZ",
        },
        {
            "sk",
            "cs_CZ",
        },
        {
            "de",
            "de_DE",
        },
        {
            "es",
            "es_ES",
        },
        {
            "fr",
            "fr_FR",
        },
        {
            "it",
            "it_IT",
        },
        {
            "ja",
            "ja_JP",
        },
        {
            "ko",
            "ko_KR",
        },
        {
            "pl",
            "pl_PL",
        },
        //{ "uk", 	"uk_UA", },
        //{ "zh", 	"zh_CN", },
        //{ "ru", 	"ru_RU", },
    };

    std::string language_code = localization().active_language();
    auto it                   = mapping.find(language_code);
    if (it != mapping.end())
        language_code = it->second;
    else
        language_code = "en_US";
    return language_code;
}

void open_browser(OpenBrowserParams params)
{
    if (params.is_localized_url) {
        params.url += "&lng=" + current_language_code_safe();
    }

    enum class SuppressHyperLinksOption
    {
        ShowWarning,
        AlwaysSuppress,
        AlwaysAllow
    };

    AppConfig& app_config          = AppServices::instance().app_config();
    IDialogManager& dialog_manager = AppServices::instance().dialog_manager();
    bool show_warning              = app_config.get<bool>("show_open_browser_warning_dialog");
    bool checked                   = app_config.get<bool>("suppress_hyperlinks");
    SuppressHyperLinksOption opt_val =
        (show_warning ? SuppressHyperLinksOption::ShowWarning :
                        (checked ? SuppressHyperLinksOption::AlwaysSuppress :
                                   SuppressHyperLinksOption::AlwaysAllow));
    bool launch = true;
    if (opt_val == SuppressHyperLinksOption::ShowWarning) {
        // no previous action from user
        // open dialog with remember checkbox
        dialog_manager.show_rich_yesno_dialog(
            Biz::_u8L("PrusaSlicer: Open hyperlink"),
            Biz::_u8L("Open hyperlink in default browser?"),
            Biz::_u8L("Remember my choice"),
            [&launch](bool answer) { launch = answer; },
            [&launch](bool checked)
            {
                if (checked) {
                    // ysFIXME: use AppConfigInteractor , when it will be merged
                    AppServices::instance().app_config_interactor().set_item_value(
                        "show_open_browser_warning_dialog",
                        Domain::ConfigValue(false)
                    );
                    AppServices::instance().app_config_interactor().set_item_value(
                        "suppress_hyperlinks",
                        Domain::ConfigValue(!launch)
                    );
                }
            }
        );
    } else if (opt_val == SuppressHyperLinksOption::AlwaysAllow) {
        // user already set checkbox to always open
        launch = true;
    } else if (opt_val == SuppressHyperLinksOption::AlwaysSuppress && params.force_remember_choice)
    {
        // user already set checkbox or preferences to always supress
        launch = false;
    } else if (opt_val == SuppressHyperLinksOption::AlwaysSuppress && !params.force_remember_choice)
    {
        // user already set checkbox or preferences to always supress but it is overriden
        // no checkbox in dialog
        dialog_manager.show_yesno_dialog(
            Biz::_u8L("PrusaSlicer: Open hyperlink"),
            Biz::_u8L("Open hyperlink in default browser?"),
            [&](bool answer) { launch = answer; }
        );
    }

    if (launch) {
        dialog_manager.open_in_browser(params.url, 0);
    }
}

} // namespace Slic3r::App
