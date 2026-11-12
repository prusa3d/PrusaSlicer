#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/OverrideItemRow.hpp"

namespace Slic3r::Biz {
class ObjectSettingsInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class OverrideOptionGroup : public Biz::DataObserver<Biz::OverrideItem>, public Yoga::Item
{
public:
    using SelectCategoryFn = std::function<void(Domain::ConfigItemDef::Category category)>;

    explicit OverrideOptionGroup(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::ProjectInteractor& project_interactor,
        SelectCategoryFn open_dialog_for_category
    );

protected:
    void on_data_update() override;

private:
    using OverrideConfigFilter = Biz::ObservableListSortFilter<Biz::OverrideItem>;
    using OverrideConfigListViewFactory =
        Yoga::ViewFactory<OverrideItemRow, Biz::OverrideItem, Biz::Preset::PresetInteractor&, bool>;
    using OverrideConfigListView =
        Yoga::ListView<OverrideItemRow, Biz::OverrideItem, OverrideConfigListViewFactory>;

    Biz::ProjectInteractor& m_project_interactor;

    Yoga::Text* m_text_group_name{nullptr};
    Yoga::LayoutButton* m_remove_all_btn{nullptr};

    Biz::UnsharedPointer<OverrideConfigFilter> m_override_config_filter;
    OverrideConfigListView* m_override_config_list_view{nullptr};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};

    SelectCategoryFn m_open_dialog_for_category{nullptr};
};

} // namespace Slic3r::App
