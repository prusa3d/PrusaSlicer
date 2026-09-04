#pragma once

#include "LanguageInfo.hpp"

#include <string>
#include <vector>
#include <set>

#include <boost/filesystem.hpp>
#include <boost/locale/generator.hpp>

namespace Slic3r::Biz {

struct LanguageShortInfo
{
    std::string         canonical_name;
    std::string         local_tag;
    std::string         description;
    std::string         canonical_ref;
    std::string         language;   // value in canonical_name before first "_"
    std::string         country;    // value in canonical_name after first "_"

    bool operator==(const LanguageShortInfo& other) const
    {
        return  (other.canonical_name == this->canonical_name);
    }
};

extern void set_ui_locale(std::locale new_locale);

class Translations 
{
public:
    void    init_translations(const boost::filesystem::path& local_dir);
    bool    set_best_translation_for_language(const std::string& s);
    bool    is_alternative_language() const;

    const std::vector<LanguageShortInfo>&   languages()         const { return m_translations; }
    const std::string                       active_language()   const { return m_language_short_info_active ? m_language_short_info_active->canonical_name : "";
    }

    const std::string language_description(const std::string& name) const;

private:
    std::set<std::string>   load_available_languages(const boost::filesystem::path& local_dir);
    LanguageShortInfo*      get_language_short_info(const std::string& lang);

    void    init_system_locale_values();
    void    init_system_language();
    void    init_best_language();

    bool    is_available_locale(LanguageShortInfo* language_info);

#ifdef __linux__
    LanguageShortInfo* linux_get_existing_locale_language(LanguageShortInfo* language,
                                                                const LanguageShortInfo* system_language);
#endif // __linux__

private:
    std::string                     m_sys_locale;
    std::string                     m_sys_locale_language;

    std::vector<LanguageShortInfo>  m_translations;

    LanguageShortInfo*              m_language_short_info_active    { nullptr };
    LanguageShortInfo*              m_language_short_info_system    { nullptr };
    LanguageShortInfo*              m_language_short_info_best      { nullptr };

    boost::locale::generator        m_locale_generator;
};

}
