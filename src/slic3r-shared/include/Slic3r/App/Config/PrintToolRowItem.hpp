#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/IConfigNavigable.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Config/ToolRowControl.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

namespace Slic3r::Biz {
class PrintToolConfigBoxInteractor;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigRowItem;
class PrintToolRowButton;
class FavoriteButton;
class ExplanationContainer;
class PrintToolMenu;
class ToolDropArea;

struct PrintToolRowItemDisplayOptions
{
    bool show_favorites{true};
    bool show_favorites_only_on_hover{false};
    bool show_explanation{true};
};

class PrintToolRowItem :
    public Biz::DataObserver<Biz::PrintToolItem>,
    public IConfigNavigable,
    public Yoga::Rectangle,
    public Biz::IObservableList<ToolRowOverrideGroup>,
    public Biz::IColorsChangedListener,
    public Biz::Preset::IPresetChangedListener
{
public:
    PrintToolRowItem(
        size_t index,
        const Biz::PrintToolItem& data,
        Biz::PrintToolConfigBoxInteractor& cbi,
        Biz::IConfigBoxSetter& cb_setter,
        Biz::ProjectInteractor& project_interactor,
        const PrintToolRowItemDisplayOptions& options = {}
    );
    ~PrintToolRowItem() override;

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

    const ToolRowOverrideGroup& at(size_t index) const override;
    size_t size() const override;

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

    void on_view_will_be_reset() override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

protected:
    using ToolRowOverridePtr = std::unique_ptr<ToolRowOverride>;

    void on_data_update() override;

    void clear();
    void initialize();
    void update_explanation();

    void exclude_tool(size_t tool_index);
    void move_tool(size_t tool_index, size_t group_index);

    void presort_overrides();
    void sort_extruders_in_groups();
    void update_group_size();

    ToolRowOverrideGroup* find_group(const ToolRowOverridePtr& override);

private:
    using ToolRowFactory = Yoga::ViewFactory<
        ToolRowControl,
        ToolRowOverrideGroup,
        Biz::IConfigBoxSetter&,
        ToolRowControl::ExtruderClickedFn,
        ToolRowControl::ExtruderDroppedFn,
        bool>;
    using ToolRowListView = Yoga::ListView<ToolRowControl, ToolRowOverrideGroup, ToolRowFactory>;

    enum class InitializedType
    {
        None,
        PrintOnly,
        PrintTool
    };
    InitializedType m_initialized_type{InitializedType::None};

    Biz::PrintToolConfigBoxInteractor& m_cbi;
    Biz::IConfigBoxSetter& m_cb_setter;
    Biz::ProjectInteractor& m_project_interactor;

    const PrintToolRowItemDisplayOptions m_options;

    Biz::
        ListenerScope<Biz::IColorsChangedListener, Biz::ProjectSettingsInteractor, PrintToolRowItem>
            m_colors_changed_listener_scope;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PrintToolRowItem>
        m_preset_changed_listener_scope;

    Yoga::Item* m_column{nullptr};
    Yoga::Item* m_header{nullptr};
    PrintToolRowButton* m_main_button{nullptr};
    Yoga::Rectangle* m_content{nullptr};
    ToolRowListView* m_tool_list_view{nullptr};
    FavoriteButton* m_favorite_button{nullptr};
    ExplanationContainer* m_explanation_container{nullptr};
    ToolDropArea* m_tool_drop_area{nullptr};

    std::vector<ToolRowOverrideGroup> m_tool_overrides;
    std::vector<ToolRowOverridePtr> m_overrides;

    ConfigRowItem* m_config_row_item{nullptr};
    bool m_small{false};
};

} // namespace Slic3r::App
