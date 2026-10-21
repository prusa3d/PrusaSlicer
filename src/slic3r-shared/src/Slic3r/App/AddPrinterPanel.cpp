#include "Slic3r/App/AddPrinterPanel.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/PhysicalPrinterForm.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r::App {

using namespace Yoga;

static Yoga::Item* append_item(Yoga::Item* parent, Yoga::ItemPtr item)
{
    auto item_ptr{item.get()};
    parent->append(std::move(item));
    return item_ptr;
}

static std::string get_thumbnail(const Domain::Preset::HwPrinterConfig& config)
{
    if (!config.visual.thumbnail) {
        return "";
    }
    return config.relative_path_to_assets() + *config.visual.thumbnail;
}

static std::string to_lower(std::string str)
{
    std::ranges::transform(str, str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

static bool matches_search(
    const AddPrinterPanel::PrinterEntry& entry,
    const std::string& vendor_name,
    const std::string& search_text
)
{
    if (search_text.empty()) {
        return true;
    }

    const Domain::Preset::HwPrinterConfig& config{entry.printer_config};
    const std::string needle{to_lower(search_text)};
    const auto contains = [&needle](const std::string& text)
    { return to_lower(text).find(needle) != std::string::npos; };

    return contains(entry.preset_name)
        || contains(config.name)
        || contains(config.short_name)
        || contains(config.model.base_model)
        || contains(vendor_name);
}

static bool
has_template_id(const AddPrinterPanel::PrinterEntry& entry, const std::string& template_id)
{
    const std::optional<std::string>& config_template_id{entry.printer_config.template_id};
    return config_template_id.has_value() && *config_template_id == template_id;
}

AddPrinterPanel::AddPrinterPanel(
    Biz::ProjectInteractor& project_interactor,
    std::function<void()> close,
    std::function<void(const Biz::PhysicalPrinter::PhysicalPrinterConfig&)> add_printer
) :
    m_project_interactor{project_interactor},
    m_add_printer{add_printer},
    m_preset_changed_listener_scope(m_project_interactor.preset_interactor(), *this)
{
    const ImColor secondary_color{m_theme->color_imgui(Platform::Color::WindowBgAlternate)};

    set_object_name("AddPrinterPanel");
    set_orientation(Orientation::Vertical);
    set_align_items(YGAlignStretch);

    if (close) {
        auto top_bar{emplace_back<Rectangle>()};
        top_bar->set_align_items(YGAlignCenter);
        top_bar->set_padding({0, 0, 10_fpx, 0});
        top_bar->set_fill(secondary_color);
        top_bar->set_flex_shrink(0);
        top_bar->set_justify_content(YGJustifySpaceBetween);
        auto title{top_bar->emplace_back<Rectangle>()};
        title->set_padding({30_fpx, 11_fpx, 30_fpx, 11_fpx});
        title->emplace_back<Text>(Biz::_u8L("Choose a printer"));
        title->set_fill(m_theme->color_imgui(Platform::Color::WindowBg));
        title->set_rounding(0);
        title->set_flex_shrink(0);

        auto close_button{top_bar->emplace_back<LayoutButton>("", Render::Icon::TopBarCross)};
        close_button->set_width(22_fpx);
        close_button->set_height(22_fpx);
        close_button->callbacks().action = close;
    }

    m_stack_layout = emplace_back<StackLayout>();
    m_stack_layout->set_orientation(Orientation::Vertical);
    m_stack_layout->set_flex_grow(1);

    create_page_search();

    create_page_details();
}

ItemPtr AddPrinterPanel::create_printer_family(
    const PrinterFamily& printer_family,
    const std::vector<const PrinterEntry*>& printers
)
{
    auto result{std::make_unique<Item>()};
    result->set_orientation(Orientation::Vertical);
    result->set_gap(20_fpx);
    result->set_flex_shrink(0);

    auto title_item{result->emplace_back<Text>(
        printer_family.label.empty() ? printer_family.base_model : printer_family.label
    )};
    title_item->set_font_type(Render::ImguiFontType::Bold);
    title_item->set_font_size(16_fpx);
    title_item->set_flex_shrink(0);

    auto printer_section{result->emplace_back<Item>()};
    printer_section->set_gap(20_fpx);
    printer_section->set_flex_wrap(YGWrapWrap);
    printer_section->set_flex_shrink(0);

    for (const PrinterEntry* entry : printers) {
        auto printer{printer_section->emplace_back<RectangleButton>()};
        printer->set_flex_shrink(0);
        printer->set_content_padding(0);
        printer->callbacks().action = [this, entry]()
        {
            m_physical_printer_form->set_printer_config(
                Biz::PhysicalPrinter::PhysicalPrinterConfig{
                    Biz::PhysicalPrinter::FileSystemExport{},
                    std::string{},
                    entry->preset_name,
                    std::string{},
                    entry->printer_config
                }
            );
            m_printer_image->set_image(get_thumbnail(entry->printer_config));
            m_printer_name->set_text(entry->preset_name);
            m_stack_layout->set_current_index(1);
            // AddPrinterPanel::Printer printer{entry->printer};
            // m_add_printer(printer);
        };

        auto printer_content{printer->emplace_back<Item>()};
        printer_content->set_padding({20_fpx, 15_fpx, 20_fpx, 15_fpx});
        printer_content->set_width(160_fpx);
        printer_content->set_height(140_fpx);
        printer_content->set_orientation(Orientation::Vertical);
        printer_content->set_gap(15_fpx);
        printer_content->set_flex_shrink(0);
        printer_content->set_align_items(YGAlignCenter);

        auto image{printer_content->emplace_back<Icon>(Render::Icon::None)};
        image->set_image(get_thumbnail(entry->printer_config));
        image->set_height(80_fpx);
        image->set_aspect_ratio(1.0);
        image->set_flex_shrink(0);
        m_shown_thumbnails.push_back(image);

        printer_content->emplace_back<Text>(entry->printer_config.short_name);
    }
    return result;
}

void AddPrinterPanel::emplace_vendor_button(const std::string& name, const std::string& id)
{
    auto button{m_left_bar->emplace_back<LayoutButton>(name)};
    button->set_content_padding({20_fpx, 10_fpx, 20_fpx, 10_fpx});
    button->set_rounding(0);
    button->set_checkable(true);
    button->set_content_justify_content(YGJustifyFlexStart);
    button->set_flex_shrink(0);
    m_button_vendors.emplace(button, id);

    if (m_selected_vendor == id) {
        button->set_checked(true);
        m_vendor_button_group.insert_button(button);
    } else {
        m_vendor_button_group.insert_button(button);
    }
}

void AddPrinterPanel::create_page_search()
{
    const ImColor secondary_color{m_theme->color_imgui(Platform::Color::WindowBgAlternate)};

    Item* page_search = m_stack_layout->emplace_back<Item>();
    page_search->set_orientation(Orientation::Vertical);

    auto search_row{page_search->emplace_back<Item>()};
    search_row->set_gap(14_fpx);
    search_row->set_align_items(YGAlignCenter);
    search_row->set_padding({38_fpx, 6_fpx, 6_fpx, 6_fpx});
    auto search_icon{search_row->emplace_back<Icon>(Render::Icon::Search)};
    search_icon->set_width(16_fpx);
    search_icon->set_height(16_fpx);
    search_row->set_flex_shrink(0);

    auto search{search_row->emplace_back<InputTextField>()};
    search->set_padding(8_fpx);
    search->set_flex_shrink(0);
    search->set_color(Platform::Color::WindowBg);
    search->set_rounding(0);
    search->set_hint(Biz::_u8L("Search printer"));
    search->set_flex_grow(1);
    search->callbacks().text_changed = [this, search]
    {
        m_search_text = search->text();
        rebuild_printer_view();
    };

    auto separator{page_search->emplace_back<Rectangle>()};
    separator->set_fill(secondary_color);
    separator->set_height(1_px);
    separator->set_flex_shrink(0);

    auto content{page_search->emplace_back<Item>()};
    content->set_flex_grow(1);
    content->set_align_items(YGAlignStretch);

    m_left_bar = content->emplace_back<ScrollArea>();
    m_left_bar->set_width(240_fpx);
    m_left_bar->set_padding({0, 20_fpx, 0, 20_fpx});
    m_left_bar->set_orientation(Orientation::Vertical);

    auto vertical_separator{content->emplace_back<Rectangle>()};
    vertical_separator->set_fill(secondary_color);
    vertical_separator->set_width(1_px);

    m_printers = content->emplace_back<ScrollArea>();
    m_printers->set_flex_grow(1);
    m_printers->set_padding(20_fpx);
    m_printers->set_orientation(Orientation::Vertical);
    m_printers->set_gap(35_fpx);

    m_detail = content->emplace_back<Item>();
    m_detail->set_width_percent(100);
    m_detail->set_visible(false);
    m_printers->set_flex_grow(1);

    m_vendor_button_group.callbacks().checked_changed =
        [this](Yoga::AbstractButton* current_checked, Yoga::AbstractButton* /*last_checked*/)
    {
        const auto vendor_it{m_button_vendors.find(current_checked)};
        if (vendor_it == m_button_vendors.end() || m_selected_vendor == vendor_it->second) {
            return;
        }
        m_selected_vendor = vendor_it->second;
        rebuild_printer_families();
        rebuild_printer_view();
    };

    reload();
}

void AddPrinterPanel::create_page_details()
{
    ScrollArea* page_details = m_stack_layout->emplace_back<ScrollArea>();
    page_details->set_orientation(Orientation::Vertical);
    page_details->set_gap(5_fpx);
    page_details->set_padding(Paddings{0, 0, 10_fpx, 0});

    Item* row = page_details->emplace_back<Item>();
    row->set_gap(5_fpx);
    row->set_flex_shrink(0);

    Item* left_column = row->emplace_back<Item>();
    left_column->set_orientation(Orientation::Vertical);
    left_column->set_gap(5_fpx);

    m_printer_name = left_column->emplace_back<Text>(std::string{});
    m_printer_name->set_wrap_mode(Text::WrapMode::WrapElide);
    m_printer_name->set_width_percent(100);
    m_printer_name->set_align({AlignH::Center, AlignV::Center});

    m_printer_image = left_column->emplace_back<Icon>(Render::Icon::None);
    m_printer_image->set_width(200_fpx);
    m_printer_image->set_aspect_ratio(1.0);
    m_printer_image->set_flex_shrink(0);

    m_physical_printer_form = row->emplace_back<PhysicalPrinterForm>(m_project_interactor);
    m_physical_printer_form->set_flex_grow(1);
    m_physical_printer_form->set_flex_shrink(0);
    m_physical_printer_form->set_max_width(400_fpx);
    m_physical_printer_form->callbacks().printer_saved =
        [this](const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer)
    {
        if (m_add_printer) {
            m_add_printer(printer);
        }
    };
}

void AddPrinterPanel::reload_vendor_buttons(const Domain::Preset::Bundle& preset_bundle)
{
    // Detach the old buttons from the group first, so it holds no dangling pointers.
    m_vendor_button_group.set_buttons({});
    m_button_vendors.clear();

    // Immediate remove is safe here - this function must never be called from within
    // a button callback (the button would destroy itself mid-render).
    while (!m_left_bar->items().empty()) {
        m_left_bar->remove(m_left_bar->items().back());
    }

    const Domain::Preset::VendorBundles& vendor_bundles{preset_bundle.vendor_bundles};
    if (vendor_bundles.empty()) {
        m_selected_vendor.reset();
        return;
    }

    const bool selection_valid{
        m_selected_vendor.has_value() && vendor_bundles.contains(*m_selected_vendor)
    };
    if (!selection_valid) {
        m_selected_vendor.reset();
        for (const auto& [vendor_id, vendor_bundle] : vendor_bundles) {
            if (!vendor_bundle.printer_configs.empty()) {
                m_selected_vendor = vendor_id;
                break;
            }
        }
        if (!m_selected_vendor.has_value()) {
            m_selected_vendor = vendor_bundles.begin()->first;
        }
    }

    const std::string prefer_vendor_id{"PrusaResearch"};
    const auto it{vendor_bundles.find(prefer_vendor_id)};
    if (it != vendor_bundles.end()) {
        emplace_vendor_button(it->second.vendor_data.info.name, it->first);
    }

    for (const auto& [vendor_id, vendor_bundle] : vendor_bundles) {
        if (vendor_id == prefer_vendor_id) {
            continue;
        }
        emplace_vendor_button(vendor_bundle.vendor_data.info.name, vendor_id);
    }
}

void AddPrinterPanel::rebuild_printer_families()
{
    m_printer_families.clear();
    m_vendor_name.clear();

    const Domain::Preset::Bundle& preset_bundle{m_project_interactor.workbench().preset_bundle()};
    if (!m_selected_vendor.has_value()) {
        return;
    }

    const auto vendor_it{preset_bundle.vendor_bundles.find(*m_selected_vendor)};
    if (vendor_it == preset_bundle.vendor_bundles.end()) {
        return;
    }

    const Domain::Preset::VendorBundle& vendor_bundle{vendor_it->second};
    m_vendor_name = vendor_bundle.vendor_data.info.name;

    for (const Domain::Preset::HwPrinterConfig& config : vendor_bundle.printer_configs) {
        const auto evaluated_it{preset_bundle.evaluated_presets.find(config.id)};
        if (evaluated_it == preset_bundle.evaluated_presets.end()) {
            continue;
        }

        for (const Domain::Preset::EvaluatedPrinterPreset& evaluated : evaluated_it->second) {
            if (evaluated.preset.origin != Domain::Preset::PresetOrigin::System) {
                continue;
            }

            const PrinterEntry entry{
                // .printer = Printer{
                // .preset_item_id = evaluated.preset.id,
                // .default_config = config,
                // .tools          = m_project_interactor.preset_interactor().get_tool_items(config),
                // .sheets         = m_project_interactor.preset_interactor().get_sheet_items(config)
                // },
                .printer_config = config,
                .preset_name    = evaluated.preset.name
            };

            const auto family_it{std::ranges::find_if(
                m_printer_families,
                [&](const PrinterFamily& family)
                { return family.base_model == config.model.base_model; }
            )};

            if (family_it == m_printer_families.end()) {
                m_printer_families.push_back(
                    PrinterFamily{.base_model = config.model.base_model, .printers = {entry}}
                );
            } else {
                family_it->printers.push_back(entry);
            }
        }
    }

    const std::vector<Domain::Preset::PrinterFamilyInfo>& order{
        vendor_bundle.vendor_data.info.printer_families
    };
    if (order.empty()) {
        return;
    }

    std::vector<PrinterFamily> ordered_families;
    std::vector<bool> family_used(m_printer_families.size(), false);
    for (const Domain::Preset::PrinterFamilyInfo& order_entry : order) {
        for (size_t fi{}; fi < m_printer_families.size(); ++fi) {
            if (family_used[fi] || m_printer_families[fi].base_model != order_entry.base_model) {
                continue;
            }

            family_used[fi] = true;
            PrinterFamily& family{m_printer_families[fi]};

            std::vector<PrinterEntry> ordered_printers;
            std::vector<bool> printer_used(family.printers.size(), false);
            for (const std::string& template_id : order_entry.printer_configs_order) {
                for (size_t pi{}; pi < family.printers.size(); ++pi) {
                    if (printer_used[pi] || !has_template_id(family.printers[pi], template_id)) {
                        continue;
                    }
                    printer_used[pi] = true;
                    ordered_printers.push_back(std::move(family.printers[pi]));
                }
            }
            for (size_t pi{}; pi < family.printers.size(); ++pi) {
                if (!printer_used[pi]) {
                    ordered_printers.push_back(std::move(family.printers[pi]));
                }
            }
            family.printers = std::move(ordered_printers);
            family.label    = order_entry.label;

            ordered_families.push_back(std::move(family));
        }
    }
    for (size_t fi{}; fi < m_printer_families.size(); ++fi) {
        if (!family_used[fi]) {
            ordered_families.push_back(std::move(m_printer_families[fi]));
        }
    }
    m_printer_families = std::move(ordered_families);
}

void AddPrinterPanel::rebuild_printer_view()
{
    m_detail->set_visible(false);
    m_printers->set_visible(true);

    clear_printer_items();

    for (const PrinterFamily& family : m_printer_families) {
        std::vector<const PrinterEntry*> matched_printers;
        for (const PrinterEntry& entry : family.printers) {
            if (matches_search(entry, m_vendor_name, m_search_text)) {
                matched_printers.push_back(&entry);
            }
        }

        if (!matched_printers.empty()) {
            append_item(m_printers, create_printer_family(family, matched_printers));
        }
    }
}

void AddPrinterPanel::clear_printer_items()
{
    for (Yoga::Icon* thumbnail : m_shown_thumbnails) {
        if (thumbnail->texture()) {
            m_texture_cache.push(thumbnail->texture());
        }
    }
    while (m_texture_cache.size() > 100) {
        m_texture_cache.pop();
    }
    while (!m_printers->items().empty()) {
        m_printers->remove(m_printers->items().back());
    }
    m_shown_thumbnails.clear();
}

void AddPrinterPanel::reload()
{
    clear_printer_items();

    reload_vendor_buttons(m_project_interactor.workbench().preset_bundle());
    rebuild_printer_families();
    rebuild_printer_view();
}

} // namespace Slic3r::App
