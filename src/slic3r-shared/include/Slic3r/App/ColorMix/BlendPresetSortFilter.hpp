#pragma once

#include "Slic3r/App/ColorMix/BlendRecipeTile.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include <functional>

namespace Slic3r::App::ColorMix {

class BlendPresetSortFilter : public Biz::ObservableListSortFilter<BlendTileData, size_t>
{
public:
    using FnExtruderEnabled = std::function<bool(unsigned int extruder_id_1based)>;

    explicit BlendPresetSortFilter(FnExtruderEnabled is_extruder_enabled);

private:
    FnExtruderEnabled m_is_extruder_enabled;
};

} // namespace Slic3r::App::ColorMix
