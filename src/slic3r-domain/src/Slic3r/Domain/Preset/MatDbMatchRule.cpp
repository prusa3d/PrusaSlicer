#include <utility>

#include "Slic3r/Domain/Preset/MatDbMatchRule.hpp"

#include "Slic3r/Domain/TemplateUtils.hpp"

namespace Slic3r::Domain::Preset {

namespace {
template <typename T>
void count(size_t& rank, const T& v) {
    if constexpr (is_std_optional_v<T>) {
        if (v.has_value()) {
            rank++;
        }
    } else if constexpr (is_std_vector_v<T>) {
        rank += v.size();
    } else {
        static_assert("Not supported type");
    }
};

bool match_eq(const std::string& a, const OptString& b)
{
    return b.has_value() && a == b.value();
}

bool match_eq(const OptString& a, const OptString& b)
{
    return b.has_value() && a.has_value() && a.value() == b.value();
}

size_t match_includes_count(const Strings& subject, const Strings& pattern)
{
    return std::ranges::count_if(
        pattern,
        [&subject](const auto& p) { return std::ranges::find(subject, p) != subject.end(); }
    );
}

bool match_includes(const Strings& subject, const Strings& pattern)
{
    return std::ranges::all_of(
        pattern,
        [&subject](const auto& p) { return std::ranges::find(subject, p) != subject.end(); }
    );
}

bool match_excludes(const Strings& subject, const Strings& pattern)
{
    return std::ranges::all_of(
        pattern,
        [&subject](const auto& p) { return std::ranges::find(subject, p) == subject.end(); }
    );
}

} // namespace

size_t MatDbMatchRule::match(const MatDb::Material& mat) const
{
    /*
    OptString uuid;
    OptString brand_slug;
    OptString slug;
    OptString name;

    OptString klass;
    OptString type;
    OptString abbrev;
    Strings include_tags;
    Strings exclude_tags;
     */
    size_t rank{0};
#define MATCH_MATERIAL_IF(N) if (match_eq(mat.N, N)) rank++;
#define MATCH_MATERIAL_IF2(N, N2) if (match_eq(mat.N, N2)) rank++;
#define MATCH_BRAND_IF(N) if (match_eq(mat.brand.N, brand_##N)) rank++;

    MATCH_MATERIAL_IF(uuid)
    MATCH_BRAND_IF(slug)
    MATCH_MATERIAL_IF(slug)
    MATCH_MATERIAL_IF(name)
    MATCH_MATERIAL_IF(klass)
    MATCH_MATERIAL_IF(type)
    MATCH_MATERIAL_IF(abbreviation)

#undef MATCH_MATERIAL_IF
#undef MATCH_BRAND_IF

    if (!match_includes(mat.tags, required_tags)) {
        return 0;
    }
    rank += required_tags.size();

    rank += match_includes_count(mat.tags, optional_tags);

    if (!match_excludes(mat.tags, exclude_tags)) {
        return 0;
    }
    return rank;
}

} // namespace Slic3r::Domain::Preset
