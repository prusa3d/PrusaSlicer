#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ObservableListTransformer.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"

#include "Slic3r/App/Yoga/AbstractSettingsDialog.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"
#include "Slic3r/App/DirtyCategoryList.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class OverridableConfigBoxInteractor;
class OverridableCBIObservableList;

namespace Preset {
class PresetInteractor;
} // namespace Preset
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class MaterialSelectionDialog;
class CurrentPresetLabel;

class MaterialSettingsDialog :
    public Yoga::AbstractSettingsDialog,
    public IConfigNavigable,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IListObserver<Biz::OverridableConfigBoxInteractor>
{
public:
    explicit MaterialSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        MaterialSelectionDialog* material_selection_dialog
    );
    ~MaterialSettingsDialog();

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

    void on_reset() override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

protected:
    void close_action() override;

    void remove_tab(size_t index) override;

    void on_tab_selected(int current_index) override;

private:
    void on_about_to_close() override;
    void update_ui_state(const Domain::ConfigItem* changed_item = nullptr);

private:
    using OverridableCategoryPageTransformer =
        Biz::ObservableListTransformer<Biz::OverrideItem, PageEntry>;

    using Categorizer =
        Biz::ObservableListSortFilter<Biz::OverrideItem, Domain::ConfigItemDef::Category>;

    using DirtyCategorizer = DirtyCategoryList<Biz::OverrideItem>;

    struct ConfigTab
    {
        ConfigTab(
            Biz::OverridableConfigBoxInteractor& cbi,
            Tab& tab,
            Biz::ProjectInteractor& project_interactor,
            size_t cbi_index
        );

        void init();

        void navigate_to_item(const Domain::ConfigItem* config_item);
        void clear_navigation();

        Biz::OverridableConfigBoxInteractor& cbi;
        Tab& tab;
        Biz::ProjectInteractor& project_interactor;
        size_t cbi_index{0};
        Biz::UnsharedPointer<Categorizer> categorizer;
        Biz::UnsharedPointer<DirtyCategorizer> dirty_categorizer;
        Biz::UnsharedPointer<OverridableCategoryPageTransformer> category_page_transformer;
    };

    using ConfigTabPtr = std::unique_ptr<ConfigTab>;
    using ConfigTabs   = std::vector<ConfigTabPtr>;

    ConfigTabs m_config_tabs;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    MaterialSelectionDialog* m_material_selection_dialog{nullptr};
    Biz::OverridableCBIObservableList& m_material_cbi_list;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        MaterialSettingsDialog>
        m_preset_changed_listener_scope;

    CurrentPresetLabel* m_current_preset_label{nullptr};

    Yoga::LayoutButton* m_revert_button{nullptr};
};

} // namespace Slic3r::App
