#include <sstream>

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
#define MATCH_MATERIAL_IF(N, R) if (match_eq(mat.N, N)) rank += (R);
#define MATCH_MATERIAL_IF_REQUIRED(N, R)\
    if (match_eq(mat.N, N)) {           \
        rank += (R);                    \
    } else if ((N).has_value()) {       \
        return 0;                       \
    }
#define MATCH_BRAND_IF(N, R) if (match_eq(mat.brand.N, brand_##N)) rank += (R);
#define MATCH_BRAND_IF_REQUIRED(N, R)       \
    if (match_eq(mat.brand.N, brand_##N)) { \
        rank += (R);                        \
    } else if ((brand_##N).has_value()) {   \
        return 0;                           \
    }

    MATCH_MATERIAL_IF(uuid, RANK_OPT)
    MATCH_BRAND_IF_REQUIRED(slug, RANK_REQ)
    MATCH_MATERIAL_IF(slug, RANK_OPT)
    MATCH_MATERIAL_IF(name, RANK_OPT)
    MATCH_MATERIAL_IF(klass, RANK_OPT)
    MATCH_MATERIAL_IF_REQUIRED(type, RANK_REQ)
    MATCH_MATERIAL_IF_REQUIRED(abbreviation, RANK_REQ)

#undef MATCH_MATERIAL_IF
#undef MATCH_MATERIAL_IF_REQUIRED
#undef MATCH_BRAND_IF
#undef MATCH_BRAND_IF_REQUIRED

    if (!match_includes(mat.tags, required_tags)) {
        return 0;
    } else if (!required_tags.empty()) {
        rank += RANK_ATTR_RANGE;
    }

    if (!optional_tags.empty()) {
        rank += RANK_ATTR_RANGE * match_includes_count(mat.tags, optional_tags) / optional_tags.size();
    }

    if (!match_excludes(mat.tags, exclude_tags)) {
        return 0;
    } else if (!exclude_tags.empty()) {
        rank += RANK_ATTR_RANGE;
    }
    return rank;
}

std::string to_string(const MatDbMatchRule& r)
{
    std::ostringstream os;

#define DUMP_FIELD_STR(field)                   \
    if (r.field.has_value()) {                  \
        os << " " #field "=" << r.field.value();\
    }
#define DUMP_FIELD_STRS(field)                  \
    if (!r.field.empty()) {                     \
        os << " " #field "=(";                  \
        bool first = true;                      \
        for (const auto& f : r.field ) {        \
            if (first) {                        \
                first = false;                  \
            } else {                            \
                os << ", ";                     \
            }                                   \
            os << f;                            \
        }                                       \
        os << ")";                              \
    }

    // OptString uuid;
    // OptString brand_slug;
    // OptString slug;
    // OptString name;
    // OptString klass;
    // OptString type;
    // OptString abbreviation;
    // Strings required_tags;
    // Strings optional_tags;
    // Strings exclude_tags;

    os << "[Rule";

    DUMP_FIELD_STR(uuid)
    DUMP_FIELD_STR(brand_slug)
    DUMP_FIELD_STR(slug)
    DUMP_FIELD_STR(name)
    DUMP_FIELD_STR(klass)
    DUMP_FIELD_STR(type)
    DUMP_FIELD_STR(abbreviation)
    DUMP_FIELD_STRS(required_tags)
    DUMP_FIELD_STRS(optional_tags)
    DUMP_FIELD_STRS(exclude_tags)

    os << "]";

#undef DUMP_FIELD_STR
#undef DUMP_FIELD_STRS

    return os.str();
}

} // namespace Slic3r::Domain::Preset
