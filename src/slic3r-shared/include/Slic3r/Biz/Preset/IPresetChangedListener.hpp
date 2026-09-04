#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz::Preset {

enum class PresetItemType
{
    PrinterPreset,
    PrintPreset,
    ToolPrintPreset,
    MaterialPreset
};

enum class HwItemType
{
    ToolItem,
    SheetItem
};

class IPresetChangedListener
{
public:
    virtual ~IPresetChangedListener() = default;

    virtual void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        PresetItemType type
    )
    {}

    virtual void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    )
    {}

    virtual void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    )
    {}

    virtual void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        HwItemType type
    )
    {}

    virtual void on_preset_bundles_loaded()
    {}
};

} // namespace Slic3r::Biz::Preset
