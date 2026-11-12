#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"

#include "Slic3r/App/Config/PrintToolRowItem.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::Biz {
class PrintToolConfigBoxInteractor;
class IConfigBoxSetter;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class PrintToolSubcategoryItem :
    public Biz::DataObserver<Biz::PrintToolItem>,
    public Yoga::Rectangle,
    public IConfigNavigable
{
    using PrintToolRowListViewFactory = Yoga::ViewFactory<
        PrintToolRowItem,
        Biz::PrintToolItem,
        Biz::PrintToolConfigBoxInteractor&,
        Biz::IConfigBoxSetter&,
        Biz::ProjectInteractor&>;
    using PrintToolRowListView =
        Yoga::ListView<PrintToolRowItem, Biz::PrintToolItem, PrintToolRowListViewFactory>;

public:
    PrintToolSubcategoryItem(
        size_t index,
        const Biz::PrintToolItem& data,
        Biz::PrintToolConfigBoxInteractor& cbi,
        Biz::IConfigBoxSetter& cbi_setter,
        Biz::ProjectInteractor& project_interactor
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

private:
    void on_data_update() override;

    void on_index_update() override;

private:
    Biz::PrintToolConfigBoxInteractor& m_cbi;
    Biz::IConfigBoxSetter& m_cbi_setter;

    PrintToolRowListView* m_rows_list_view{nullptr};
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::PrintToolItem>> m_rows_filter_list;
    Yoga::Text* m_label{nullptr};
    Domain::ConfigItemDef::OptionGroup m_option_group{Domain::ConfigItemDef::OptionGroup::Unknown};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
