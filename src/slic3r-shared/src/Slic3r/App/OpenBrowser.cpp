#include "Slic3r/App/OpenBrowser.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Localization.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <map>
#include <memory>
#include <optional>

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

static void append_query_param(std::string& url, const std::string& key_value)
{
    size_t insert_pos = url.find('#');
    if (insert_pos == std::string::npos) {
        insert_pos = url.size();
    }
    const bool has_query = url.find('?') < insert_pos;
    url.insert(insert_pos, (has_query ? "&" : "?") + key_value);
}

void open_browser(OpenBrowserParams params)
{
    if (params.is_localized_url) {
        append_query_param(params.url, "lng=" + current_language_code_safe());
    }

    AppConfig& app_config          = AppServices::instance().app_config();
    IDialogManager& dialog_manager = AppServices::instance().dialog_manager();
    HyperlinkPolicy policy         = params.skip_confirmation ?
        HyperlinkPolicy::AlwaysOpen :
        app_config.get<HyperlinkPolicy>("open_hyperlink_policy");

    if (policy == HyperlinkPolicy::NeverOpen) {
        return;
    }

    if (policy == HyperlinkPolicy::Ask) {
        struct AskState
        {
            std::optional<bool> answer;
            bool remember = false;
        };
        auto state = std::make_shared<AskState>();
        auto persist_policy = [](bool launch)
        {
            AppConfig& app_config     = AppServices::instance().app_config();
            Domain::EnumWrapper value = app_config.get<Domain::EnumWrapper>("open_hyperlink_policy");
            value.set(launch ? HyperlinkPolicy::AlwaysOpen : HyperlinkPolicy::NeverOpen);
            AppServices::instance().app_config_interactor().set_item_value(
                "open_hyperlink_policy",
                Domain::ConfigValue(value)
            );
        };
        dialog_manager.show_rich_yesno_dialog(
            // TRN Title of the dialog asking whether a clicked hyperlink should be opened.
            Biz::_u8L("Open hyperlink"),
            fmt::format(
                // TRN Followed by the URL about to be opened.
                "{}\n\n{}", Biz::_u8L("Do you want to open this link in your web browser?"), params.url
            ),
            // TRN Checkbox of the open hyperlink dialog; stores the answer into the "Open hyperlinks in web browser" preference.
            Biz::_u8L("Remember my choice"),
            [state, persist_policy, url = params.url](bool answer)
            {
                state->answer = answer;
                if (state->remember) {
                    persist_policy(answer);
                }
                if (answer) {
                    AppServices::instance().dialog_manager().open_in_browser(url, 0);
                }
            },
            [state, persist_policy](bool checked)
            {
                if (!checked) {
                    return;
                }
                state->remember = true;
                if (state->answer.has_value()) {
                    persist_policy(*state->answer);
                }
            }
        );
        return;
    }

    dialog_manager.open_in_browser(params.url, 0);
}

bool hyperlinks_allowed()
{
    return AppServices::instance().app_config().get<HyperlinkPolicy>("open_hyperlink_policy") !=
        HyperlinkPolicy::NeverOpen;
}

} // namespace Slic3r::App
