#include "Slic3r/App/Search.hpp"

namespace Slic3r::App {

bool find_locale_aware(
    std::string_view source_text,
    std::string_view search_text,
    MatchCase match_case,
    const std::locale& loc
)
{
    using boost::locale::fold_case;
    using boost::locale::norm_nfc;
    using boost::locale::normalize;

    // Normalize to NFC to handle composed/decomposed forms equivalently.
    std::string source = normalize(std::string(source_text), norm_nfc, loc);
    std::string search = normalize(std::string(search_text), norm_nfc, loc);

    if (match_case == MatchCase::CaseInsensitive) {
        source = fold_case(source, loc);
        search = fold_case(search, loc);
    }

    std::string::iterator it = std::search(source.begin(), source.end(), search.begin(), search.end());
    return it != source.end();
}

} // namespace Slic3r::App
