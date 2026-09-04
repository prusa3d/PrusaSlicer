#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

namespace Slic3r {

inline std::vector<Domain::PartialConfigPtr> join(
    const Domain::PartialObjectConfigFDMPtr& object_settings,
    const std::vector<Domain::PartialVolumeConfigFDMPtr>& volume_settings
) {
    std::vector<Domain::PartialConfigPtr> result;
    result.push_back(object_settings);
    result.insert(result.end(), volume_settings.begin(), volume_settings.end());
    return result;
}

class PrintRegionConfigView : public Domain::ConfigView
{
public:
    PrintRegionConfigView(
        const Domain::FullConfigFDMPtr& full_config,
        const Domain::PartialObjectConfigFDMPtr& object_settings,
        const std::vector<Domain::PartialVolumeConfigFDMPtr>& volume_settings
    ) :
        ConfigView{full_config, join(object_settings, volume_settings)},
        m_virtual_extruders{full_config->virtual_extruders()}
    {}

    PrintRegionConfigView():
        ConfigView{std::make_shared<const Domain::FullConfigFDM>(Domain::FullConfigFDM::defaults()), {}}
    {}

    void add_override(const Domain::PartialVolumeConfigFDMPtr& override) {
        m_partial_configs.push_back(override);
    }

    [[nodiscard]] const Domain::VirtualExtruders& virtual_extruders() const
    {
        return m_virtual_extruders;
    }

    /** @brief
     * Virtual extruder id resolved from perimeter_extruder, std::nullopt when physical.
     */
    [[nodiscard]] std::optional<unsigned int> source_virtual_extruder() const;

private:
    Domain::VirtualExtruders m_virtual_extruders;
};

class PrintObjectConfigView : public Domain::ConfigView
{
public:
    PrintObjectConfigView(
        const Domain::FullConfigFDMPtr& full_config,
        const Domain::PartialObjectConfigFDMPtr& object_settings
    ):
        ConfigView{full_config, {object_settings}}, m_object_settings{object_settings}
    {
        finalize();
    }

    const Domain::PartialObjectConfigFDMPtr& object_settings() const {
        return m_object_settings;
    }

private:
    Domain::PartialObjectConfigFDMPtr m_object_settings;
};

class PrintConfigView : public Domain::ConfigView
{
public:
    PrintConfigView(
        const Domain::FullConfigFDMPtr& full_config
    ):
        ConfigView{full_config, {}}
    {
        finalize();
    }

    PrintConfigView():
        ConfigView{std::make_shared<const Domain::FullConfigFDM>(Domain::FullConfigFDM::defaults()), {}}
    {
        finalize();
    }
};

class SLAPrintConfigView : public Domain::ConfigView
{
public:
    SLAPrintConfigView(
        const Domain::FullConfigSLAPtr& full_config
    ):
        ConfigView{full_config, {}}
    {
        finalize();
    }

    SLAPrintConfigView():
        ConfigView{std::make_shared<const Domain::FullConfigSLA>(Domain::FullConfigSLA::defaults()), {}}
    {
        finalize();
    }
};

class SLAPrintObjectConfigView : public Domain::ConfigView
{
public:
    SLAPrintObjectConfigView(
        const Domain::FullConfigSLAPtr& full_config,
        const Domain::PartialObjectConfigSLAPtr& object_settings
    ):
        ConfigView{full_config, {object_settings}}
    {
        finalize();
    }
};

} // namespace Slic3r
