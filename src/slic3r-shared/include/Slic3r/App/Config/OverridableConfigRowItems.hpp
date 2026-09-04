#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Config/OverridableConfigRowItem.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
class OverridableConfigBoxInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class OverridableConfigRowItems :
    public Biz::DataObserver<Biz::OverrideItem>,
    public Yoga::Item,
    public IConfigNavigable
{
    using ConfigRowListViewFactory = Yoga::ViewFactory<
        OverridableConfigRowItem,
        Biz::OverrideItem,
        Biz::IConfigBoxSetter&,
        Biz::OverridableConfigBoxInteractor&,
        size_t>;
    using ConfigRowListView =
        Yoga::ListView<OverridableConfigRowItem, Biz::OverrideItem, ConfigRowListViewFactory>;

public:
    OverridableConfigRowItems(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::IConfigBoxSetter& cbi_container,
        Biz::OverridableConfigBoxInteractor& cbi,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

private:
    void on_data_update() override;

private:
    enum class InitializedType
    {
        None,
        Single,
        Multiple
    };
    InitializedType m_initialized_type{InitializedType::None};

    Biz::IConfigBoxSetter& m_cbi_container;
    Biz::OverridableConfigBoxInteractor& m_cbi;
    size_t m_cbi_index{0};

    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::OverrideItem>> m_row_items_filter;
    ConfigRowListView* m_row_group_list_view{nullptr};
    OverridableConfigRowItem* m_single_item{nullptr};
    Yoga::Text* m_label{nullptr};

    Domain::ConfigItemDef::OptionGroup m_option_group{Domain::ConfigItemDef::OptionGroup::Unknown};
    std::string m_row_group;
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
