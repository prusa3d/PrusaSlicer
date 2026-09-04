#pragma once

#include "Slic3r/App/PresetUpdater/PresetUpdaterController.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterVendorRow.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::App {

namespace Yoga {
class Icon;
class LayoutButton;
class Separator;
class StackLayout;
class Text;
class ToggleButton;
} // namespace Yoga

/**
 * @brief One preset source: whether it is used, what is known about it, and its vendors.
 *
 * Built once by the ListView and updated in place from on_data_update - children are never rebuilt.
 * Whether the vendor list is unrolled is view state and deliberately not part of the model, so the
 * two dialog instances can be unrolled independently.
 */
class PresetUpdaterSourceRow :
    public Biz::DataObserver<PresetUpdater::SourceRowState>,
    public Yoga::Rectangle
{
public:
    using VendorFactory = Yoga::ViewFactory<
        PresetUpdaterVendorRow,
        PresetUpdater::VendorRowState,
        PresetUpdater::PresetUpdaterController&>;
    using VendorListView =
        Yoga::ListView<PresetUpdaterVendorRow, PresetUpdater::VendorRowState, VendorFactory>;

    PresetUpdaterSourceRow(
        size_t index,
        const PresetUpdater::SourceRowState& data,
        PresetUpdater::PresetUpdaterController& controller
    );

protected:
    void on_data_update() override;

private:

    /// @note Must match the order the pages are added to m_action_slot.
    enum ActionPage : size_t
    {
        ActionNone       = 0,
        ActionWaiting    = 1,
        ActionChecking   = 2,
        ActionInstalling = 3,
        ActionApplyAll   = 4
    };

    void build_header();
    void build_status_cell(Yoga::Item* header);
    void set_expanded(bool expanded);

    PresetUpdater::PresetUpdaterController& m_controller;

    bool m_expanded{false};

    bool m_updating{false};

    Yoga::ToggleButton* m_tick{nullptr};
    Yoga::LayoutButton* m_arrow{nullptr};
    Yoga::Text* m_name{nullptr};
    Yoga::LayoutButton* m_info_badge{nullptr};
    Yoga::Text* m_description{nullptr};

    Yoga::Item* m_icon_slot{nullptr};
    Yoga::LayoutButton* m_reveal_button{nullptr};
    Yoga::LayoutButton* m_remove_button{nullptr};

    Yoga::StackLayout* m_action_slot{nullptr};
    Yoga::LayoutButton* m_update_all_button{nullptr};

    Yoga::Text* m_summary_text{nullptr};
    Yoga::Icon* m_summary_icon{nullptr};

    Yoga::Separator* m_vendor_separator{nullptr};

    Yoga::Item* m_vendor_container{nullptr};
    VendorListView* m_vendor_list_view{nullptr};
};

} // namespace Slic3r::App
