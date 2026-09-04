#include "Slic3r/App/Plater/LayerRangeSettingsDialog.hpp"

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"

#include <map>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

using Slic3r::Biz::IConfigBoxSetter;
using Slic3r::Domain::ConfigBox;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::VolumeSettings;

namespace Slic3r::App::Plater {

LayerRangeSettingsDialog::LayerRangeSettingsDialog(IConfigBoxSetter* config_box_setter) :
    Dialog({_u8L("Settings")}, "LayerRangeSettingsDialog"),
    m_config_box_setter(config_box_setter)
{
    this->content()->set_width(380);
    this->content()->set_orientation(Orientation::Vertical);

    m_stack_layout = this->content()->emplace_back<StackLayout>();
    m_stack_layout->set_orientation(Orientation::Vertical);

    this->init_categories_page();
    this->init_settings_page();
}

void LayerRangeSettingsDialog::init_categories_page()
{
    const std::vector<ConfigItem> all_items = VolumeSettings{}.overrides.all_items();

    std::map<ConfigItemDef::Category, std::string> category_names;
    for (const ConfigItem& item : all_items) {
        const ConfigItemDef::Category category = item.def().category;
        if (category == ConfigItemDef::Category::Unknown
            || category == ConfigItemDef::Category::Hidden)
        {
            continue;
        }

        category_names.emplace(
            category,
            Biz::_u8(ConfigItemDef::translate_category(category, PrinterTechnology::FFF))
        );
    }

    ScrollArea* categories_scroll = m_stack_layout->emplace_back<ScrollArea>();
    categories_scroll->set_orientation(Orientation::Vertical);
    categories_scroll->set_gap(5);
    categories_scroll->set_max_height(350);

    for (const auto& [category, category_name] : category_names) {
        LayoutButton* button = categories_scroll->emplace_back<LayoutButton>(
            category_name,
            Render::Icon::ChevronRight
        );

        button->set_content_direction(YGDirectionRTL);
        button->set_content_justify_content(YGJustifyFlexEnd);
        button->set_expand_label(true);
        button->set_flex_shrink(0);
        button->callbacks().action = [this, category]() { this->select_category(category); };
    }
}

void LayerRangeSettingsDialog::init_settings_page()
{
    Item* options_page = m_stack_layout->emplace_back<Item>();
    options_page->set_orientation(Orientation::Vertical);
    options_page->set_gap(5);

    Item* back_row = options_page->emplace_back<Item>();
    back_row->set_gap(5);
    back_row->set_flex_shrink(0);

    LayoutButton* back_button = back_row->emplace_back<LayoutButton>("", Render::Icon::ChevronLeft);
    back_button->set_width(18);
    back_button->set_height(18);
    back_button->set_content_padding(Paddings(2));
    back_button->callbacks().action = [this]() { on_about_to_show(); };

    m_options_category_text = back_row->emplace_back<Text>(std::string());
    m_options_category_text->set_flex_grow(1);
    m_options_category_text->set_font_type(Render::ImguiFontType::Bold);

    options_page->emplace_back<Separator>(Orientation::Horizontal);

    m_settings_scroll = options_page->emplace_back<ScrollArea>();
    m_settings_scroll->set_orientation(Orientation::Vertical);
    m_settings_scroll->set_gap(5);
    m_settings_scroll->set_max_height(350);
}

void LayerRangeSettingsDialog::set_config_box(const ConfigBox* config_box)
{
    m_config_box = config_box;
}

void LayerRangeSettingsDialog::open_at_category(const ConfigItemDef::Category category)
{
    this->open();
    this->select_category(category);
}

void LayerRangeSettingsDialog::on_about_to_show()
{
    this->clear_settings();
    m_stack_layout->set_current_index(0);
}

void LayerRangeSettingsDialog::select_category(const ConfigItemDef::Category category)
{
    const std::string& category_name =
        Biz::_u8(ConfigItemDef::translate_category(category, PrinterTechnology::FFF));

    this->clear_settings();
    this->create_settings_page_for_category(category);
    m_options_category_text->set_text(category_name);
    m_stack_layout->set_current_index(1);
}

void LayerRangeSettingsDialog::create_settings_page_for_category(ConfigItemDef::Category category)
{
    ASSERT(m_config_box != nullptr || m_config_box_setter != nullptr);

    const std::vector<ConfigItem> all_items = VolumeSettings{}.overrides.all_items();

    for (const ConfigItem& item : all_items) {
        if (item.def().category != category) {
            continue;
        }

        const ConfigItem* config_item = m_config_box->overrides.find(item.def().name);
        if (config_item == nullptr) {
            continue;
        }

        Item* setting_row = m_settings_scroll->emplace_back<Item>();
        setting_row->set_gap(5);
        setting_row->set_flex_shrink(0);
        setting_row->set_object_name("SettingRow");
        setting_row->set_align_items(YGAlignCenter);

        const std::string& setting_label =
            Biz::_u8(item.def().full_label.empty() ? item.def().label : item.def().full_label);
        Text* label_text = setting_row->emplace_back<Text>(setting_label);
        label_text->set_wrap_mode(Text::WrapMode::Wrap);
        label_text->set_align({AlignH::Left, AlignV::Center});
        label_text->set_width(120);

        ConfigItemControl* control = ConfigItemControl::config_item_control_factory(
            setting_row,
            1,
            0,
            *config_item,
            *m_config_box_setter,
            {0}
        );

        if (Item* control_item = dynamic_cast<Item*>(control); control_item != nullptr) {
            control_item->set_width(100);
            control_item->set_flex_shrink(0);
        }

        control->set_overriden(true);
        control->set_state(*config_item);

        Text* setting_right_sidetext = setting_row->emplace_back<Text>(
            config_item->def().units.empty() ? std::string{} :
                                               Biz::_u8(config_item->def().units.front())
        );
        setting_right_sidetext->set_flex_grow(1);
        setting_right_sidetext->set_align({AlignH::Left, AlignV::Center});
        setting_right_sidetext->set_wrap_mode(Text::WrapMode::WrapElide);

        m_setting_rows.push_back(setting_row);
    }
}

void LayerRangeSettingsDialog::clear_settings()
{
    for (Item* setting_row : m_setting_rows) {
        m_settings_scroll->remove_later(setting_row);
    }
    m_setting_rows.clear();
}

} // namespace Slic3r::App::Plater
