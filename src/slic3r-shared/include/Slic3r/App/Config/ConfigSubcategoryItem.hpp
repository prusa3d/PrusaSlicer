#pragma once

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include <Slic3r/Biz/ConfigItemContext.hpp>

#include "Slic3r/App/Config/ConfigRowItems.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::Biz {
class ConfigBoxInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigSubcategoryItem :
    public Biz::DataObserver<Biz::ConfigItemContext>,
    public Yoga::Rectangle,
    public IConfigNavigable
{
    using ConfigRowListViewFactory = Yoga::ViewFactory<
        ConfigRowItems,
        Biz::ConfigItemContext,
        Biz::IConfigBoxSetter&,
        Biz::ConfigBoxInteractor&,
        size_t>;
    using ConfigRowListView =
        Yoga::ListView<ConfigRowItems, Biz::ConfigItemContext, ConfigRowListViewFactory>;

public:
    ConfigSubcategoryItem(
        size_t index,
        const Biz::ConfigItemContext& data,
        Biz::IConfigBoxSetter& cbi_container,
        Biz::ConfigBoxInteractor& cbi,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

private:
    void on_data_update() override;

    void on_index_update() override;

private:
    Biz::ConfigBoxInteractor& m_cbi;
    Biz::IConfigBoxSetter& m_cbi_container;
    size_t m_cbi_index{0};

    ConfigRowListView* m_rows_list_view{nullptr};
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::ConfigItemContext>> m_rows_filter_list;
    Yoga::Text* m_label{nullptr};
    Domain::ConfigItemDef::OptionGroup m_option_group{Domain::ConfigItemDef::OptionGroup::Unknown};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
