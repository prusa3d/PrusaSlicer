#include "Slic3r/App/OverridablePreviewSubcategoryItem.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverridablePreviewSubcategoryItem::OverridablePreviewSubcategoryItem(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::Preset::PresetInteractor& preset_interactor
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_rows_filter_list(std::make_shared<Biz::ObservableListSortFilter<Biz::OverrideItem>>())
{
    m_rows_filter_list->set_filter_fn(
        [this](const Biz::OverrideItem& item) -> bool
        {
            return item.config_item->def().option_group == m_option_group
                && item.config_item->def().category == m_category;
        }
    );

    m_rows_filter_list->set_sort_fn(
        [](const Biz::OverrideItem& lhs, const Biz::OverrideItem& rhs)
        { return lhs.config_item->def().order < rhs.config_item->def().order; }
    );

    m_rows_filter_list->set_source_model(
        preset_interactor.object_settings_interactor().object_observable_list()
    );

    set_object_name("OverridablePriviewSubcategoryItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);
    set_gap(15);
    set_padding({0, 10, 0, 0});

    m_label = emplace_back<Text>(std::string{}, Render::ImguiFontType::Bold);

    m_rows_list_view =
        emplace_back<ConfigPreviewRowListView>(ConfigPreviewRowListViewFactory{preset_interactor});
    m_rows_list_view->set_source_list(m_rows_filter_list.get());
    m_rows_list_view->set_orientation(Orientation::Vertical);
    m_rows_list_view->set_gap(5);

    emplace_back<Separator>(Orientation::Horizontal);

    on_data_update();
}

void OverridablePreviewSubcategoryItem::on_data_update()
{
    const Domain::ConfigItem* config_item = m_state->config_item;

    m_label->set_text(
        Biz::_u8(Domain::ConfigItemDef::translate_option_group(config_item->def().option_group))
    );

    const Domain::ConfigItemDef::OptionGroup option_group = config_item->def().option_group;
    const Domain::ConfigItemDef::Category category        = config_item->def().category;

    if (option_group != m_option_group || m_category != category) {
        m_option_group = option_group;
        m_category     = category;
        m_rows_filter_list->invalidate();
    }
}
} // namespace Slic3r::App
