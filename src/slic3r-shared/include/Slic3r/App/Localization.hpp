#pragma once

#include "ILanguageChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerList.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/I18N/Translation.hpp"

// ! Singleton Class  

namespace Slic3r::App {

class Localization : public WithListeners<ILanguageChangedListener> {

public:
    Localization(const Localization& obj) = delete;

    static Localization& instance();

    bool set_language(const std::string& language);
    bool is_alternative_language() const;
    const std::string   active_language() const;
    const std::string   language_description(const std::string& name) const;
    const std::vector<Biz::LanguageShortInfo>& languages() const;

private:
    Localization();

private:
    Biz::Translations m_translations;
};

Localization& localization();

} // Slic3r::App

