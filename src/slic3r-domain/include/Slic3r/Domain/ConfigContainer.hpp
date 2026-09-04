#pragma once

#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FindById.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <memory>
#include <vector>

namespace Slic3r::Domain {

class Bed;

class ConfigContainer : public ObjectBase
{
public:
    std::unique_ptr<ConfigContainer> copy(
        const std::unordered_map<const Bed*, const Bed*>& bed_translation_table,
        const std::unordered_map<ModelInstance*, ModelInstance*>& model_instance_translation_table
    ) const;

    Domain::PrinterTechnology print_technology() const
    {
        return m_preset.technology();
    }

    const Preset::SelectedPreset& selected_preset() const
    {
        return m_preset;
    }

    Preset::SelectedPreset& mutable_selected_preset()
    {
        return m_preset;
    }
    ConfigPack build_print_config() const;

    Domain::ProjectSettings& project_settings()
    {
        return m_project_settings;
    }

    const Domain::ProjectSettings& project_settings() const
    {
        return m_project_settings;
    }

    VirtualExtruders& virtual_extruders()
    {
        return m_virtual_extruders;
    }

    const VirtualExtruders& virtual_extruders() const
    {
        return m_virtual_extruders;
    }

    void set_bed(const Bed& bed)
    {
        m_bed = &bed;
    }

    const Bed& bed() const
    {
        return *ASSERT_VAL(m_bed);
    }

    /**
     * @name Bed instances management
     * @{
     */
    using BedInstanceList = std::vector<std::unique_ptr<BedInstance>>;

    [[nodiscard]] BedInstanceList& bed_instances()
    {
        return m_bed_instances;
    }

    [[nodiscard]] const BedInstanceList& bed_instances() const
    {
        return m_bed_instances;
    }

    BedInstance& add_bed_instance();
    BedInstanceList::const_iterator remove_bed_instance_by_id(size_t id);

    const BedInstance& find_bed_instance(size_t id) const
    {
        return *DEBUG_ASSERT_VAL(find_by_id(m_bed_instances, id));
    }

    BedInstance& find_bed_instance(size_t id)
    {
        return *DEBUG_ASSERT_VAL(find_by_id(m_bed_instances, id));
    }

private:
    Preset::SelectedPreset m_preset{};
    Domain::ProjectSettings m_project_settings;
    VirtualExtruders m_virtual_extruders;

    const Bed* m_bed{nullptr};
    BedInstanceList m_bed_instances;
};

} // namespace Slic3r::Domain
