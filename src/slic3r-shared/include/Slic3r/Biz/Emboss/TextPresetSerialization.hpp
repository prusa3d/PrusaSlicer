#pragma once

#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/Biz/Emboss/TextPresetManager.hpp"

namespace cereal {

template <class Archive>
void serialize(Archive& ar, Slic3r::Biz::Emboss::TextPresetManager::Preset& preset)
{
    ar(preset.emboss_style, preset.projection, preset.distance, preset.angle);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Biz::Emboss::TextPresetManager::PresetsObj& data, const std::uint32_t version)
{
    if constexpr (Archive::is_loading::value) {
        if (version == 1) {
            // Preserve existing presets whose FontProp record predates bend_arc.
            cereal::size_type count = 0;
            ar(cereal::make_size_tag(count));
            data.presets.resize(static_cast<size_t>(count));
            for (auto& preset : data.presets) {
                auto& prop = preset.emboss_style.prop;
                ar(preset.emboss_style.descriptor);
                ar(prop.size_in_mm, prop.per_glyph, prop.align, prop.char_gap, prop.line_gap,
                    prop.boldness, prop.skew, prop.collection_number, prop.bend_horizontal, prop.bend_vertical);
                prop.bend_arc.reset();
                ar(preset.projection, preset.distance, preset.angle);
            }
            ar(data.current_index);
            return;
        }
    }
    if (version != 2)
        return;
    ar(data.presets, data.current_index);
}

} // namespace cereal

CEREAL_CLASS_VERSION(Slic3r::Biz::Emboss::TextPresetManager::PresetsObj, 2);
