#pragma once

#include <optional>

#include "Slic3r/Domain/MatDb/Material.hpp"
#include "Slic3r/Domain/Preset/Types.hpp"

namespace Slic3r::Domain::Preset {

using OptString = std::optional<std::string>;

struct MatDbMatchRule
{
    OptString uuid;
    OptString brand_slug;
    OptString slug;
    OptString name;

    OptString klass;
    OptString type;
    OptString abbreviation;
    Strings required_tags;
    Strings optional_tags;
    Strings exclude_tags;

    size_t match(const MatDb::Material& mat) const;
};


}
