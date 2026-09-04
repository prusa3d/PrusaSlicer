#include "Slic3r/Biz/I18N/Translation.hpp"
#include "Slic3r/Biz/I18N/LanguageInfo.hpp"

#include "Slic3r/Assert.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <clocale>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/locale/info.hpp>
#include <Slic3r/Log.hpp>
#include <iostream>

#include "Slic3r/Version.hpp"

#ifdef __linux__
#include <cstdlib>
#include <regex>
#endif // __linux__

namespace Slic3r::Biz {

static std::string language(const std::string& locale_str)
{
    std::size_t pos = locale_str.find_first_of("_-@");
    if (pos == std::string::npos)
        return locale_str;

    return locale_str.substr(0, pos);
}

static std::string country(const std::string& locale_str)
{
    std::size_t pos = locale_str.find('_');
    if (pos == std::string::npos)
        return "";

    return locale_str.substr(pos + 1, locale_str.length() - pos);
}

void Translations::init_translations(const boost::filesystem::path& local_dir)
{
    // init locale generator
    m_locale_generator.add_messages_path(local_dir.string());
    m_locale_generator.add_messages_domain(SLIC3R_APP_KEY);

    std::set<std::string> lang_tags = load_available_languages(local_dir);

    std::vector<psLanguageInfo> languages_db;
    GetLanguagesDB(languages_db);

    for (const psLanguageInfo& lang : languages_db) {
        // first check
        if (lang_tags.find(lang.LocaleTag)     != lang_tags.end() || 
            lang_tags.find(lang.CanonicalName) != lang_tags.end())
        {
            if (lang.LayoutDirection == psLayout_RightToLeft) {
                // PrusaSlicer does not support the Right to Left languages yet.
                // Don't add this dictionary into m_translations.
                SPDLOG_TRACE("The following language code requires right to left layout, which is not supported by PrusaSlicer: {}", lang.CanonicalName);
            }
            else
                m_translations.emplace_back( LanguageShortInfo{ lang.CanonicalName, 
                                                                lang.LocaleTag, 
                                                                lang.Description,
                                                                lang.CanonicalRef,
                                                                language(lang.CanonicalName),
                                                                country(lang.CanonicalName) });
        }
    }

    init_system_locale_values();
    init_system_language();
    init_best_language();
}

void Translations::init_system_locale_values()
{
    boost::locale::generator gen;
    // Create an object with default locale
    std::locale base_locale = gen("");

    // Use boost::locale::info to get all parameters
    boost::locale::info const& properties = std::use_facet<boost::locale::info>(base_locale);

    std::string name = properties.name();
    m_sys_locale = name.substr(0, name.find_first_of('.'));
    m_sys_locale_language = properties.language();
}

const std::string Translations::language_description(const std::string& name) const
{
    auto it = std::find_if(
        m_translations.begin(),
        m_translations.end(),
        [name](const Biz::LanguageShortInfo& item) { return item.canonical_name == name; }
    );
    if (it == m_translations.end()) {
        return std::string();
    }

    return it->description;
}

std::set<std::string> Translations::load_available_languages(const boost::filesystem::path& local_dir)
{
    std::set<std::string> lang_tags;

    if (!boost::filesystem::exists(local_dir))
        return lang_tags;

    for (auto& dir_entry : boost::filesystem::directory_iterator(local_dir)) {
        if (dir_entry.is_directory()) {
            std::string lang_tag = dir_entry.path().stem().string();
            lang_tags.emplace(lang_tag);
        }
    }

    return lang_tags;
}

void Translations::init_system_language()
{
    if (!m_sys_locale.empty()) {
        m_language_short_info_system = get_language_short_info(m_sys_locale);
        SPDLOG_TRACE("System language detected (user locales and such): {}",
                     m_language_short_info_system ? m_language_short_info_system->canonical_name : m_sys_locale);
    }
}

#ifdef __linux__
LanguageShortInfo* Translations::linux_get_existing_locale_language(LanguageShortInfo* language,
    const LanguageShortInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = language->language;

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
        {
            auto has_utf8 = [](const std::string& s) {
                auto S = boost::to_upper_copy(s);
                return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
                };
            return !has_utf8(a) && has_utf8(b);
        });

    // Remove the suffix behind a dot, if there is one.
    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(),
        locales.end(),
        [](const std::string& s) {
            return !std::regex_match(s,
            std::regex("^[a-z]+_[A-Z]{2}$"));
        }),
        locales.end());

    if (system_language) {
        // Is there a candidate matching a country code of a system language? Move it to the end,
        // while maintaining the order of matches, so that the best match ends up at the very end.
        std::string system_country = "_" + system_language->country;
        int cnt = locales.size();
        for (int i = 0; i < cnt; ++i)
            if (locales[i].find(system_country) != std::string::npos) {
                locales.emplace_back(std::move(locales[i]));
                locales[i].clear();
            }
    }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++it)
        if (!it->empty()) {
            const std::string& locale = *it;
            LanguageShortInfo* lang = get_language_short_info(locale);
            if (lang)
                return lang;
        }
    return language;
}
#endif

#ifdef __linux__ 
bool Translations::is_available_locale(LanguageShortInfo* language_info)
{
    // remember previous locale for the case, when new locale is unavailable and we need to revert selection
    const auto old_locale = std::setlocale(LC_NUMERIC, "");

    const std::string num_locale = (
        language_info->language == m_sys_locale_language ?
        // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
        // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
        m_sys_locale :
        language_info->canonical_ref
    ) + ".UTF-8";

    bool is_available = std::setlocale(LC_NUMERIC, num_locale.c_str()) != nullptr;

    if (!is_available) {
        // If we can't find this locale , try to use different one for the language
        // instead of just reporting that it is impossible to switch.
        std::string original_lang = language_info->canonical_name;
        language_info = linux_get_existing_locale_language(language_info, m_language_short_info_system);
        SPDLOG_TRACE("Can't switch language to {} (missing locales). Using {} instead.",
                     original_lang, language_info->canonical_name);
        is_available = std::setlocale(LC_NUMERIC, language_info->canonical_ref.c_str()) != nullptr;
    }

    if (!is_available) {
        // revert old locale
        std::setlocale(LC_NUMERIC, old_locale);
    }

    return is_available;
}
#else
bool Translations::is_available_locale(LanguageShortInfo* language_info)
{
    // remember previous locale for the case, when new locale is unavailable and we need to revert selection
    const auto old_locale = std::setlocale(LC_NUMERIC, "");

    const std::string &num_locale = language_info->language == m_sys_locale_language ?
        // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
        // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
        m_sys_locale : 
        language_info->canonical_name;

    bool is_available = std::setlocale(LC_NUMERIC, num_locale.c_str()) != nullptr;

    if (!is_available) {
        // If we can't find this locale, try to use canonical ref value
        // instead of just reporting that it is impossible to switch.
        is_available = std::setlocale(LC_NUMERIC, language_info->canonical_ref.c_str()) != nullptr;
    }

    if (!is_available) {
        // revert old locale
        std::setlocale(LC_NUMERIC, old_locale);
    }

    return is_available;
}
#endif

void Translations::init_best_language()
{
    m_language_short_info_best = get_language_short_info(m_sys_locale_language);

    // Alternate language code.
    if (!m_language_short_info_best && m_sys_locale_language == "sk") {
        // Slovaks understand Czech well. Give them the Czech translation.
        m_language_short_info_best = get_language_short_info("cs");
        SPDLOG_TRACE("Using Czech dictionaries for Slovak language");
    }

    if (m_language_short_info_best)
        SPDLOG_TRACE("Best translation language detected (may be different from user locales): {}",
                     m_language_short_info_best->canonical_name);
}

LanguageShortInfo* Translations::get_language_short_info(const std::string& lang_tag)
{
    auto it = std::find(m_translations.begin(), m_translations.end(), LanguageShortInfo{ lang_tag });
    if (it == m_translations.end())
        return nullptr;

    return &m_translations[it - m_translations.begin()];
}

bool Translations::set_best_translation_for_language(const std::string& language) 
{
    auto language_info = language.empty() ? nullptr : get_language_short_info(language);

    if (!language_info && m_language_short_info_system) {
       language_info = m_language_short_info_system;
    }
    if (!language_info && m_language_short_info_best) {
       language_info = m_language_short_info_best;
    }
    if (!language_info)
        language_info = get_language_short_info("en");

    ASSERT(language_info != nullptr);
    // System locale may not be available and that should not be needed
    // if (!is_available_locale(language_info))
    //     return false;

    m_language_short_info_active = language_info;

    // Create new ui locale and save it.
    set_ui_locale(m_locale_generator(m_language_short_info_active->canonical_name + ".UTF-8"));
    return true; 
}

bool Translations::is_alternative_language() const
{
    return  m_language_short_info_active->language == "cs" && m_sys_locale_language == "sk";
}

}
