#include "libslic3r/ExtruderCandidates.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"

using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

namespace Slic3r::Biz::Slicing {

std::vector<unsigned int> get_painting_extruders(
    const ModelObject& model_object,
    const unsigned int num_extruders,
    const VirtualExtruders& virtual_extruders
)
{
    if (!model_object.is_mm_painted()) {
        return {};
    }

    std::array<bool, TRIANGLE_STATE_TYPE_COUNT> used_facet_states{};
    for (const Domain::ModelVolume* volume : model_object.volumes) {
        if (volume->is_mm_painted()) {
            const std::vector<bool>& volume_used_facet_states{
                volume->mm_segmentation_facets.get_data().used_states
            };

            assert(volume_used_facet_states.size() <= used_facet_states.size());
            for (size_t state_idx = 1;
                 state_idx < std::min(volume_used_facet_states.size(), used_facet_states.size());
                 ++state_idx)
            {
                used_facet_states[state_idx] |= volume_used_facet_states[state_idx];
            }
        }
    }

    std::vector<unsigned int> result;
    for (size_t state_idx = static_cast<size_t>(TriangleStateType::Extruder1);
         state_idx < used_facet_states.size();
         ++state_idx)
    {
        if (used_facet_states[state_idx]
            && (state_idx <= num_extruders
                || Algorithms::VirtualExtruder::is_virtual_extruder(
                    static_cast<unsigned int>(state_idx),
                    virtual_extruders
                )))
        {
            result.emplace_back(state_idx);
        }
    }

    return Algorithms::VirtualExtruder::expand_virtual_extruders_1based(result, virtual_extruders);
}

static std::set<unsigned> get_extra_support_extruders(
    const Domain::ObjectSettings& object_settings,
    const Domain::PrintSettings& print_settings
)
{
    const bool support_material{
        print_settings.items.opt("support_material").get<Domain::SupportMode>()
        != Domain::SupportMode::None
    };
    const bool raft{print_settings.items.opt("raft_layers").get<int>() > 0};

    std::optional<Domain::ConfigItem> object_support_material_opt{
        object_settings.overrides.get("support_material")
    };
    const bool object_support_material{
        object_support_material_opt
        && (*object_support_material_opt).get<Domain::SupportMode>() != Domain::SupportMode::None
    };
    if (!support_material && !object_support_material && !raft) {
        return {};
    }

    std::set<unsigned> result;

    for (const std::string key :
         {"support_material_extruder", "support_material_interface_extruder"})
    {
        const std::optional<Domain::ConfigItem> object_supports_extruder_item{
            object_settings.overrides.get(key)
        };
        if (object_supports_extruder_item) {
            const auto object_supports_extruder{object_supports_extruder_item->get<int>()};
            if (object_supports_extruder > 0) {
                result.insert(static_cast<unsigned>(object_supports_extruder - 1));
                continue;
            }
        }
        const int print_supports_extruder{print_settings.items.opt(key).get<int>()};
        if (print_supports_extruder > 0) {
            result.insert(static_cast<unsigned>(print_supports_extruder - 1));
        }
    }
    return result;
}

std::set<unsigned> get_volume_extruder_candidates(
    const Domain::VolumeSettings& volume_settings,
    const Domain::ObjectSettings& object_settings,
    const Domain::PrintSettings& print_settings,
    const VirtualExtruders& virtual_extruders
)
{
    std::set<unsigned> result;

    const int object_default_extruder{object_settings.items.opt("extruder").get<int>()};
    const std::optional<Domain::ConfigItem> volume_default_extruder_item{
        volume_settings.overrides.get("extruder")
    };
    const std::optional<int> volume_default_extruder{
        volume_default_extruder_item ? std::optional{volume_default_extruder_item->get<int>()} :
                                       std::nullopt
    };
    for (const std::string key : {"perimeter_extruder", "infill_extruder", "solid_infill_extruder"})
    {
        const std::optional<Domain::ConfigItem> volume_extruder{volume_settings.overrides.get(key)};
        if (volume_extruder && volume_extruder->get<int>() > 0) {
            result.insert(static_cast<unsigned>(volume_extruder->get<int>() - 1));
            continue;
        }
        const std::optional<Domain::ConfigItem> object_extruder{object_settings.overrides.get(key)};
        if (object_extruder && object_extruder->get<int>() > 0) {
            result.insert(static_cast<unsigned>(object_extruder->get<int>() - 1));
            continue;
        }
        if (volume_default_extruder && volume_default_extruder != 0) {
            result.insert(static_cast<unsigned>(*volume_default_extruder) - 1);
            continue;
        }
        if (object_default_extruder != 0) {
            result.insert(static_cast<unsigned>(object_default_extruder) - 1);
            continue;
        }
        result.insert(static_cast<unsigned>(print_settings.items.opt(key).get<int>()) - 1);
    }

    const std::vector<unsigned> collected_extruders{result.begin(), result.end()};
    const std::vector<unsigned> expanded_extruders{
        Algorithms::VirtualExtruder::expand_virtual_extruders_0based(
            collected_extruders,
            virtual_extruders
        )
    };

    return std::set<unsigned>{expanded_extruders.begin(), expanded_extruders.end()};
}

/**
 * @brief Returns whether the volume is painted with an extruder that does not exist.
 *
 * Facets painted with such an extruder are printed with the default extruder of the volume.
 *
 * @param volume Volume with multi-material painting.
 * @param num_extruders Number of physical extruders of the current printer.
 * @param virtual_extruders Virtual extruder definitions valid for the current printer.
 * @return True when any painting extruder of the volume is neither a physical slot
 *         nor a declared virtual extruder.
 */
bool has_paint_extruder_exceeding_extruder_count(
    const ModelVolume& volume,
    const size_t num_extruders,
    const VirtualExtruders& virtual_extruders
)
{
    const std::vector<bool>& used_facet_states =
        volume.mm_segmentation_facets.get_data().used_states;

    for (size_t state_idx = static_cast<size_t>(TriangleStateType::Extruder1);
         state_idx < used_facet_states.size();
         ++state_idx)
    {
        if (used_facet_states[state_idx] && state_idx > num_extruders
            && !Algorithms::VirtualExtruder::is_virtual_extruder(static_cast<unsigned int>(state_idx), virtual_extruders)) {
            return true;
        }
    }

    return false;
}

std::set<unsigned> get_object_extruder_candidates(
    const Domain::ModelObject& object,
    const Domain::IConfigPackFDMViewer& config,
    const VirtualExtruders& virtual_extruders
)
{
    const bool all_instances_not_printable{std::ranges::all_of(
        object.instances,
        [](const Domain::ModelInstance* model_instance) { return !model_instance->printable; }
    )};
    if (all_instances_not_printable) {
        return {};
    }

    const Domain::PrintSettings& print_settings{config.get_print()};

    std::set<unsigned> extruders;
    const Domain::ObjectSettings& object_settings{object.object_settings};

    for (const auto& pair : object.layer_config_ranges) {
        const Domain::VolumeSettings& volume_settings{pair.second};
        extruders.merge(get_volume_extruder_candidates(
            volume_settings,
            object_settings,
            print_settings,
            virtual_extruders
        ));
    }

    for (const Domain::ModelVolume* volume : object.volumes) {
        using Domain::ModelVolumeType::MODEL_PART;
        using Domain::ModelVolumeType::PARAMETER_MODIFIER;
        if (volume->type() != MODEL_PART && volume->type() != PARAMETER_MODIFIER) {
            continue;
        }

        // A fully painted part doesn't use its default extruders, unless some painting extruder doesn't exist.
        const bool default_extruders_unused = volume->type() == MODEL_PART
            && volume->is_fully_mm_painted()
            && !has_paint_extruder_exceeding_extruder_count(
                                                  *volume,
                                                  config.filament_size(),
                                                  virtual_extruders
            );

        if (default_extruders_unused) {
            continue;
        }

        extruders.merge(get_volume_extruder_candidates(
            volume->volume_settings,
            object.object_settings,
            config.get_print(),
            virtual_extruders
        ));
    }
    const std::vector<unsigned> painting_extruders{
        get_painting_extruders(object, config.filament_size(), virtual_extruders)
    };
    for (unsigned extruder : painting_extruders) {
        extruders.insert(extruder - 1);
    }

    std::set<unsigned> support_extruders{
        get_extra_support_extruders(object_settings, config.get_print())
    };
    extruders.merge(support_extruders);

    return extruders;
}

std::vector<unsigned> get_extruder_candidates(
    const Domain::Model& model,
    const Domain::IConfigPackFDMViewer& config,
    const Domain::BedInstance& bed,
    const VirtualExtruders& virtual_extruders
)
{
    ASSERT(config.tool_size() > 0);
    std::set<unsigned> extruders;

    if (bed.custom_gcode) {
        for (const Domain::CustomGCode::Item& custom_gcode : bed.custom_gcode->gcodes) {
            if (custom_gcode.type == Domain::CustomGCode::Type::ToolChange) {
                ASSERT(custom_gcode.extruder > 0);
                extruders.insert(custom_gcode.extruder - 1);
            }
        }
    }

    for (const Domain::ModelObject* object : model.objects) {
        extruders.merge(get_object_extruder_candidates(*object, config, virtual_extruders));
    }

    const bool can_have_wipe_tower{
        !config.get_print().items.opt("spiral_vase").get<bool>()
        && config.get_print().items.opt("wipe_tower").get<bool>()
        && extruders.size() > 1
    };

    if (can_have_wipe_tower) {
        const int wipe_tower_extruder{
            config.get_print().items.opt("wipe_tower_extruder").get<int>()
        };
        if (wipe_tower_extruder > 0
            && static_cast<size_t>(wipe_tower_extruder) < config.filament_size() + 1)
        {
            extruders.insert(static_cast<unsigned>(wipe_tower_extruder - 1));
        }
    }

    std::vector<unsigned> result;
    for (unsigned extruder : extruders) {
        if (extruder < config.filament_size()) {
            result.push_back(extruder);
        }
    }

    return result;
}
} // namespace Slic3r::Biz::Slicing
