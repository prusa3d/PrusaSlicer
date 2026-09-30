#pragma once

#include "Slic3r/Biz/Algorithms/LruCache.hpp"

#include <memory>

#include "Slic3r/Domain/MatDb/Storage.hpp"
#include "Slic3r/Domain/MatDb/Material.hpp"
#include "Slic3r/Biz/Crypto/ContentProvider.hpp"

namespace Slic3r::Biz::MatDb {

using SearchResult = std::vector<Domain::MatDb::Material>;

class MatDbClient
{
public:
    struct Query
    {
        std::string name_search;
        std::vector<std::string> types;
    };

    bool open(const std::string& path);
    SearchResult query(const Query& q, size_t count, size_t offset);

    const std::set<std::string>& types() const;
    std::optional<Domain::MatDb::Material> get_by_uuid(const std::string& uuid);

private:
    bool load_index();
    const Domain::MatDb::BrandData& get(const std::string& brand_slug);
private:
    std::unique_ptr<Crypto::IContentProvider> m_content;
    Domain::MatDb::Index m_index;
    Domain::MatDb::StringList m_types;
    std::map<std::string, std::pair<std::string_view, size_t>> m_uuid_lookup;
    Algorithms::LruCache<std::string, Domain::MatDb::BrandData> m_cache{32};
};

} // namespace Slic3r::Biz::MatDb
