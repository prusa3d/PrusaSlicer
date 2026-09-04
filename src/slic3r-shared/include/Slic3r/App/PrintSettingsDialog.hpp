#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/ObservableListTransformer.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include "Slic3r/App/Config/PrintToolSubcategoryListView.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/PageEntryButton.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"
#include "Slic3r/App/PrintMetadataSettings.hpp"
#include "Slic3r/App/DirtyCategoryList.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class PrintToolConfigObservableList;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Yoga {
class StackLayout;
class Menu;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;

class PrintSettingsDialog :
    public Yoga::Dialog,
    public IConfigNavigable,
    public Biz::Preset::IPresetChangedListener
{
public:
    explicit PrintSettingsDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

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
    void on_preset_bundles_loaded() override;

protected:
    using PageListView = Yoga::ListView<
        PageEntryButton,
        PageEntry,
        Yoga::ViewFactory<PageEntryButton, PageEntry, PageEntryButton::FnIndexClicked>>;

    using ToolPrintCategorizer =
        Biz::ObservableListSortFilter<Biz::PrintToolItem, Domain::ConfigItemDef::Category>;

    using DirtyToolPrintCategorizer = DirtyCategoryList<Biz::PrintToolItem>;

    using ToolPrintMenuTransformer = Biz::ObservableListTransformer<Biz::PrintToolItem, PageEntry>;
    using ExtruderMenuTransformer =
        Biz::ObservableListTransformer<Biz::Preset::ToolConfigItemObservableList, PageEntry>;

    using ToolPrintCategoryFactory = Yoga::ViewFactory<
        PrintToolSubcategoryListView,
        Biz::PrintToolItem,
        Biz::PrintToolConfigBoxInteractor&,
        Biz::IConfigBoxSetter&,
        Biz::ProjectInteractor&>;
    using ToolPrintCategoryListView = Yoga::ListView<
        PrintToolSubcategoryListView,
        Biz::PrintToolItem,
        ToolPrintCategoryFactory,
        Yoga::StackLayout>;

    using PrintMetadataListView = Yoga::ListView<
        PrintMetadataSettings,
        Biz::Preset::ToolConfigItemObservableList,
        Yoga::ViewFactory<
            PrintMetadataSettings,
            Biz::Preset::ToolConfigItemObservableList,
            Biz::Preset::PresetInteractor&>,
        Yoga::StackLayout>;

    void close_action() override;

    void select_page_entry(size_t index, bool category);

private:
    void on_about_to_close() override;

    void update_dirty_state();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::StackLayout* m_content_stack_layout{nullptr};
    ToolPrintCategoryListView* m_category_stack_list_view{nullptr};
    PrintMetadataListView* m_metadata_stack_list_view{nullptr};

    Yoga::Item* m_footer{nullptr};
    Yoga::LayoutButton* m_revert_button{nullptr};
    Yoga::LayoutButton* m_save_button{nullptr};
    PageListView* m_category_page_list_view{nullptr};
    PageListView* m_extruder_page_list_view{nullptr};

    Yoga::Text* m_bed_name{nullptr};

    Biz::UnsharedPointer<ToolPrintCategorizer> m_tool_print_categorizer;
    Biz::UnsharedPointer<DirtyToolPrintCategorizer> m_dirty_tool_print_categorizer;
    Biz::UnsharedPointer<ToolPrintMenuTransformer> m_tool_print_transformer;
    Biz::UnsharedPointer<ExtruderMenuTransformer> m_extruder_menu_transformer;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PrintSettingsDialog>
        m_preset_changed_listener_scope;
};

} // namespace Slic3r::App
