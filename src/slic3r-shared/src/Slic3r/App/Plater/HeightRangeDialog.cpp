#include "Slic3r/App/Plater/HeightRangeDialog.hpp"

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Plater/LayerRangeSettingsDialog.hpp"
#include "Slic3r/App/Plater/GizmoHelpFactory.hpp"
#include "Slic3r/App/Plater/HeightRangeControl.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/LambdaItem.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Domain/Config.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <map>
#include <ranges>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

using Slic3r::Domain::ConfigBox;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::LayerHeightRange;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::ZHeightPairs;

namespace Slic3r::App::Plater {

const constexpr float HEIGHT_RANGE_INPUT_HEIGHT = 26.f;
static const Paddings HEIGHT_RANGE_PADDING      = {0.f, 10.f, 0.f, 10.f};

const constexpr float HEIGHT_RANGE_INPUT_ROUNDING = 3.f;

const constexpr std::array ALWAYS_VISIBLE_MODIFIER_CATEGORIES = {
    ConfigItemDef::Category::Print_LayersSurfaces,
    ConfigItemDef::Category::Print_Infill,
};

static std::string format_trimmed(const double value, const int precision)
{
    std::string str_out = fmt::format("{:.{}f}", value, precision);
    if (str_out.find('.') != std::string::npos) {
        str_out.erase(str_out.find_last_not_of('0') + 1, std::string::npos);
        if (str_out.back() == '.') {
            str_out.pop_back();
        }
    }

    return str_out;
}

HeightRangeDialog::HeightRangeDialog(IConfigBoxSetter* config_box_setter) :
    GizmoWindowWithLeftSidePanel(),
    m_config_box_setter(config_box_setter)
{
    this->content()->set_orientation(Orientation::Vertical);
    this->content()->set_flex_grow(1);

    this->add_background_deselection_catcher(content());

    this->add_height_range_section(content());
    this->add_height_range_editor_section(content());
    this->add_overrides_section(content());
    this->add_modifier_button_section(content());

    this->add_separator(content());
    this->add_help_section(content());

    this->init_layer_height_profile_control();

    this->revert_button()->set_visible(true);
    this->gizmo_callbacks().revert_requested = [this]() { m_callbacks.revert_clicked(); };

    m_layer_range_settings_dialog =
        this->emplace_back<LayerRangeSettingsDialog>(m_config_box_setter);
    m_layer_range_settings_dialog->attach_to_item(this, Position::Left);
}

void HeightRangeDialog::clear_overrides()
{
    // Todo: This whole systems has to be rewriten to fully support observable lists
    while (m_overrides_section->object_count()) {
        m_overrides_section->remove(m_overrides_section->get_object(0));
    }
    m_layer_range_settings_dialog->clear_settings();
}

HeightRangeDialog::Callbacks& HeightRangeDialog::callbacks()
{
    return m_callbacks;
}

/**
 * Detects clicks on empty space inside the scroll area to deselect the current height range.
 * Uses direct ImGui state queries instead of InvisibleButton to avoid inflating the scroll
 * area's content size (which would cause the scrollbar to always be visible).
 */
void HeightRangeDialog::add_background_deselection_catcher(Item* parent)
{
    Yoga::LambdaItem* background_btn = parent->emplace_back<LambdaItem>(
        [this](const Vec2f&, const Vec2f&)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && ImGui::IsWindowHovered(ImGuiHoveredFlags_None)
                && !ImGui::IsAnyItemHovered())
            {
                m_callbacks.height_range_deselected();
            }
        }
    );

    background_btn->set_position_type(YGPositionTypeAbsolute);
    background_btn->set_width(0.f);
    background_btn->set_height(0.f);
}

void HeightRangeDialog::add_height_range_section(Item* parent)
{
    Item* height_range_section = parent->emplace_back<Item>();
    height_range_section->set_orientation(Orientation::Vertical);
    height_range_section->set_flex_shrink(0);

    Item* add_range_row = height_range_section->emplace_back<Item>();
    add_range_row->set_orientation(Orientation::Horizontal);

    Text* add_range_title = add_range_row->emplace_back<Text>(_u8L("Add range modifier"));
    add_range_title->set_font_type(Render::ImguiFontType::Bold);

    Item* spacer = add_range_row->emplace_back<Item>();
    spacer->set_flex_grow(1);

    LayoutButton* add_range_button =
        add_range_row->emplace_back<LayoutButton>("", Render::Icon::PlusHeightRange);
    add_range_button->set_width(22);
    add_range_button->set_height(22);
    add_range_button->callbacks().action = [this]() { m_callbacks.add_range_clicked(); };

    add_range_row->set_padding({0.f, 0.f, 0.f, 5.f});

    m_height_range_list_container = height_range_section->emplace_back<Item>();
    m_height_range_list_container->set_orientation(Orientation::VerticalReverse);
    m_height_range_list_container->set_gap(this->gap_size());
    m_height_range_list_container->set_width_percent(100.f);
}

void HeightRangeDialog::add_height_range_editor_section(Item* parent)
{
    m_height_range_editor_section = parent->emplace_back<Item>();
    m_height_range_editor_section->set_orientation(Orientation::Vertical);
    m_height_range_editor_section->set_gap(gap_size());
    m_height_range_editor_section->set_visible(false);
    m_height_range_editor_section->set_flex_shrink(0);

    this->add_separator(m_height_range_editor_section);

    Item* height_range_editor_content = m_height_range_editor_section->emplace_back<Item>();
    height_range_editor_content->set_padding({0.f, 10.f, 0.f, 10.f});
    height_range_editor_content->set_orientation(Orientation::Vertical);
    height_range_editor_content->set_gap(gap_size());

    Item* title_row = height_range_editor_content->emplace_back<Item>();
    Text* title     = title_row->emplace_back<Text>(_u8L("Range"));
    title->set_font_type(Render::ImguiFontType::Bold);

    Item* height_range_row = height_range_editor_content->emplace_back<Item>();
    height_range_row->set_orientation(Orientation::Horizontal);
    height_range_row->set_gap(this->gap_size());
    height_range_row->set_align_items(YGAlignCenter);

    Rectangle* min_z_wrapper = height_range_row->emplace_back<Rectangle>();
    min_z_wrapper->set_orientation(Orientation::Horizontal);
    min_z_wrapper->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    min_z_wrapper->set_rounding(HEIGHT_RANGE_INPUT_ROUNDING);
    min_z_wrapper->set_padding({8.f, 4.f, 8.f, 4.f});
    min_z_wrapper->set_align_items(YGAlignCenter);
    min_z_wrapper->set_gap(4.f);
    min_z_wrapper->set_height(HEIGHT_RANGE_INPUT_HEIGHT);
    min_z_wrapper->set_flex_grow(1);

    Icon* min_z_icon = min_z_wrapper->emplace_back<Icon>(Render::Icon::ArrowUpFromLine);
    min_z_icon->set_width(16.f);
    min_z_icon->set_height(16.f);

    m_min_z_input = min_z_wrapper->emplace_back<InputText>("");
    m_min_z_input->set_flex_grow(1);
    m_min_z_input->set_validator(std::make_unique<DoubleValidator>(0.));
    m_min_z_input->callbacks().text_edited = [this]()
    {
        try {
            Expr::Parser parser;
            Expr::Eval eval;
            m_callbacks.min_z_changed(
                boost::get<double>(eval.eval(parser.parse(m_min_z_input->text())))
            );
        } catch (const Expr::ParseError&) {
        } catch (const Expr::EvalError&) {
        }
    };

    Rectangle* max_z_wrapper = height_range_row->emplace_back<Rectangle>();
    max_z_wrapper->set_orientation(Orientation::Horizontal);
    max_z_wrapper->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    max_z_wrapper->set_rounding(HEIGHT_RANGE_INPUT_ROUNDING);
    max_z_wrapper->set_padding({8.f, 4.f, 8.f, 4.f});
    max_z_wrapper->set_align_items(YGAlignCenter);
    max_z_wrapper->set_gap(4.f);
    max_z_wrapper->set_height(HEIGHT_RANGE_INPUT_HEIGHT);
    max_z_wrapper->set_flex_grow(1);

    Icon* max_z_icon = max_z_wrapper->emplace_back<Icon>(Render::Icon::ArrowUpToLine);
    max_z_icon->set_width(16);
    max_z_icon->set_height(16);

    m_max_z_input = max_z_wrapper->emplace_back<InputText>("");
    m_max_z_input->set_flex_grow(1);
    m_max_z_input->set_validator(std::make_unique<DoubleValidator>(0.));
    m_max_z_input->callbacks().text_edited = [this]()
    {
        try {
            Expr::Parser parser;
            Expr::Eval eval;
            m_callbacks.max_z_changed(
                boost::get<double>(eval.eval(parser.parse(m_max_z_input->text())))
            );
        } catch (const Expr::ParseError&) {
        } catch (const Expr::EvalError&) {
        }
    };

    Text* mm_label = height_range_row->emplace_back<Text>(_u8L("mm"));
    mm_label->set_self_align(YGAlignCenter);
    mm_label->set_flex_shrink(0);
}

void HeightRangeDialog::add_overrides_section(Item* parent)
{
    m_overrides_section = parent->emplace_back<Item>();
    m_overrides_section->set_orientation(Orientation::Vertical);
    m_overrides_section->set_gap(gap_size());
    m_overrides_section->set_visible(false);
    m_overrides_section->set_flex_shrink(0);
}

void HeightRangeDialog::add_modifier_button_section(Item* parent)
{
    m_add_modifier_button_section = parent->emplace_back<Item>();
    m_add_modifier_button_section->set_orientation(Orientation::Vertical);
    m_add_modifier_button_section->set_gap(gap_size());
    m_add_modifier_button_section->set_visible(false);
    m_add_modifier_button_section->set_flex_shrink(0);

    this->add_separator(m_add_modifier_button_section);

    LayoutButton* add_modifier_button = m_add_modifier_button_section->emplace_back<LayoutButton>(
        _u8L("Add modifier"),
        Render::Icon::PlusModifier
    );
    add_modifier_button->set_content_direction(YGDirectionRTL);
    add_modifier_button->set_expand_label(true);
    add_modifier_button->set_label_font_type(Render::ImguiFontType::Bold);
    add_modifier_button->set_background_color(Platform::Color::ButtonTransparent);
    add_modifier_button->set_content_padding({1.f, 10.f, 4.f, 10.f});
    add_modifier_button->callbacks().action = [this]()
    {
        if (m_layer_range_settings_dialog->opened()) {
            m_layer_range_settings_dialog->close();
        } else {
            m_layer_range_settings_dialog->open();
        }
    };
}

void HeightRangeDialog::add_override_category_section(
    Item* parent,
    ConfigItemDef::Category category,
    const std::vector<std::reference_wrapper<const ConfigItem>>& config_items
)
{
    using ConfigItemRef = std::reference_wrapper<const ConfigItem>;

    this->add_separator(parent);

    const std::string category_name =
        Biz::_u8(ConfigItemDef::translate_category(category, PrinterTechnology::FFF));

    LayoutButton* add_override_button =
        parent->emplace_back<LayoutButton>(_u8L(category_name), Render::Icon::PlusModifier);
    add_override_button->set_content_direction(YGDirectionRTL);
    add_override_button->set_expand_label(true);
    add_override_button->set_label_font_type(Render::ImguiFontType::Bold);
    add_override_button->set_background_color(Platform::Color::ButtonTransparent);
    add_override_button->set_content_padding({1.f, 10.f, 4.f, 10.f});
    add_override_button->callbacks().action = [this, category]()
    { m_layer_range_settings_dialog->open_at_category(category); };

    if (config_items.empty()) {
        return;
    }

    Item* override_items_container = parent->emplace_back<Item>();
    override_items_container->set_orientation(Orientation::Vertical);
    override_items_container->set_gap(5.f);

    for (const ConfigItemRef& item_ref : config_items) {
        const ConfigItem& config_item = item_ref.get();

        ConfigItem* override_item = m_current_range_settings->overrides.find(config_item.name());
        if (!override_item) {
            continue;
        }

        Item* override_row = override_items_container->emplace_back<Item>();
        override_row->set_orientation(Orientation::Horizontal);
        override_row->set_padding({5.f, 0.f, 0.f, 0.f});
        override_row->set_gap(5.f);
        override_row->set_flex_shrink(0);
        override_row->set_align_items(YGAlignCenter);

        const std::string& config_item_name = config_item.def().full_label.empty() ?
            Biz::_u8(config_item.def().label) :
            Biz::_u8(config_item.def().full_label);
        Text* config_item_title             = override_row->emplace_back<Text>(config_item_name);
        config_item_title->set_width(120);
        config_item_title->set_max_width(120);
        config_item_title->set_height(40);
        config_item_title->set_wrap_mode(Text::WrapMode::WrapElide);
        config_item_title->set_align({AlignH::Left, AlignV::Center});

        ConfigItemControl* control = ConfigItemControl::config_item_control_factory(
            override_row,
            1,
            0,
            *override_item,
            *m_config_box_setter,
            {0}
        );
        if (Item* control_item = dynamic_cast<Item*>(control)) {
            control_item->set_width(100);
            control_item->set_flex_shrink(0);
        }

        control->set_overriden(true);
        control->set_state(*override_item);

        Text* sidetext = override_row->emplace_back<Text>(
            config_item.def().units.empty() ? std::string{} :
                                              Biz::_u8(config_item.def().units.front())
        );
        sidetext->set_height(40);
        sidetext->set_min_width(25);
        sidetext->set_wrap_mode(Text::WrapMode::WrapElide);
        sidetext->set_flex_grow(1);
        sidetext->set_align({AlignH::Left, AlignV::Center});

        LayoutButton* remove_override_button = override_row->emplace_back<LayoutButton>(
            std::string(),
            Render::Icon::Minus,
            _u8L("Remove override")
        );
        remove_override_button->set_width(22);
        remove_override_button->set_height(22);
        remove_override_button->callbacks().action = [this, key = config_item.name()]()
        { m_callbacks.override_removed(key); };
    }
}

void HeightRangeDialog::add_help_section(Item* parent)
{
    Item* help_section = parent->emplace_back<Item>();
    help_section->set_orientation(Orientation::Vertical);
    help_section->set_min_height(50);
    help_section->set_align_items(YGAlignFlexStart);
    help_section->set_gap(gap_size());
    help_section->set_flex_shrink(0);

    GizmoHelpFactory help;
    help.init(help_section);
    help.add_item({{"H"}}, _u8L("Add layer height override"));
    help.add_item({{"CTRL"}, {"C"}}, _u8L("Copy settings overrides"));
    help.add_item({{"CTRL"}, {"V"}}, _u8L("Paste settings overrides"));
}

void HeightRangeDialog::init_layer_height_profile_control()
{
    this->side_panel()->set_orientation(Orientation::Vertical);

    m_layer_height_profile_control = this->side_panel()->emplace_back<HeightRangeControl>();
    m_layer_height_profile_control->set_flex_grow(1);

    m_layer_height_profile_control->callbacks().height_range_clicked =
        [this](const std::optional<size_t> range_index)
    {
        if (range_index.has_value()) {
            ASSERT(range_index < m_height_range_rows.size());

            const HeightRangeEntry& height_range_entry =
                m_height_range_rows[range_index.value()]->height_range();
            m_callbacks.height_range_selected(
                LayerHeightRange{height_range_entry.min_z, height_range_entry.max_z}
            );
        } else {
            m_callbacks.height_range_deselected();
        }
    };

    m_layer_height_profile_control->callbacks().height_range_dragging =
        [this](const size_t range_index, const double new_min_z, const double new_max_z)
    {
        ASSERT(range_index < m_height_range_rows.size());

        if (!m_dragged_height_range.has_value()) {
            const HeightRangeEntry& height_range_entry =
                m_height_range_rows[range_index]->height_range();
            m_dragged_height_range =
                LayerHeightRange{height_range_entry.min_z, height_range_entry.max_z};
        }

        HeightRangeEntry height_range_entry = m_height_range_rows[range_index]->height_range();
        height_range_entry.min_z            = new_min_z;
        height_range_entry.max_z            = new_max_z;
        m_height_range_rows[range_index]->set_height_range(height_range_entry);

        m_layer_height_profile_control->update_height_range(range_index, new_min_z, new_max_z);
        this->set_height_range_min_z(new_min_z);
        this->set_height_range_max_z(new_max_z);

        m_callbacks.height_range_dragging(m_dragged_height_range.value(), new_min_z, new_max_z);
    };

    m_layer_height_profile_control->callbacks().height_range_drag_ended =
        [this](const size_t range_index, const double final_min_z, const double final_max_z)
    {
        if (m_dragged_height_range.has_value()) {
            m_callbacks
                .height_range_drag_ended(m_dragged_height_range.value(), final_min_z, final_max_z);
            m_dragged_height_range.reset();
        }
    };

    m_layer_height_profile_control->callbacks().height_range_hovered =
        [this](const std::optional<size_t> range_index)
    {
        if (range_index.has_value()) {
            ASSERT(range_index < m_height_range_rows.size());

            const HeightRangeEntry& height_range_entry =
                m_height_range_rows[range_index.value()]->height_range();
            m_callbacks.height_range_hovered(
                LayerHeightRange{height_range_entry.min_z, height_range_entry.max_z}
            );
        } else {
            m_callbacks.height_range_hovered(std::nullopt);
        }
    };
}

void HeightRangeDialog::set_layer_height_title(const double layer_height)
{
    side_panel_header_title()->set_text(format_trimmed(layer_height, 3));
}

void HeightRangeDialog::set_layer_height_profile(const ZHeightPairs& layer_height_profile)
{
    m_layer_height_profile_control->set_layer_height_profile(layer_height_profile);
}

void HeightRangeDialog::set_selected_height_range_config_box(ConfigBox* settings)
{
    m_current_range_settings = settings;
    m_layer_range_settings_dialog->set_config_box(settings);
}

void HeightRangeDialog::set_object_max_z(const float object_max_z)
{
    m_layer_height_profile_control->set_object_max_z(object_max_z);
}

void HeightRangeDialog::set_min_layer_height(const float min_layer_height)
{
    m_layer_height_profile_control->set_min_layer_height(min_layer_height);
}

void HeightRangeDialog::set_max_layer_height(const float max_layer_height)
{
    m_layer_height_profile_control->set_max_layer_height(max_layer_height);
}

void HeightRangeDialog::set_default_layer_height(const float default_layer_height)
{
    m_layer_height_profile_control->set_default_layer_height(default_layer_height);
}

void HeightRangeDialog::set_height_range_min_z(const double min_z)
{
    m_min_z_input->set_text(format_trimmed(min_z, 3));
}

void HeightRangeDialog::set_height_range_max_z(const double max_z)
{
    m_max_z_input->set_text(format_trimmed(max_z, 3));
}

void HeightRangeDialog::update_height_ranges(
    const HeightRangeEntries& height_range_entries,
    const Domain::LayerConfigRanges& layer_config_ranges
)
{
    m_layer_height_profile_control->set_height_ranges(height_range_entries);

    for (HeightRangeRow* item : m_height_range_rows) {
        item->set_visible(false);
        m_height_range_list_container->remove_later(item);
    }
    m_height_range_rows.clear();

    for (const HeightRangeEntry& height_range_entry : height_range_entries) {
        const LayerHeightRange height_range{height_range_entry.min_z, height_range_entry.max_z};
        HeightRangeRow* item =
            m_height_range_list_container->emplace_back<HeightRangeRow>(height_range_entry);
        item->callbacks().selected = [this, height_range]()
        { m_callbacks.height_range_selected(height_range); };
        item->callbacks().delete_clicked = [this, height_range]()
        { m_callbacks.delete_range_clicked(height_range); };
        item->callbacks().hovered = [this, height_range](const bool is_hovered)
        {
            m_callbacks.height_range_hovered(
                is_hovered ? std::optional(height_range) : std::nullopt
            );
        };
        item->callbacks().undo_clicked = [this, height_range]()
        { m_callbacks.undo_overrides_clicked(height_range); };

        m_height_range_rows.emplace_back(item);
    }

    for (HeightRangeRow* height_range_row : m_height_range_rows) {
        const HeightRangeEntry& height_range_entry = height_range_row->height_range();
        const LayerHeightRange height_range{height_range_entry.min_z, height_range_entry.max_z};
        const auto layer_config_range_it = layer_config_ranges.find(height_range);
        const bool has_overrides         = layer_config_range_it != layer_config_ranges.end()
            && !layer_config_range_it->second.overrides.empty();

        height_range_row->set_has_overrides(has_overrides);
    }

    m_height_range_list_container->set_padding(
        height_range_entries.empty() ? Paddings{0.f} : HEIGHT_RANGE_PADDING
    );
}

void HeightRangeDialog::update_overrides_section()
{
    using ConfigItemRef = std::reference_wrapper<const ConfigItem>;

    for (Item* item : m_overrides_section->items()) {
        item->set_visible(false);
        m_overrides_section->remove_later(item);
    }

    if (m_current_range_settings == nullptr) {
        return;
    }

    std::map<ConfigItemDef::Category, std::vector<ConfigItemRef>> grouped_by_categories;
    for (const ConfigItemRef& config_item_ref :
         m_current_range_settings->overrides.overridden_items())
    {
        grouped_by_categories[config_item_ref.get().def().category].emplace_back(config_item_ref);
    }

    for (const ConfigItemDef::Category category : ALWAYS_VISIBLE_MODIFIER_CATEGORIES) {
        add_override_category_section(
            m_overrides_section,
            category,
            grouped_by_categories[category]
        );
    }

    for (auto& [category, config_items] : grouped_by_categories) {
        if (std::ranges::find(ALWAYS_VISIBLE_MODIFIER_CATEGORIES, category)
            != ALWAYS_VISIBLE_MODIFIER_CATEGORIES.end())
        {
            continue;
        }

        add_override_category_section(m_overrides_section, category, config_items);
    }
}

void HeightRangeDialog::update_single_height_range(
    const LayerHeightRange& range_to_update,
    const HeightRangeEntry& height_range_entry
)
{
    std::optional<size_t> index_to_update = find_height_range_row_index(range_to_update);
    if (!index_to_update.has_value() && range_to_update == m_selected_height_range) {
        index_to_update = m_selected_row_index;
    }

    if (index_to_update.has_value()) {
        const size_t row_index = index_to_update.value();
        m_height_range_rows[row_index]->set_height_range(height_range_entry);
        m_layer_height_profile_control
            ->update_height_range(row_index, height_range_entry.min_z, height_range_entry.max_z);
    }
}

void HeightRangeDialog::select_range(const LayerHeightRange& range_to_select)
{
    this->select_range(std::optional(range_to_select));
}

void HeightRangeDialog::clear_selection()
{
    this->select_range(std::nullopt);
}

void HeightRangeDialog::highlight_range(const std::optional<LayerHeightRange>& range_to_highlight)
{
    if (m_highlighted_row_index.has_value()) {
        if (m_highlighted_row_index.value() < m_height_range_rows.size()) {
            m_height_range_rows[m_highlighted_row_index.value()]->set_highlighted(false);
        }
    }

    m_highlighted_row_index = range_to_highlight.has_value() ?
        find_height_range_row_index(range_to_highlight.value()) :
        std::nullopt;
    if (!m_highlighted_row_index.has_value() && range_to_highlight == m_selected_height_range) {
        m_highlighted_row_index = m_selected_row_index;
    }

    if (m_highlighted_row_index.has_value()) {
        const size_t highlighted_row_index = m_highlighted_row_index.value();
        m_height_range_rows[highlighted_row_index]->set_highlighted(true);
        m_layer_height_profile_control->set_external_hovered_height_range(highlighted_row_index);
    } else {
        m_layer_height_profile_control->reset_external_hovered_height_range();
    }
}

void HeightRangeDialog::select_range(const std::optional<LayerHeightRange>& range_to_select)
{
    if (m_selected_height_range.has_value()) {
        const std::optional<size_t> old_index =
            find_height_range_row_index(m_selected_height_range.value());

        if (old_index.has_value()) {
            m_height_range_rows[old_index.value()]->set_checked(false);
        }
    }

    m_selected_height_range = range_to_select;
    m_selected_row_index    = range_to_select.has_value() ?
        find_height_range_row_index(range_to_select.value()) :
        std::nullopt;

    if (m_selected_row_index.has_value()) {
        m_height_range_rows[m_selected_row_index.value()]->set_checked(true);
        m_height_range_editor_section->set_visible(true);
        m_overrides_section->set_visible(true);
        m_add_modifier_button_section->set_visible(true);
        m_layer_height_profile_control->set_selected_height_range(m_selected_row_index.value());
    } else {
        m_height_range_editor_section->set_visible(false);
        m_overrides_section->set_visible(false);
        m_add_modifier_button_section->set_visible(false);
        m_layer_height_profile_control->reset_selected_height_range();

        if (m_layer_range_settings_dialog->opened()) {
            m_layer_range_settings_dialog->close();
        }
    }
}

std::optional<size_t> HeightRangeDialog::find_height_range_row_index(
    const LayerHeightRange& key
) const
{
    for (HeightRangeRow* const& height_range_row : m_height_range_rows) {
        const size_t index = static_cast<size_t>(&height_range_row - m_height_range_rows.data());
        const HeightRangeEntry& height_range_entry = height_range_row->height_range();
        if (height_range_entry.min_z == key.first && height_range_entry.max_z == key.second) {
            return index;
        }
    }

    return std::nullopt;
}

} // namespace Slic3r::App::Plater
