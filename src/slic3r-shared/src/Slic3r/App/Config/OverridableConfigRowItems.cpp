#include "Slic3r/App/Config/OverridableConfigRowItems.hpp"

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverridableConfigRowItems::OverridableConfigRowItems(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::IConfigBoxSetter& cbi_container,
    Biz::OverridableConfigBoxInteractor& cbi,
    size_t cbi_index
) :
    DataObserver<Biz::OverrideItem>(index, data),
    m_cbi_container(cbi_container),
    m_cbi(cbi),
    m_cbi_index(cbi_index)
{
    on_data_update();
}

void OverridableConfigRowItems::navigate_to_item(const Domain::ConfigItem* config_item)
{
    if (m_single_item) {
        m_single_item->navigate_to_item(config_item);
    } else {
        for (size_t column_index = 0; column_index < m_row_items_filter->size(); ++column_index) {
            m_row_group_list_view->item_at(column_index)->navigate_to_item(config_item);
        }
    }
}

void OverridableConfigRowItems::clear_navigation()
{
    if (m_single_item) {
        m_single_item->clear_navigation();
    } else {
        for (size_t column_index = 0; column_index < m_row_items_filter->size(); ++column_index) {
            m_row_group_list_view->item_at(column_index)->clear_navigation();
        }
    }
}

void OverridableConfigRowItems::on_data_update()
{
    const std::string row_group = m_state->config_item->def().row_group;
    const Domain::ConfigItemDef::OptionGroup option_group =
        m_state->config_item->def().option_group;
    const Domain::ConfigItemDef::Category category = m_state->config_item->def().category;
    if (m_row_group != row_group || m_initialized_type == InitializedType::None) {
        m_row_group = row_group;
        if (row_group.empty()) {
            if (m_initialized_type == InitializedType::Multiple) {
                // Cleanup if we used to have row_group
                remove(m_label);
                m_label = nullptr;
                remove(m_row_group_list_view);
                m_row_group_list_view = nullptr;
                m_row_items_filter.reset();
            }

            if (m_initialized_type != InitializedType::Single) {
                m_initialized_type = InitializedType::Single;
                m_single_item      = emplace_back<OverridableConfigRowItem>(
                    m_index,
                    *m_state,
                    m_cbi_container,
                    m_cbi,
                    m_cbi_index
                );
                m_single_item->set_flex_grow(1);
            }

        } else {
            if (m_initialized_type == InitializedType::Single) {
                // Cleanup if we used to have single item
                remove(m_single_item);
                m_single_item = nullptr;
            }

            if (m_initialized_type != InitializedType::Multiple) {
                m_initialized_type = InitializedType::Multiple;
                m_label            = emplace_back<Text>(m_state->config_item->def().row_group);
                m_label->set_width(150);
                m_label->set_self_align(YGAlignCenter);

                m_row_items_filter =
                    std::make_shared<Biz::ObservableListSortFilter<Biz::OverrideItem>>();

                m_category  = category;
                m_row_group = row_group;

                m_row_items_filter->set_filter_fn(
                    [this](const Biz::OverrideItem& item) -> bool
                    {
                        return item.config_item->def().option_group == m_option_group
                            && item.config_item->def().category == m_category
                            && item.config_item->def().row_group == m_row_group;
                    }
                );
                m_row_items_filter->set_source_model(m_cbi.config_box_overridable_list());

                m_row_group_list_view = emplace_back<ConfigRowListView>(
                    ConfigRowListViewFactory{m_cbi_container, m_cbi, m_cbi_index}
                );
                m_row_group_list_view->set_align_items(YGAlignCenter);
                m_row_group_list_view->set_gap(5);
                m_row_group_list_view->set_flex_grow(1);
                m_row_group_list_view->set_source_list(m_row_items_filter.get());
            } else {
                m_row_items_filter->invalidate();
            }
        }
    }

    if (m_initialized_type == InitializedType::Multiple) {
        // just check if option_group, category or row_group is updated
        if (m_option_group != option_group || m_category != category) {
            m_option_group = option_group;
            m_category     = category;
            m_row_items_filter->invalidate();
        }
    }

    if (m_single_item) {
        m_single_item->set_state(*m_state);
    }
}

} // namespace Slic3r::App
