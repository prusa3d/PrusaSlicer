#include "Slic3r/Biz/MatDb/MatDbClient.hpp"

#include <ranges>
#include <set>

#include <boost/algorithm/string/find.hpp>
#include <nlohmann/json.hpp>

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/MatDb/MatDbClient.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"

namespace nlohmann {

template <typename T>
concept BaseRec = requires(T& v)
{
    { v.name } -> std::convertible_to<std::string>;
    { v.slug } -> std::convertible_to<std::string>;
    { v.uuid } -> std::convertible_to<std::string>;
};

template <BaseRec T>
void from_json_base_rec(const json& j, T& v)
{
    j.at("n").get_to(v.name);
    j.at("sg").get_to(v.slug);
    j.at("id").get_to(v.uuid);
}


template <>
struct adl_serializer<Slic3r::Domain::MatDb::BrandIndex>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::BrandIndex& v)
    {
        from_json_base_rec(j, v);
    }
};

template <>
struct adl_serializer<Slic3r::Domain::MatDb::MaterialIndex>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::MaterialIndex& v)
    {
        j.at("n").get_to(v.name);
        j.at("t").get_to(v.type);
        j.at("bs").get_to(v.brand_slug);
        j.at("i").get_to(v.index);
    }
};


template <>
struct adl_serializer<Slic3r::Domain::MatDb::Index>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::Index& v)
    {
        std::vector<Slic3r::Domain::MatDb::BrandIndex> brands;
        adl_serializer<decltype(brands)>::from_json(j.at("brands"), brands);
        std::ranges::transform(
            brands,
            std::inserter(v.brands, v.brands.end()),
            [](const auto& b) { return std::make_pair(b.slug, b); }
        );
        adl_serializer<decltype(v.materials)>::from_json(j.at("materials"), v.materials);
        std::ranges::transform(
            v.materials | std::views::filter([](const auto& m) { return m.type.has_value(); }),
            std::inserter(v.types, v.types.end()),
            [](const auto& m) { return m.type.value(); }
        );
    }
};

template <>
struct adl_serializer<Slic3r::Domain::MatDb::MaterialStorage>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::MaterialStorage& v)
    {
        from_json_base_rec(j, v);
        j.at("c").get_to(v.klass);
        if (j.contains("t")) {
            j.at("t").get_to(v.type);
        } else {
            v.type = std::nullopt;
        }
        j.at("a").get_to(v.abbrev);
        if (j.contains("pc")) {
            j.at("pc").get_to(v.primary_color);
        } else {
            v.primary_color = std::nullopt;
        }
        if (j.contains("sc")) {
            j.at("sc").get_to(v.secondary_colors);
        } else {
            v.secondary_colors = {};
        }
        j.at("tg").get_to(v.tags);
    }
};

template <>
struct adl_serializer<Slic3r::Domain::MatDb::BrandStorage>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::BrandStorage& v)
    {
        from_json_base_rec(j, v);
    }
};

template <>
struct adl_serializer<Slic3r::Domain::MatDb::BrandData>
{
    static void from_json(const json& j, Slic3r::Domain::MatDb::BrandData& v)
    {
        adl_serializer<decltype(v.brand)>::from_json(j, v.brand);
        adl_serializer<decltype(v.materials)>::from_json(j.at("materials"), v.materials);
    }
};
} // namespace nlohmann

namespace Slic3r::Biz::MatDb {

namespace {
bool contains(std::string_view text, const std::set<std::string_view>& search)
{
    return search.empty()
        || std::ranges::
            any_of(search, [&text](const auto& s) -> bool { return boost::ifind_first(text, s); });
}

std::optional<Domain::MatDb::Color> color_from_storage(const std::string& s)
{
    Domain::ColorRGBA c;
    if (!Algorithms::Color::decode_color(s, c)) {
        return std::nullopt;
    }
    return Domain::MatDb::Color{.color_rgba = s, .native=c};
}

template <typename T, typename S>
std::optional<T> map(const std::optional<S>& source, const std::function<std::optional<T>(const S&)>& transform)
{
    if (!source.has_value()) {
        return std::nullopt;
    }
    return transform(source.value());
}

std::vector<Domain::MatDb::Color> colors_from_storage(const Domain::MatDb::StringList& colors)
{
    std::vector<Domain::MatDb::Color> ret;
    for (const auto& c : colors) {

        if (auto color = color_from_storage(c); color.has_value()) {
            ret.push_back(color.value());
        }
    }
    return ret;
}

Domain::MatDb::Material from_storage(const Domain::MatDb::BrandStorage& brand, const Domain::MatDb::MaterialStorage& material)
{
    return {
        .uuid = material.uuid,
        .slug = material.slug,
        .name = material.name,
        .brand = {
            .uuid = brand.uuid,
            .slug = brand.slug,
            .name = brand.name,
        },
        .klass = material.klass,
        .type = material.type,
        .abbreviation = material.abbrev,
        .primary_color = map<Domain::MatDb::Color, std::string>(material.primary_color, color_from_storage),
        .secondary_colors = colors_from_storage(material.secondary_colors),
        .tags = material.tags,
    };
}

auto match_query(const MatDbClient::Query& q, const Domain::MatDb::Index& index)
{
    std::set<std::string_view> tokens;
    std::string_view sv{q.name_search};
    std::ranges::copy(
        sv
            | std::views::split(' ')
            | std::views::
                transform([](const auto& r) -> std::string_view { return {r.begin(), r.end()}; })
            | std::views::filter([](const auto& s) { return !s.empty(); }),
        std::inserter(tokens, tokens.end())
    );
    return [&q, tokens, &index](const Domain::MatDb::MaterialIndex& mat_idx)
    {
        bool ret = true;
        if (!tokens.empty()) {
            ret = contains(mat_idx.name, tokens)
                || (mat_idx.type.has_value() && contains(mat_idx.type.value(), tokens));
            if (!ret) {
                auto brand_it = index.brands.find(mat_idx.brand_slug);
                if (brand_it != index.brands.end()) {
                    ret = contains(brand_it->second.name, tokens);
                }
            }
        }
        if (!q.types.empty()) {
            ret = ret
                && std::ranges::any_of(
                      q.types,
                      [&](const auto& t) { return mat_idx.type.has_value() && mat_idx.type == t; }
                );
        }
        return ret;
    };
}

} // namespace

bool MatDbClient::open(const std::string& path)
{
    m_content = Crypto::create_zip_source(path);
    if (m_content == nullptr) {
        SPDLOG_ERROR("Cannot open {}", path);
        return false;
    }
    bool success = load_index();
    if (!success) {
        m_index.clear();
        m_content = nullptr;
    }
    return success;
}

SearchResult MatDbClient::query(const Query& q, size_t count, size_t offset)
{
    SearchResult ret;
    if (m_content) {
        auto range = m_index.materials
            | std::views::filter(match_query(q, m_index))
            | std::views::drop(offset)
            | std::views::take(count);
        std::ranges::transform(
            range,
            std::back_inserter(ret),
            [this](const auto& idx) -> Domain::MatDb::Material
            {
                const auto& brand = get(idx.brand_slug);
                return from_storage(brand.brand, brand.materials.at(idx.index));
            }
        );
    }
    return ret;
}

const std::set<std::string>& MatDbClient::types() const
{
    return m_index.types;
}

bool MatDbClient::load_index()
{
    if (m_content == nullptr) {
        return false;
    }
    auto index_raw = m_content->file_as_text("index.json");
    if (!index_raw.has_value()) {
        SPDLOG_ERROR("Cannot open {}/index.json", m_content->path());
        return false;
    }

    try {
        nlohmann::json::parse(index_raw.value()).get_to(m_index);
    } catch (nlohmann::json::exception& e) {
        SPDLOG_ERROR("Cannot parse {}/index.json: {}", m_content->path(), e.what());
        return false;
    }

    std::set<std::string> types;
    std::ranges::copy(
        m_index.materials
            | std::views::filter([](const auto& m) { return m.type.has_value(); })
            | std::views::transform([](const auto& m) { return m.type.value(); }),
        std::inserter(types, types.end())
    );
    std::ranges::copy(types, std::back_inserter(m_types));

    return true;
}

const Domain::MatDb::BrandData& MatDbClient::get(const std::string& brand_slug)
{
    return m_cache.get(brand_slug, [this](const std::string& slug) -> Domain::MatDb::BrandData
    {
        Domain::MatDb::BrandData ret;
        auto data = m_content->file_as_text(slug +".json");

        // TODO: handle error
        nlohmann::json::parse(data.value()).get_to(ret);
        return ret;
    });
}

} // namespace Slic3r::Biz::MatDb
