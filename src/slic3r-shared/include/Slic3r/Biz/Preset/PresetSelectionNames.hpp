#pragma once

#include <string>
#include <vector>

namespace Slic3r::Biz::Preset {

struct PresetSelectionNames
{
    struct PresetName
    {
        std::string name;
        Domain::Preset::PresetOrigin origin;
        bool is_runtime_only;

        bool operator==(const PresetName& other) const
        {
            return this->name == other.name && this->origin == other.origin;
        }
    };

    PresetName printer;
    PresetName print;
    std::vector<PresetName> tools;
    std::vector<PresetName> materials;
};

} // namespace Slic3r::Biz::Preset