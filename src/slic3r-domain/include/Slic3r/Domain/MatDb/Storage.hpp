#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Slic3r/Domain/MatDb/Types.hpp"

namespace Slic3r::Domain::MatDb {



struct BrandStorage
{
    std::string uuid;
    std::string slug;
    std::string name;
};

struct MaterialStorage
{
    std::string uuid;
    std::string slug;
    std::string name;

    std::string klass;
    std::optional<std::string> type;
    std::string abbrev;
    std::optional<std::string> primary_color;
    StringList secondary_colors;
    StringList tags;
};

struct BrandData
{
    BrandStorage brand;
    std::vector<MaterialStorage> materials;
};

struct MaterialIndex
{
    std::string name;
    std::optional<std::string> type;
    std::string brand_slug;
    size_t index;
};

struct BrandIndex
{
    std::string uuid;
    std::string slug;
    std::string name;
};

struct Index
{
    std::vector<BrandIndex> brands;
    std::vector<MaterialIndex> materials;
};

} // namespace Slic3r::Domain::MatDb
