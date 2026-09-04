#pragma once

#include "Slic3r/App/Config/ObservableOverrideCategorizer.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/OverrideCategoryButton.hpp"
#include "Slic3r/App/OverridablePreviewSubcategoryItem.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class StackLayout;
class LayoutButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class OverrideSettingsDialog : public Yoga::Dialog
{
public:
    using SelectCategoryFn = std::function<void(Domain::ConfigItemDef::Category category)>;

    explicit OverrideSettingsDialog(Biz::ProjectInteractor& project_interactor);

    void open_for_category(Domain::ConfigItemDef::Category category);

private:
    void on_about_to_show() override;

private:
    using OverrideCategoryFactory = Yoga::ViewFactory<
        OverrideCategoryButton,
        Biz::OverrideItem,
        Biz::ProjectInteractor&,
        SelectCategoryFn&>;
    using OverrideCategoryListView =
        Yoga::ListView<OverrideCategoryButton, Biz::OverrideItem, OverrideCategoryFactory>;

    using OverrideConfigListViewFactory = Yoga::ViewFactory<
        OverridablePreviewSubcategoryItem,
        Biz::OverrideItem,
        Biz::Preset::PresetInteractor&>;
    using OverrideConfigListView = Yoga::ListView<
        OverridablePreviewSubcategoryItem,
        Biz::OverrideItem,
        OverrideConfigListViewFactory,
        Yoga::ScrollArea>;

    using OverrideConfigFilter =
        Biz::ObservableListSortFilter<Biz::OverrideItem, Domain::ConfigItemDef::OptionGroup>;

    Biz::ProjectInteractor& m_project_interactor;

    Biz::UnsharedPointer<ObservableOverrideCategorizer> m_categorizer;
    Biz::UnsharedPointer<OverrideConfigFilter> m_category_filter;
    OverrideCategoryListView* m_override_category_list_view{nullptr};
    Yoga::StackLayout* m_stack_layout{nullptr};
    Yoga::Text* m_options_category_text{nullptr};
    OverrideConfigListView* m_override_config_list_view{nullptr};
    Domain::ConfigItemDef::Category m_current_category{Domain::ConfigItemDef::Category::Unknown};

    SelectCategoryFn m_select_category{nullptr};
};

} // namespace Slic3r::App
