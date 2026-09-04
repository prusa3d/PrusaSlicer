#pragma once

#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Uuid.hpp"

namespace Slic3r::Domain {


BoxOrBoxesVector as_boxes(const ConfigPackFDM& config_pack);
MutBoxOrBoxesVector as_mut_boxes(ConfigPackFDM& config_pack);

class FullConfigFDM : public FullConfig
{
public:
    FullConfigFDM(
        const ConfigPackFDM& config_pack,
        const std::vector<unsigned>& extruder_candidates,
        const Preset::HwPrinterConfig& hw_config
    );

    const VirtualExtruders& virtual_extruders() const
    {
        return m_virtual_extruders;
    }

    static FullConfigFDM defaults();

private:
    VirtualExtruders m_virtual_extruders;
};

class PartialObjectConfigFDM : public PartialConfig {
public:
    PartialObjectConfigFDM(
        const ObjectSettings& object_settings,
        const Preset::HwPrinterConfig& hw_config
    );
};

class PartialVolumeConfigFDM : public PartialConfig {
public:
    PartialVolumeConfigFDM(
        const VolumeSettings& volume_settings,
        const Preset::HwPrinterConfig& hw_config
    );
};

using FullConfigFDMPtr = std::shared_ptr<const FullConfigFDM>;
using PartialObjectConfigFDMPtr = std::shared_ptr<const PartialObjectConfigFDM>;
using PartialVolumeConfigFDMPtr = std::shared_ptr<const PartialVolumeConfigFDM>;

} // namespace Slic3r::Domain
