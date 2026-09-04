#include "Slic3r/App/ColorDropdown.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include <algorithm>
#include <imgui_internal.h>

using Slic3r::Biz::ProjectInteractor;
using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::VirtualExtruder;

using Slic3r::Biz::Algorithms::VirtualExtruder::effective_color;

namespace Slic3r::App::Yoga {

class MulticolorCircle : public Yoga::Item
{
public:
    void render(const Vec2f& pos, const Vec2f& size)
    {
        render_item_begin(pos, size);

        ImRect rect(to_im(pos), to_im(pos + size));

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ASSERT(draw_list);

        const ImVec2 center{rect.GetCenter()};
        const float radius{0.5f * rect.GetHeight()};
        constexpr int circle_segments{64};

        ASSERT(!m_colors.empty());
        if (m_colors.size() == 1) {
            draw_list->AddCircleFilled(center, radius, m_colors.front(), circle_segments);
        } else {
            const float step{2.0f * IM_PI / static_cast<float>(m_colors.size())};
            constexpr float start_angle{0.5f * IM_PI};
            const int segments_per_sector{std::max(
                2,
                static_cast<int>((circle_segments + m_colors.size() - 1) / m_colors.size()))};

            for (std::size_t i{}; i < m_colors.size(); ++i) {
                const float angle_start{start_angle + static_cast<float>(i) * step};
                const float angle_end{angle_start + step};

                draw_list->PathClear();
                draw_list->PathLineTo(center);
                draw_list->PathArcTo(center, radius, angle_start, angle_end, segments_per_sector);
                draw_list->PathFillConvex(m_colors[i]);
            }
        }

        if (m_border_width > 0) {
            draw_list->AddCircle(rect.GetCenter(),
                                 0.5f * rect.GetHeight(),
                                 m_border_color,
                                 36,
                                 m_border_width);
        }

        render_item_end(pos, size);
    }

    void set_colors(const std::vector<ImColor>& colors) {
        m_colors = colors;
    }
    void set_border_width(float width) {
        m_border_width = width;
    }
    void set_border_color(const ImColor& color) {
        m_border_color = color;
    }

private:
    std::vector<ImColor> m_colors{m_theme->color_imgui(Platform::Color::Text)};
    ImColor m_border_color{m_theme->color_imgui(Platform::Color::Text)};
    float m_border_width{};
};

ColorMenuItem::ColorMenuItem(
    const std::string& label,
    const std::vector<Domain::ColorRGBA>& colors,
    bool selectable,
    bool dropdown_indicator,
    bool hollow,
    std::optional<std::string> index
) :
    m_dropdown_indicator(dropdown_indicator)
{
    set_min_height(24_fpx);
    set_flex_shrink(0);
    set_width_percent(100);
    set_content_padding(
        m_dropdown_indicator ? Yoga::Paddings{8_fpx, 3_fpx, 24_fpx, 3_fpx} : Yoga::Paddings{8_fpx, 3_fpx}
    );
    set_content_align_items(YGAlignCenter);
    set_content_justify_content(YGJustifyFlexStart);
    set_background_color(Platform::Color::ButtonTransparent);
    if (selectable) {
        set_background_color_checked(Platform::Color::Button);
        set_checkable(true);
    }

    m_swatch = emplace_back<Yoga::MulticolorCircle>();
    m_text = m_swatch->emplace_back<Text>("");
    m_text->set_font_type(Render::ImguiFontType::Bold);
    m_swatch->set_justify_content(YGJustifyCenter);
    m_swatch->set_align_items(YGAlignFlexStart);

    auto* spacer = emplace_back<Yoga::Item>();
    spacer->set_flex_grow(1);

    set_entry(label, colors, hollow, index);
}

void ColorMenuItem::set_entry(
    const std::string& label,
    const std::vector<Domain::ColorRGBA>& colors,
    const bool hollow,
    std::optional<std::string> index
)
{
    m_label = label;
    std::vector<ImColor> imgui_colors;
    for (const Domain::ColorRGBA& color : colors) {
        imgui_colors.emplace_back(color.r_uchar(), color.g_uchar(), color.b_uchar(), color.a_uchar());
    }

    if (imgui_colors.empty()) {
        imgui_colors.push_back(m_theme->color_imgui(Platform::Color::Text));
    }

    m_text->set_text_color(Imgui::contrast_color(imgui_colors.front()));

    if (hollow) {
        m_swatch->set_width(10_fpx);
        m_swatch->set_height(10_fpx);
        const Unit margin{3_fpx};
        m_swatch->set_margin({margin, margin, margin, margin});
        m_swatch->set_colors({m_theme->color_imgui(Platform::Color::Transparent)});
        m_swatch->set_border_width(1);
        m_swatch->set_border_color(imgui_colors.front());
    } else {
        m_swatch->set_width(index ? 16_fpx : 14_fpx);
        m_swatch->set_height(index ? 16_fpx : 14_fpx);
        m_swatch->set_margin(0);
        m_swatch->set_colors(imgui_colors);
        m_swatch->set_padding(0);
        m_swatch->set_border_width(0);
        m_swatch->set_border_color(m_theme->color_imgui(Platform::Color::Transparent));
    }

    ASSERT(m_swatch->items().size() == 1);
    auto text{dynamic_cast<Text*>(m_swatch->get_item(0))};
    ASSERT(text);
    text->set_text(index ? *index : "");
}

static float fpx(float value) {
    constexpr float imgui_scale_factor{18.0f / 14.0f};
    return value * imgui_scale_factor;
}

void ColorMenuItem::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    Yoga::RectangleButton::render(pos, size);

    const ImRect bb{to_im(pos), to_im(pos + size)};

    const ImVec2 swatch_pos{to_im(m_swatch->get_global_pos())};
    const float swatch_right{swatch_pos.x + m_swatch->width()};

    const float text_left{swatch_right + fpx(6)};
    const float right_padding{fpx(4)};
    float text_right{bb.Max.x - right_padding};

    if (!m_dropdown_indicator) {
        ImRect text_rect{ImVec2(text_left, bb.Min.y), ImVec2(text_right, bb.Max.y)};
        ImGui::RenderTextClipped(
            text_rect.Min,
            text_rect.Max,
            m_label.c_str(),
            nullptr,
            nullptr,
            ImVec2(0.f, 0.5f),
            &text_rect
        );
        return;
    }

    ImDrawList* draw_list{ImGui::GetWindowDrawList()};
    if (draw_list == nullptr) {
        return;
    }

    const ImGuiStyle& style{GImGui->Style};
    const float arrow_size{ImGui::GetFrameHeight()};
    const float value_x2{ImMax(bb.Min.x, bb.Max.x - arrow_size)};

    text_right = value_x2 - style.FramePadding.x;
    ImRect text_rect{ImVec2(text_left, bb.Min.y), ImVec2(text_right, bb.Max.y)};
    ImGui::RenderTextClipped(
        text_rect.Min,
        text_rect.Max,
        m_label.c_str(),
        nullptr,
        nullptr,
        ImVec2(0.f, 0.5f),
        &text_rect
    );

    const ImU32 text_col{ImGui::GetColorU32(hovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled)};
    if (value_x2 + arrow_size - style.FramePadding.x <= bb.Max.x) {
        const float w{arrow_size - 2.f * style.FramePadding.x};
        const float h{GImGui->FontSize};
        Yoga::YGRenderArrow(
            draw_list,
            ImVec2(value_x2 + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
            ImVec2(w, h),
            text_col,
            ImGuiDir_Down,
            1.0f
        );
    }
}

static std::vector<std::pair<ColorRGBA, std::string>> get_material_colors(
    const ProjectInteractor& project_interactor,
    const std::vector<ColorRGB>& slot_colors
)
{
    const Slic3r::Biz::Preset::PresetInteractor& preset_interactor{
        project_interactor.preset_interactor()
    };

    std::vector<ColorRGBA> colors;
    colors.reserve(slot_colors.size());
    std::ranges::transform(
        slot_colors,
        std::back_inserter(colors),
        [](const Domain::ColorRGB& color)
        { return Domain::ColorRGBA{color.r(), color.g(), color.b(), 1.0f}; }
    );

    std::vector<std::string> names;
    const auto& selected{preset_interactor.selected_printer_preset()};
    names.reserve(selected.materials.size());
    for (const auto& material : selected.materials) {
        names.push_back(std::string{material.short_name()});
    }

    std::vector<std::pair<Domain::ColorRGBA, std::string>> result{};

    std::size_t count{std::min(names.size(), colors.size())};
    for (std::size_t i{}; i < count; ++i) {
        result.push_back({colors[i], names[i]});
    }
    return result;
}

ColorDropdown::ColorDropdown(
    ProjectInteractor& project_interactor,
    bool with_default,
    bool with_numbers,
    bool with_virtual
) :
    m_project_interactor{project_interactor},
    m_with_default{with_default},
    m_with_numbers{with_numbers},
    m_with_virtual{with_virtual}
{
    m_project_interactor.project_settings_interactor().add_listener<Biz::IColorsChangedListener>(
        this
    );
    m_project_interactor.preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(
        this
    );
    m_project_interactor.virtual_extruder_interactor()
        .add_listener<IVirtualExtrudersChangedListener>(this);

    set_object_name("ColorDropdown");

    m_trigger = emplace_back<ColorMenuItem>(std::string{}, std::vector{Domain::ColorRGBA{}}, true, true);
    m_trigger->set_flex_grow(1);
    m_trigger->set_background_color(Platform::Color::Button);
    m_trigger->set_background_color_checked(
        m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered)
    );

    m_popup = m_trigger->emplace_back<Yoga::ContextPopup>("MMPaintingColorDropdownPopup");
    m_popup->content_item()->set_orientation(Yoga::Orientation::Vertical);
    m_popup->set_width_percent(100);
    m_popup->set_padding({2_fpx, 2_fpx});
    m_popup->content_item()->set_gap(2_fpx);
    m_popup->set_offset(2);
    m_popup->set_position(Yoga::Position::Bottom);

    m_trigger->callbacks().action = [this]()
    {
        if (m_popup->opened()) {
            m_popup->close();
        } else {
            m_popup->open();
        }
    };

    m_popup->callbacks().opened = [this]() { m_trigger->set_checked(true); };
    m_popup->callbacks().closed = [this]() { m_trigger->set_checked(false); };

    m_default_colors = {m_theme->color(Platform::Color::Text)};

    this->reload_items();
}

ColorDropdown::~ColorDropdown()
{
    m_project_interactor.project_settings_interactor().remove_listener<Biz::IColorsChangedListener>(
        this
    );
    m_project_interactor.preset_interactor().remove_listener<Biz::Preset::IPresetChangedListener>(
        this
    );
    m_project_interactor.virtual_extruder_interactor()
        .remove_listener<IVirtualExtrudersChangedListener>(this);
}

void ColorDropdown::on_colors_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& colors
)
{
    if (config_container_id != m_project_interactor.selected_config_container_id()) {
        return;
    }

    this->reload_items();
}

void ColorDropdown::on_preset_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (config_container_id != m_project_interactor.selected_config_container_id()) {
        return;
    }

    this->reload_items();
}

void
ColorDropdown::on_virtual_extruders_changed(SelectionId project_id, SelectionId config_container_id)
{
    if (config_container_id != m_project_interactor.selected_config_container_id()) {
        return;
    }

    this->reload_items();
}

void ColorDropdown::
    on_config_container_selection_changed(SelectionId project_id, SelectionId config_container_id)
{
    if (config_container_id != m_project_interactor.selected_config_container_id()) {
        return;
    }

    this->reload_items();
}

void ColorDropdown::reload_items()
{
    const std::optional<int> selected_extruder_id = this->selected_item_extruder_id();

    const SelectionId container_id = m_project_interactor.selected_config_container_id();

    const std::vector<ColorRGB> slot_colors =
        m_project_interactor.project_settings_interactor().get_colors(container_id);

    std::vector<std::pair<ColorRGBA, std::string>> items =
        get_material_colors(m_project_interactor, slot_colors);

    m_extruder_ids.clear();
    for (size_t i = 0; i < items.size(); ++i) {
        m_extruder_ids.push_back(static_cast<int>(i) + 1);
    }

    if (m_with_virtual && container_id != Domain::INVALID_ID) {
        for (const VirtualExtruder& virtual_extruder :
             m_project_interactor.selected_config_container().virtual_extruders())
        {
            const ColorRGB color =
                effective_color(virtual_extruder, slot_colors).value_or(ColorRGB::GRAY());

            items.emplace_back(
                ColorRGBA{color.r(), color.g(), color.b(), 1.f},
                "[V] " + Biz::_u8L("Extruder") + " " + std::to_string(virtual_extruder.id)
            );
            m_extruder_ids.push_back(static_cast<int>(virtual_extruder.id));
        }
    }

    this->set_items(items);

    // Clearing the selection when the extruder is gone would make current_index() abort.
    if (selected_extruder_id.has_value()) {
        const std::optional<std::size_t> index = this->index_of_extruder_id(*selected_extruder_id);
        if (index.has_value()) {
            this->set_current_index(index);
        }
    }
}

void ColorDropdown::set_items(
    const std::vector<std::pair<Domain::ColorRGBA, std::string>>& material_colors
)
{
    if (material_colors.empty()) {
        return;
    }

    m_material_colors = material_colors;
    reload();
}

std::optional<int> ColorDropdown::selected_item_extruder_id() const
{
    if (!m_current_index.has_value()) {
        return std::nullopt;
    }

    const std::size_t index = *m_current_index;
    if (m_with_default && index == 0) {
        return 0;
    }

    const std::size_t position = index - (m_with_default ? 1 : 0);
    if (position >= m_extruder_ids.size()) {
        return std::nullopt;
    }

    return m_extruder_ids[position];
}

void ColorDropdown::append_default_color_of_extruder(const int extruder_id)
{
    const std::optional<std::size_t> item_index = index_of_extruder_id(extruder_id);
    if (item_index.has_value()) {
        m_default_colors.push_back(m_material_colors[*item_index - (m_with_default ? 1 : 0)].first);
    }
}

void ColorDropdown::reload() {
    reload_default_colors();
    rebuild_popup_items();
    update_trigger_label();
}

void ColorDropdown::set_current_index(std::optional<std::size_t> index)
{
    if (m_material_colors.empty()) {
        return;
    }

    if (!index) {
        m_current_index = std::nullopt;
        update_trigger_label();
        return;
    }

    const std::size_t items_count{
        m_material_colors.size() + (m_with_default ? std::size_t{1} : std::size_t{0})
    };

    m_current_index = *index;

    if (m_current_index >= items_count) {
        rebuild_popup_items();
    }

    for (std::size_t i{}; i < m_popup_items.size(); ++i) {
        m_popup_items[i]->set_checked(i == m_current_index);
    }
    update_trigger_label();
}

void ColorDropdown::reload_default_colors()
{
    if (!m_with_default
        || m_project_interactor.selected_config_container_id() == Domain::INVALID_ID)
    {
        return;
    }

    const Biz::Scene::ObjectSelection& selection{
        m_project_interactor.scene_interactor().object_selection()};

    const Domain::ConfigPack config{
        m_project_interactor.selected_config_container().selected_preset().config()};
    const Domain::ConfigPackFDM* config_fdm{std::get_if<Domain::ConfigPackFDM>(&config)};
    if (!config_fdm) {
        return;
    }

    const std::array<std::string_view, 3> extruder_keys{
        "perimeter_extruder",
        "infill_extruder",
        "solid_infill_extruder",
    };

    m_default_colors.clear();
    if (selection.mode == Biz::Scene::SelectionMode::Volume) {
        std::set<int> object_extruders;
        if (!selection.only_single_object() || selection.empty()) {
            return;
        }
        const Domain::ModelObject* model_object{
            m_project_interactor.selected_project().find_object_by_id(
                selection.elements.front().object_id)};

        const int object_extruder{model_object->object_settings.items.opt("extruder").get<int>()};
        for (std::string_view extruder_key : extruder_keys)
        {
            const std::optional<Domain::ConfigItem> override{
                model_object->object_settings.overrides.get(std::string{extruder_key})};

            if (override) {
                object_extruders.insert(override->get<int>());
                continue;
            }
            if (object_extruder != 0) {
                object_extruders.insert(object_extruder);
                continue;
            }
            const int print_extruder{config_fdm->print.items.opt(extruder_key).get<int>()};
            ASSERT(print_extruder > 0);
            object_extruders.insert(print_extruder);
        }

        for (int extruder : object_extruders) {
            append_default_color_of_extruder(extruder);
        }
    } else {
        std::set<int> print_extruders;
        for (std::string_view extruder_key : extruder_keys)
        {
            const int extruder{config_fdm->print.items.opt(extruder_key).get<int>()};
            ASSERT(extruder > 0);
            print_extruders.insert(extruder);
        }
        for (int extruder : print_extruders) {
            append_default_color_of_extruder(extruder);
        }
    }
}

void ColorDropdown::set_current_index_internal(std::size_t index)
{
    const std::size_t items_count{
        m_material_colors.size() + (m_with_default ? std::size_t{1} : std::size_t{0})
    };

    ASSERT(index < items_count);
    m_current_index = index;
    for (std::size_t i{}; i < m_popup_items.size(); ++i) {
        m_popup_items[i]->set_checked(i == m_current_index);
    }
    update_trigger_label();
}

std::size_t ColorDropdown::current_index() const
{
    ASSERT(m_current_index);
    return *m_current_index;
}

int ColorDropdown::extruder_id_at(std::size_t index) const
{
    if (m_with_default && index == 0) {
        return 0;
    }

    const std::size_t position = index - (m_with_default ? 1 : 0);
    if (position < m_extruder_ids.size()) {
        return m_extruder_ids[position];
    }

    return static_cast<int>(index);
}

std::optional<std::size_t> ColorDropdown::index_of_extruder_id(int extruder_id) const
{
    if (extruder_id == 0) {
        return m_with_default ? std::optional<std::size_t>{0} : std::nullopt;
    }

    const std::size_t offset = m_with_default ? 1 : 0;
    for (std::size_t i = 0; i < m_extruder_ids.size(); ++i) {
        if (m_extruder_ids[i] == extruder_id) {
            return offset + i;
        }
    }

    return std::nullopt;
}

void ColorDropdown::style_node()
{
    if (m_popup != nullptr && m_trigger != nullptr) {
        m_popup->set_width(m_trigger->width());
    }

    Yoga::Item::style_node();
}

struct Label
{
    std::vector<Domain::ColorRGBA> colors;
    std::string name;
    bool hollow{};
    std::optional<std::string> index;
};

Label get_label(
    std::optional<std::size_t> index,
    bool with_default,
    bool with_numbers,
    const std::vector<std::pair<Domain::ColorRGBA, std::string>>& material_colors,
    const std::vector<Domain::ColorRGBA>& default_colors,
    const std::string& default_text
)
{
    if (!index) {
        return {
            .colors  = {Domain::ColorRGBA::WHITE()},
            .name   = Biz::_u8L("Mixed"),
            .hollow = true,
        };
    }

    const Label unkown_label{
        .colors = {Domain::ColorRGBA::RED()},
        .name  = fmt::format(
            fmt::runtime(Biz::_u8L("Invalid extruder ({})")),
            with_default ? *index : *index + 1
        ),
        .hollow = true,
    };

    if (with_default) {
        if (index == 0) {
            return {
                .colors = default_colors,
                .name   = default_text,
                .hollow = false,
            };
        } else {
            ASSERT(index > 0);
            if (*index - 1 >= material_colors.size()) {
                return unkown_label;
            } else {
                const auto& [color, name]{material_colors[*index - 1]};
                return {
                    .colors = {color},
                    .name  = name,
                    .index = with_numbers ? std::optional{std::to_string(*index)} : std::nullopt
                };
            }
        }
    } else {
        if (index >= material_colors.size()) {
            return unkown_label;
        } else {
            const auto& [color, name]{material_colors[*index]};
            return {
                .colors = {color},
                .name  = name,
                .index = with_numbers ? std::optional{std::to_string(*index + 1)} : std::nullopt
            };
        }
    }
}

void ColorDropdown::rebuild_popup_items()
{
    while (!m_popup->content_item()->items().empty()) {
        m_popup->content_item()->remove(m_popup->content_item()->items().back());
    }

    m_popup_items.clear();

    const std::size_t items_count{
        m_material_colors.size() + (m_with_default ? std::size_t{1} : std::size_t{0})
    };

    for (std::size_t index{}; index < items_count; ++index) {
        const auto& [colors, name, hollow, index_string]{get_label(index,
                                                                   m_with_default,
                                                                   m_with_numbers,
                                                                   m_material_colors,
                                                                   m_default_colors,
                                                                   get_default_text())};
        ColorMenuItem* item{
            m_popup->emplace_back<ColorMenuItem>(name, colors, true, false, hollow, index_string)};
        item->set_checked(index == m_current_index);
        m_popup_items.push_back(item);
        item->callbacks().action = [this, index]()
        {
            set_current_index_internal(index);
            on_color_selected(index);
            m_popup->close();
        };
    }
}

std::string ColorDropdown::get_default_text()
{
    if (m_project_interactor.scene_interactor().object_selection().mode
        == Biz::Scene::SelectionMode::Volume)
    {
        return Biz::_u8L("Inherit from object");
    }
    return Biz::_u8L("Inherit from settings");
}

void ColorDropdown::update_trigger_label()
{
    if (m_material_colors.empty()) {
        m_trigger->set_entry({}, {Domain::ColorRGBA{}});
        return;
    }
    const auto& [colors, name, hollow, index]{get_label(m_current_index,
                                                        m_with_default,
                                                        m_with_numbers,
                                                        m_material_colors,
                                                        m_default_colors,
                                                        get_default_text())};
    m_trigger->set_entry(name, colors, hollow, index);
}

} // namespace Slic3r::App::Yoga
