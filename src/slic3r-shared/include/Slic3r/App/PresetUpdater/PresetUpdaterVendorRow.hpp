#pragma once

#include "Slic3r/App/PresetUpdater/PresetUpdaterController.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::App {

namespace Yoga {
class Icon;
class LayoutButton;
class StackLayout;
class Text;
} // namespace Yoga


namespace PresetUpdaterRowLayout {

constexpr Yoga::Unit column_gap{8, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit icon_size{16, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit icon_button_size{24, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit icon_slot_width{52, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit lead_slot_width{52, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit action_slot_width{110, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit status_width{150, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit name_width{140, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit change_width{160, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit source_row_height{40, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit vendor_row_height{44, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit button_height{30, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit button_padding{7, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit source_frame_padding_horizontal{8, Yoga::Unit::Type::FigmaPixel};
constexpr Yoga::Unit source_frame_padding_vertical{4, Yoga::Unit::Type::FigmaPixel};

/// Gives a text button the size the settings dialogs use for their footer buttons.
void apply_button_size(Yoga::LayoutButton* button);

} // namespace PresetUpdaterRowLayout

/**
 * @brief One vendor of one preset source: what would change, from which version to which, and the
 * button that performs it.
 *
 * Built once by the ListView and updated in place from on_data_update - children are never rebuilt.
 */
class PresetUpdaterVendorRow :
    public Biz::DataObserver<PresetUpdater::VendorRowState>,
    public Yoga::Item
{
public:
    PresetUpdaterVendorRow(
        size_t index,
        const PresetUpdater::VendorRowState& data,
        PresetUpdater::PresetUpdaterController& controller
    );

protected:
    void on_data_update() override;

private:
    /// @note Must match the order the pages are added to m_action.
    enum ActionPage : size_t
    {
        ActionIdle    = 0,
        ActionQueued  = 1,
        ActionRunning = 2,
        ActionDone    = 3,
        ActionFailed  = 4,
        ActionNone    = 5
    };

    void build_action_slot();

    PresetUpdater::PresetUpdaterController& m_controller;

    Yoga::Icon* m_state_icon{nullptr};
    Yoga::Text* m_name{nullptr};
    Yoga::Text* m_comment{nullptr};
    Yoga::Text* m_change{nullptr};
    Yoga::Text* m_skipped_text{nullptr};
    Yoga::Item* m_version_group{nullptr};
    Yoga::Text* m_current_version{nullptr};
    Yoga::Text* m_recommended_version{nullptr};

    Yoga::StackLayout* m_action{nullptr};
    Yoga::LayoutButton* m_action_button{nullptr};
    Yoga::LayoutButton* m_retry_button{nullptr};
    Yoga::Text* m_done_text{nullptr};
    Yoga::Text* m_failed_text{nullptr};

    Yoga::LayoutButton* m_changelog_button{nullptr};
};

} // namespace Slic3r::App
