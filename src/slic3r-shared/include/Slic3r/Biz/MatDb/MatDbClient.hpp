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
    bool open(const std::string& path);
    SearchResult query_by_name(const std::string& search_what, size_t count, size_t offset);
    SearchResult query_by_type(const std::string& search_what, size_t count, size_t offset);
private:
    bool load_index();
    const Domain::MatDb::BrandData& get(const std::string& brand_slug);
private:
    std::unique_ptr<Crypto::IContentProvider> m_content;
    Domain::MatDb::Index m_index;
    Domain::MatDb::StringList m_types;
    Algorithms::LruCache<std::string, Domain::MatDb::BrandData> m_cache{32};
};

} // namespace Slic3r::Biz::MatDb
