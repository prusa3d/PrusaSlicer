#include "Slic3r/Biz/VirtualExtruderInteractor.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderGradientStop;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::Workbench;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;

namespace Slic3r::Biz {

namespace {
/**
 * @brief Physical extruder slot count of the given config container.
 * @return 0 when the config container is not FDM.
 */
unsigned int physical_slot_count(const ConfigContainer& config_container)
{
    if (config_container.print_technology() != PrinterTechnology::FFF) {
        return 0;
    }

    return static_cast<unsigned int>(
        config_container.selected_preset().hw_config.material_slot_count()
    );
}

} // namespace

VirtualExtruderInteractor::VirtualExtruderInteractor(Workbench& workbench) : m_workbench(workbench)
{}

const VirtualExtruders& VirtualExtruderInteractor::
    virtual_extruders(SelectionId project_id, SelectionId config_container_id) const
{
    // Empty fallback for an unknown project or printer group.
    static const VirtualExtruders empty_virtual_extruders;

    const Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return empty_virtual_extruders;
    }

    const ConfigContainer* config_container = project->find_config_container(config_container_id);
    if (config_container == nullptr) {
        return empty_virtual_extruders;
    }

    return config_container->virtual_extruders();
}

tl::expected<void, std::string> VirtualExtruderInteractor::set_virtual_extruders(
    SelectionId project_id,
    SelectionId config_container_id,
    const VirtualExtruders& virtual_extruders
)
{
    Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return tl::make_unexpected(std::string("Project not found."));
    }

    ConfigContainer* config_container = project->find_config_container(config_container_id);
    if (config_container == nullptr) {
        return tl::make_unexpected(std::string("Printer group not found."));
    }

    VirtualExtruders normalized_virtual_extruders =
        Algorithms::VirtualExtruder::normalize_virtual_extruders(virtual_extruders);

    const unsigned int physical_slots = physical_slot_count(*config_container);
    constexpr unsigned int max_virtual_extruder_id =
        static_cast<unsigned int>(TRIANGLE_STATE_TYPE_COUNT) - 1;

    std::vector<std::string> violation_messages;

    std::map<unsigned int, std::size_t> surviving_id_counts;
    for (const VirtualExtruder& virtual_extruder : normalized_virtual_extruders) {
        ++surviving_id_counts[virtual_extruder.id];
    }

    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        const auto it = surviving_id_counts.find(virtual_extruder.id);
        if (it == surviving_id_counts.end() || it->second == 0) {
            violation_messages.push_back(
                "Virtual extruder id "
                + std::to_string(virtual_extruder.id)
                + " was dropped by the normalization (malformed definition)."
            );
            continue;
        }

        --it->second;
    }

    std::set<unsigned int> seen_ids;
    for (const VirtualExtruder& virtual_extruder : normalized_virtual_extruders) {
        if (virtual_extruder.id < 1 || virtual_extruder.id > max_virtual_extruder_id) {
            violation_messages.push_back(
                "Virtual extruder id "
                + std::to_string(virtual_extruder.id)
                + " is outside the valid range 1.."
                + std::to_string(max_virtual_extruder_id)
                + "."
            );

            continue;
        }

        if (seen_ids.contains(virtual_extruder.id)) {
            violation_messages.push_back(
                "Virtual extruder id "
                + std::to_string(virtual_extruder.id)
                + " collides with another virtual extruder."
            );

            continue;
        }

        if (physical_slots != 0 && virtual_extruder.id <= physical_slots) {
            violation_messages.push_back(
                "Virtual extruder id "
                + std::to_string(virtual_extruder.id)
                + " falls inside the physical slot range (1.."
                + std::to_string(physical_slots)
                + ")."
            );

            continue;
        }

        seen_ids.insert(virtual_extruder.id);
    }

    for (const VirtualExtruder& virtual_extruder : normalized_virtual_extruders) {
        if (virtual_extruder.gradient.has_value()) {
            for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops) {
                if (stop.extruder_id == 0 || stop.extruder_id > physical_slots) {
                    violation_messages.push_back(
                        "Virtual extruder id "
                        + std::to_string(virtual_extruder.id)
                        + " references extruder "
                        + std::to_string(stop.extruder_id)
                        + ", which is outside the physical slot range (1.."
                        + std::to_string(physical_slots)
                        + ")."
                    );

                    break;
                }
            }

            continue;
        }

        for (const VirtualExtruderComponent& component : virtual_extruder.components) {
            if (component.extruder_id == 0 || component.extruder_id > physical_slots) {
                violation_messages.push_back(
                    "Virtual extruder id "
                    + std::to_string(virtual_extruder.id)
                    + " references extruder "
                    + std::to_string(component.extruder_id)
                    + ", which is outside the physical slot range (1.."
                    + std::to_string(physical_slots)
                    + ")."
                );

                break;
            }
        }
    }

    if (physical_slots < 2 && !virtual_extruders.empty()) {
        violation_messages.emplace_back(
            "The printer needs at least two physical slots for virtual extruders."
        );
    }

    if (!violation_messages.empty()) {
        std::string joined_message;
        for (const std::string& violation_message : violation_messages) {
            if (!joined_message.empty()) {
                joined_message += '\n';
            }
            joined_message += violation_message;
        }

        return tl::make_unexpected(std::move(joined_message));
    }

    write_virtual_extruders(*config_container, std::move(normalized_virtual_extruders));

    this->invoke_listeners<IVirtualExtrudersChangedListener>(
        [project_id, config_container_id](IVirtualExtrudersChangedListener* listener)
        { listener->on_virtual_extruders_changed(project_id, config_container_id); }
    );

    return {};
}

void VirtualExtruderInteractor::
    notify_virtual_extruders_changed(SelectionId project_id, SelectionId config_container_id)
{
    this->invoke_listeners<IVirtualExtrudersChangedListener>(
        [project_id, config_container_id](IVirtualExtrudersChangedListener* listener)
        { listener->on_virtual_extruders_changed(project_id, config_container_id); }
    );
}

void VirtualExtruderInteractor::restore_virtual_extruders_after_undo(
    SelectionId project_id,
    SelectionId config_container_id,
    VirtualExtruders virtual_extruders,
    InvokeLaterBag& listener_notifications
)
{
    Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return;
    }

    ConfigContainer* config_container = project->find_config_container(config_container_id);
    if (config_container == nullptr) {
        return;
    }

    write_virtual_extruders(*config_container, std::move(virtual_extruders));

    listener_notifications.add(
        [this, project_id, config_container_id]
        { this->notify_virtual_extruders_changed(project_id, config_container_id); }
    );
}

void VirtualExtruderInteractor::on_preset_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (type != Preset::PresetItemType::PrinterPreset) {
        return;
    }

    if (project_id == Domain::INVALID_ID || config_container_id == Domain::INVALID_ID) {
        return;
    }

    Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return;
    }

    ConfigContainer* config_container = project->find_config_container(config_container_id);
    if (config_container == nullptr) {
        return;
    }

    if (config_container->virtual_extruders().empty()) {
        return;
    }

    const unsigned int slot_count = physical_slot_count(*config_container);

    // Only this group's objects, because the remap below shifts their paint along with the ids.
    std::vector<ModelObject*> group_objects;
    for (ModelObject* object : project->model().objects) {
        const bool on_this_group = std::ranges::any_of(
            object->instances,
            [config_container_id](const ModelInstance* instance)
            { return instance->get_last_bed().config_container_id == config_container_id; }
        );

        if (on_this_group) {
            group_objects.push_back(object);
        }
    }

    unsigned int lowest_id = std::numeric_limits<unsigned int>::max();
    for (const VirtualExtruder& virtual_extruder : config_container->virtual_extruders()) {
        lowest_id = std::min(lowest_id, virtual_extruder.id);
    }

    const unsigned int source_physical_count = lowest_id > 0 ? lowest_id - 1 : 0;

    VirtualExtruders remapped_definitions = config_container->virtual_extruders();
    Algorithms::VirtualExtruder::remap_virtual_extruders_on_import(
        group_objects,
        remapped_definitions,
        slot_count,
        source_physical_count
    );

    VirtualExtruders compatible_definitions =
        Algorithms::VirtualExtruder::compatible_virtual_extruders(remapped_definitions, slot_count);
    if (compatible_definitions == config_container->virtual_extruders()) {
        return;
    }

    if (compatible_definitions.size() < remapped_definitions.size()) {
        SPDLOG_WARN(
            "Dropped {} virtual extruder definitions incompatible with the newly selected printer.",
            remapped_definitions.size() - compatible_definitions.size()
        );
    }

    write_virtual_extruders(*config_container, std::move(compatible_definitions));

    this->invoke_listeners<IVirtualExtrudersChangedListener>(
        [project_id, config_container_id](IVirtualExtrudersChangedListener* listener)
        { listener->on_virtual_extruders_changed(project_id, config_container_id); }
    );
}

void VirtualExtruderInteractor::
    write_virtual_extruders(ConfigContainer& config_container, VirtualExtruders virtual_extruders)
{
    config_container.virtual_extruders() = std::move(virtual_extruders);
}

} // namespace Slic3r::Biz
