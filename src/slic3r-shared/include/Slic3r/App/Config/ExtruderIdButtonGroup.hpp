#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class TransparentLabeledButton : public Yoga::LayoutButton
{
public:
    explicit TransparentLabeledButton(const std::string& label);

protected:
    void checked_updated_internal() override;
    void hovered_updated_internal() override;

    void update_label_color();
    ImColor m_color;
};

/**
 * @brief A single checkable button representing one physical tool/extruder slot,
 * labeled with its 1-based id. Self-registers with (and unregisters from) the shared
 * button group as the owning Yoga::ListView creates/destroys it.
 */
class ExtruderIdButton :
    public Biz::DataObserver<Biz::Preset::PresetItemObservableList>,
    public TransparentLabeledButton,
    public Biz::IColorsChangedListener
{
public:
    ExtruderIdButton(
        size_t index,
        const Biz::Preset::PresetItemObservableList& data,
        std::weak_ptr<Yoga::ButtonGroup> button_group,
        Biz::ProjectInteractor& project_interactor
    );
    ~ExtruderIdButton() override;

    int extruder_id() const
    {
        return static_cast<int>(m_index) + 1;
    }

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

private:

    void update_color_from_slots(const std::vector<Domain::ColorRGB>& colors);

    std::weak_ptr<Yoga::ButtonGroup> m_button_group;
    Biz::ProjectInteractor& m_project_interactor;

    Biz::ListenerScope<Biz::IColorsChangedListener, Biz::ProjectSettingsInteractor, ExtruderIdButton>
        m_colors_changed_listener_scope;
};

/**
 * @brief A row of checkable buttons: an "auto"/id-0 button plus one per tool/extruder
 * slot. The per-slot buttons are kept in sync with
 * Biz::Preset::PresetInteractor::tool_items() automatically (via the underlying
 * Yoga::ListView), so this always respects the current tool count, regardless of
 * which specific preset event changed it.
 */
class ExtruderIdButtonGroup : public Yoga::Item
{
public:
    ExtruderIdButtonGroup(Biz::ProjectInteractor& project_interactor, bool auto_enabled);

    /**
     * @brief Checks the button for the given extruder id (0 = auto), if present.
     * @note Does not fire the group's `action` callback (only an actual user click does).
     */
    void set_selected_extruder(int extruder_id);

    /**
     * @return The id (0 = auto) of the currently checked button, or std::nullptr if none is checked.
     */
    std::optional<int> selected_extruder_id() const;

    /**
     * @return True if the extruder id passed to the last set_selected_extruder() call exceeds
     * the current tool count (no button is checked in that case).
     */
    bool is_selected_extruder_out_of_range() const;

    Yoga::ButtonGroup::Callbacks& callbacks();

private:
    using ExtruderButtonListViewFactory = Yoga::ViewFactory<
        ExtruderIdButton,
        Biz::Preset::PresetItemObservableList,
        std::weak_ptr<Yoga::ButtonGroup>,
        Biz::ProjectInteractor&>;
    using ExtruderButtonListView = Yoga::ListView<
        ExtruderIdButton,
        Biz::Preset::PresetItemObservableList,
        ExtruderButtonListViewFactory>;

    bool m_auto_enabled{false};

    std::shared_ptr<Yoga::ButtonGroup> m_button_group;
    TransparentLabeledButton* m_auto_button{nullptr};
    ExtruderButtonListView* m_list_view{nullptr};
    bool m_selected_extruder_out_of_range{false};
};

} // namespace Slic3r::App
