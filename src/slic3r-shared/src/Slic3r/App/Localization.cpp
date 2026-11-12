#include "Slic3r/App/Localization.hpp"

#include "Slic3r/Directories.hpp"

namespace Slic3r::App {

Localization& Localization::instance()
{
    static Localization inst;
    return inst;
}

Localization::Localization()
{
    m_translations.init_translations(boost::filesystem::path(Slic3r::localization_dir()));
}

Localization& localization()
{
    return Localization::instance();
}

bool Localization::set_language(const std::string& language)
{
    if (m_translations.set_best_translation_for_language(language)) {
        invoke_listeners<ILanguageChangedListener>([](auto * l) {
            l->on_language_changed(); });
        return true;
    }
    return false;
}

bool Localization::is_alternative_language() const
{
    return m_translations.is_alternative_language();
}

const std::string Localization::active_language() const
{
    return m_translations.active_language();
}

const std::string Localization::language_description(const std::string& name) const
{
    return m_translations.language_description(name);
}

const std::vector<Biz::LanguageShortInfo>& Localization::languages() const
{
    return m_translations.languages();
}

} //namespace Slic3r::App
