#pragma once

#include <boost/locale.hpp>
#include "Slic3r/App/Localization.hpp"

namespace Slic3r::App {

enum class MatchCase
{
    CaseSensitive,
    CaseInsensitive
};

bool find_locale_aware(
    std::string_view source_text,
    std::string_view search_text,
    MatchCase match_case   = MatchCase::CaseInsensitive,
    const std::locale& loc = boost::locale::generator{}(localization().active_language() + ".UTF-8")
);

} // namespace Slic3r::App
