#include "Slic3r/App/OverrideOptionGroup.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

static LayoutButton* add_button(Item* parent, Render::Icon icon, const std::string& tooltip)
{
    LayoutButton* button = parent->emplace_back<LayoutButton>(std::string(), icon, tooltip);
    button->set_background_color(Platform::Color::ButtonTransparent);
    button->set_width(22);
    button->set_height(22);
    button->set_content_padding(Paddings(2));
    return button;
}

OverrideOptionGroup::OverrideOptionGroup(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::ProjectInteractor& project_interactor,
    SelectCategoryFn open_dialog_for_category
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_project_interactor(project_interactor),
    m_open_dialog_for_category(open_dialog_for_category)
{
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);
    set_gap(5);

    Item* label_row = emplace_back<Item>();
    label_row->set_padding({0, 10, 0, 5});

    m_text_group_name = label_row->emplace_back<Text>(std::string());
    m_text_group_name->set_self_align(YGAlignCenter);
    m_text_group_name->set_font_type(Render::ImguiFontType::Bold);
    m_text_group_name->set_flex_grow(1);

    m_remove_all_btn =
        add_button(label_row, Render::Icon::Minus, Biz::_u8L("Remove all overriden parameters"));
    m_remove_all_btn->callbacks().action = [this]
    {
        for (int index = m_override_config_filter->size() - 1; index >= 0; --index) {
            m_project_interactor.preset_interactor().set_item_override(
                *m_override_config_filter->at(index).config_item,
                false
            );
        }
    };
    m_remove_all_btn->set_visible(false);

    LayoutButton* add_overrides_btn =
        add_button(label_row, Render::Icon::Plus, Biz::_u8L("Add override parameter"));
    add_overrides_btn->callbacks().action = [this]
    {
        if (m_open_dialog_for_category) {
            m_open_dialog_for_category(m_category);
        }
    };

    m_override_config_filter = std::make_shared<OverrideConfigFilter>();
    m_override_config_filter->set_filter_fn(
        [this](const Biz::OverrideItem& item) -> bool
        {
            return item.is_override()
                && item.config_item->def().category == m_category
                && item.overriden.value();
        }
    );
    m_override_config_filter->set_sort_fn(
        [](const Biz::OverrideItem& lhs, const Biz::OverrideItem& rhs)
        {
            return lhs.config_item->def().option_group < rhs.config_item->def().option_group
                || (lhs.config_item->def().category == rhs.config_item->def().category
                    && lhs.config_item->def().option_group == rhs.config_item->def().option_group
                    && lhs.config_item->def().order < rhs.config_item->def().order);
        }
    );

    m_override_config_list_view = emplace_back<OverrideConfigListView>(
        OverrideConfigListViewFactory{m_project_interactor.preset_interactor(), true}
    );
    m_override_config_list_view->set_orientation(Orientation::Vertical);
    m_override_config_list_view->set_gap(10);
    m_override_config_list_view->set_padding(0);
    m_override_config_list_view->set_source_list(m_override_config_filter.get());

    m_override_config_filter->set_source_model(m_project_interactor.preset_interactor()
                                                   .object_settings_interactor()
                                                   .object_observable_list());

    emplace_back<Separator>(Orientation::Horizontal)->set_margin(Margins(-20, 5, -20, 0));

    on_data_update();
}

void OverrideOptionGroup::on_data_update()
{
    const Domain::ConfigItemDef::Category category = m_state->config_item->def().category;

    if (m_category != category) {
        m_text_group_name->set_text(
            Domain::ConfigItemDef::translate_category(
                category,
                m_project_interactor.selected_config_container().print_technology()
            )
        );

        m_category = category;
        m_override_config_filter->invalidate();
    }
}

} // namespace Slic3r::App
