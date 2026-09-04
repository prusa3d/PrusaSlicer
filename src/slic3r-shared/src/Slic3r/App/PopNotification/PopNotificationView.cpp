#include "Slic3r/App/PopNotification/PopNotificationView.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ProgressBar.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Slic3r::App::PopNotification {

using namespace Yoga;

const Unit TotalWidth = 400_fpx;
const Unit MinHeight  =  40_fpx;
const Unit MaxHeight  = 160_fpx;

PopNotificationView::PopNotificationView(
    size_t index,
    const PopNotificationData& data,
    PopNotificationObservableList& notification_list
) :
    Yoga::Window("PopNotification"),
    Biz::DataObserver<PopNotificationData>(index, data),
    m_notification_list(notification_list),
    m_current_level(m_state->level)
{
    layout();
}

void PopNotificationView::reset()
{
    if (m_top_row) {
        remove(m_top_row);
        m_top_row = nullptr;
    }
    if (m_mid_column) {
        remove(m_mid_column);
        m_mid_column = nullptr;
    }

    m_left_column           = nullptr;
    m_top_mid               = nullptr;
    m_right_column          = nullptr;
    m_text                  = nullptr;
    m_header                = nullptr;
    m_button_line           = nullptr;
    m_progress_bar          = nullptr;
    m_left_icon             = nullptr;
    m_close_button          = nullptr;
    m_progress_percent_text = nullptr;
    m_buttons.clear();
}

void PopNotificationView::layout()
{
    std::visit(
        Domain::overloaded{
            [this](const PopNotificationLayoutText&) { layout_type_text(); },
            [this](const PopNotificationLayoutHeaderText&) { layout_type_header_text(); },
            [this](const PopNotificationLayoutHeader&) { layout_type_header(); },
            [this](const PopNotificationLayoutImageHeader&) { layout_type_image_header(); },
            [this](const PopNotificationLayoutImageText&) { layout_type_image_text(); },
            [this](const PopNotificationLayoutImageHeaderText&)
            { layout_type_image_header_text(); },
            [this](const PopNotificationLayoutTextButtons&) { layout_type_text_buttons(); },
            [this](const PopNotificationLayoutHeaderTextButtons&)
            { layout_type_header_text_buttons(); },
            [this](const PopNotificationLayoutTextProgress&) { layout_type_text_progress(); },
            [this](const PopNotificationLayoutHeaderTextProgress&)
            { layout_type_header_text_progress(); },
            [this](const PopNotificationLayoutHeaderProgress&)
            { layout_type_header_progress(); }
        },
        m_state->layout
    );

    m_current_layout = m_state->layout;
}

void PopNotificationView::on_data_update()
{
    ASSERT(m_state);

    if (m_current_layout.index() != m_state->layout.index() || m_current_level != m_state->level) {
        m_current_level = m_state->level;
        reset();
        layout();
        return;
    }

    std::visit(
        Domain::overloaded{
            [this](const PopNotificationLayoutText& d) { update_text(d.text); },
            [this](const PopNotificationLayoutHeaderText& d)
            {
                update_header(d.header);
                update_text(d.text);
            },
            [this](const PopNotificationLayoutHeader& d) { update_header(d.header); },
            [this](const PopNotificationLayoutImageHeader& d)
            {
                if (d.image_path.empty() != (m_left_icon == nullptr)) {
                    reset();
                    layout();
                    return;
                }
                update_header(d.header);
                update_image(d.image_path);
            },
            [this](const PopNotificationLayoutImageText& d)
            {
                if (d.image_path.empty() != (m_left_icon == nullptr)) {
                    reset();
                    layout();
                    return;
                }
                update_text(d.text);
                update_image(d.image_path);
            },
            [this](const PopNotificationLayoutImageHeaderText& d)
            {
                if (d.image_path.empty() != (m_left_icon == nullptr)) {
                    reset();
                    layout();
                    return;
                }
                update_header(d.header);
                update_text(d.text);
                update_image(d.image_path);
            },
            [this](const PopNotificationLayoutTextButtons& d)
            {
                update_text(d.text);
                update_buttons(d.buttons);
            },
            [this](const PopNotificationLayoutHeaderTextButtons& d)
            {
                update_header(d.header);
                update_text(d.text);
                update_buttons(d.buttons);
            },
            [this](const PopNotificationLayoutTextProgress& d)
            {
                update_text(d.text);
                update_progress(d.progress);
            },
            [this](const PopNotificationLayoutHeaderTextProgress& d)
            {
                update_header(d.header);
                update_text(d.text);
                update_progress(d.progress);
            },
            [this](const PopNotificationLayoutHeaderProgress& d)
            {
                update_header(d.header);
                update_progress(d.progress);
            }
        },
        m_state->layout
    );
}

void PopNotificationView::basic_layout(const LeftContent& left_content, bool top_row_only)
{
    set_margin(5_fpx);
    set_max_width(TotalWidth);
    set_max_height(MaxHeight);
    set_flex_shrink(0.f);
    set_orientation(Yoga::Orientation::Vertical);
    set_padding(Yoga::Paddings(16_fpx, 10_fpx, 16_fpx, 10_fpx));

    if (top_row_only) {
        //set_padding(Yoga::Paddings(20_fpx, 10_fpx, 20_fpx, 10_fpx));
        set_min_height(MinHeight);
        set_justify_content(YGJustifyCenter);
    }

    if (m_state->level == PopNotificationLevel::Error) {
        set_border_size(2.f);
        set_border_color(m_theme->color_imgui(Platform::Color::Error));
    } else if (m_state->level == PopNotificationLevel::Warning) {
        set_border_size(2.f);
        set_border_color(m_theme->color_imgui(Platform::Color::Warning));
    } else {
        set_border_size(0.f);
        set_border_color(std::nullopt);
    }

    m_top_row = emplace_back<Yoga::Item>();
    m_top_row->set_orientation(Yoga::Orientation::Horizontal);
    m_top_row->set_width_percent(100.f);
    m_top_row->set_flex_shrink(0.f);

    m_left_column  = m_top_row->emplace_back<Yoga::Item>();
    m_top_mid      = m_top_row->emplace_back<Yoga::Item>();
    m_right_column = m_top_row->emplace_back<Yoga::Item>();

    m_left_column->set_self_align(YGAlignCenter);
    m_top_mid->set_self_align(YGAlignCenter);
    m_top_mid->set_flex_grow(1.f);
    m_right_column->set_self_align(YGAlignCenter);

    if (!top_row_only) {
        m_mid_column = emplace_back<Yoga::Item>();
        m_mid_column->set_orientation(Yoga::Orientation::Vertical);
        m_mid_column->set_flex_grow(1.f);
        m_mid_column->set_self_align(YGAlignStretch);
    }

    if (const std::string* image_path = std::get_if<std::string>(&left_content);
        image_path && !image_path->empty()) {
        basic_left_image_layout(*image_path);
    } else {
        const Render::Icon* icon = std::get_if<Render::Icon>(&left_content);
        basic_left_layout(icon ? *icon : Render::Icon::None);
    }
    basic_right_layout();
}

void PopNotificationView::basic_left_layout(Render::Icon icon_override)
{
    m_left_column->set_orientation(Yoga::Orientation::Horizontal);
    m_left_column->set_justify_content(YGJustifyFlexStart);

    Render::Icon icon = icon_override;
    std::optional<Platform::Color> tint_color;

    if (icon == Render::Icon::None && m_state->level == PopNotificationLevel::Warning) {
        icon       = Render::Icon::WarningMarkerWhite;
        tint_color = Platform::Color::Warning;
    } else if (icon == Render::Icon::None && m_state->level == PopNotificationLevel::Error) {
        icon       = Render::Icon::ErrorMarker;
        tint_color = Platform::Color::Error;
    }

    if (icon == Render::Icon::None) {
        m_left_column->set_min_width(0);
        m_left_column->set_min_height(0);
        return;
    }

    m_left_column->set_min_width(25_fpx);
    m_left_column->set_min_height(16_fpx);
    m_left_icon = m_left_column->emplace_back<Yoga::Icon>(icon);
    m_left_icon->set_min_width(16_fpx);
    m_left_icon->set_min_height(16_fpx);
    m_left_icon->set_margin({0_fpx, 0_fpx, 10_fpx, 0_fpx});

    if (tint_color) {
        m_left_icon->set_tint(m_theme->color_imgui(*tint_color));
    }
}

void PopNotificationView::basic_left_image_layout(const std::string& image_path)
{
    m_left_column->set_orientation(Yoga::Orientation::Horizontal);
    m_left_column->set_justify_content(YGJustifyFlexStart);

    if (image_path.empty()) {
        m_left_column->set_min_width(0);
        m_left_column->set_min_height(0);
        return;
    }

    m_left_column->set_min_width(30_fpx);
    m_left_column->set_min_height(16_fpx);

    m_left_icon = m_left_column->emplace_back<Yoga::Icon>(Render::Icon::None);
    m_left_icon->set_min_width(16_fpx);
    m_left_icon->set_min_height(16_fpx);
    m_left_icon->set_rounding(10_fpx);
    m_left_icon->set_margin({0_fpx, 0_fpx, 10_fpx, 0_fpx});
    m_left_icon->set_image(image_path);
}

void PopNotificationView::basic_right_layout()
{
    m_right_column->set_orientation(Yoga::Orientation::Horizontal);
    m_right_column->set_justify_content(YGJustifyFlexEnd);

    if (m_state->level != PopNotificationLevel::ProgressNoClose) {
        m_close_button = m_right_column->emplace_back<Yoga::LayoutButton>(
            std::string{},
            Render::Icon::NotificationCloseGray
        );
        m_close_button->set_min_width(16_fpx);
        m_close_button->set_min_height(16_fpx);
        m_close_button->set_max_width(16_fpx);
        m_close_button->set_max_height(16_fpx);
        m_close_button->callbacks().action = [this]()
        {
            // This code is called from render function -> removing notification now might trigger destroying object that is rendering right now.
            Biz::Platform::PlatformServices::instance()
                .main_thread_dispatcher()
                .dispatch_on_main_thread(
                    [this]() { m_notification_list.on_notification_close_button(m_state); }
                );
        };
        m_close_button->set_margin({10_fpx, 0_fpx, 0_fpx, 0_fpx});
    }
}

void PopNotificationView::basic_mid_layout()
{
    m_mid_column->set_orientation(Yoga::Orientation::Vertical);
    m_mid_column->set_justify_content(YGJustifyFlexStart);
}

void PopNotificationView::basic_mid_header_layout(const std::string& header)
{
    m_header = m_top_mid->emplace_back<Yoga::Text>(header);
    m_header->set_font_type(Render::ImguiFontType::Bold);
    m_header->set_text_color(text_color());
    m_header->set_margin({0_fpx, 5_fpx, 0_fpx, 5_fpx});
    m_header->set_flex_shrink(0.f);
    m_header->set_width_percent(100.f);
    m_header->set_wrap_mode(Yoga::Text::WrapMode::WrapElide);
}

void PopNotificationView::basic_mid_text_layout(const std::string& text)
{
    auto* text_scroll = m_mid_column->emplace_back<Yoga::ScrollArea>("TextScroll");
    text_scroll->set_flex_grow(1.f);
    text_scroll->set_flex_shrink(1.f);
    text_scroll->set_justify_content(YGJustifyFlexStart);
    text_scroll->set_margin({0_fpx, 0_fpx, -10_fpx, 10_fpx});
    text_scroll->set_padding({0_fpx, 0_fpx, 10_fpx, 0_fpx});
    text_scroll->set_width_percent(100.f);

    m_text = text_scroll->emplace_back<Yoga::Text>(text);
    m_text->set_wrap_mode(Yoga::Text::WrapMode::Wrap);
    m_text->set_text_color(text_color());
    m_text->set_flex_shrink(0.f);
    m_text->set_width_percent(100.f);
}

void PopNotificationView::basic_mid_buttons_layout(
    const std::vector<PopNotificationButtonData>& buttons
)
{
    m_button_line = m_mid_column->emplace_back<Yoga::Item>();
    m_button_line->set_orientation(Yoga::Orientation::Horizontal);
    m_button_line->set_justify_content(YGJustifyFlexStart);
    m_button_line->set_margin({0_fpx, 0_fpx, 0_fpx, 5_fpx});
    m_button_line->set_flex_shrink(0.f);

    for (const auto& bdata : buttons) {
        Yoga::LayoutButton* button =
            m_buttons.emplace_back(m_button_line->emplace_back<Yoga::LayoutButton>(bdata.text));
        button->callbacks().action = [bdata, this]()
        {
            ASSERT(bdata.callback);
            Biz::Platform::PlatformServices::instance()
                .main_thread_dispatcher()
                .dispatch_on_main_thread(
                    [cb = bdata.callback, this, state_to_close = this->m_state]()
                    {
                        if (cb()) {
                            m_notification_list.on_notification_close_button(state_to_close);
                        }
                    }
                );
        };
        style_button(button);
        button->set_margin({0_fpx, 0_fpx, 5_fpx, 0_fpx});
        if (button->label_object()) {
            button->label_object()->set_margin({5_fpx, 2_fpx, 5_fpx, 2_fpx});
        }
    }
}

void PopNotificationView::basic_mid_progress_layout(int progress)
{
    m_progress_bar = m_mid_column->emplace_back<Yoga::ProgressBar>();
    m_progress_bar->set_show_overlay(true);
    m_progress_bar->set_flex_grow(1.f);
    m_progress_bar->set_progress(progress);
    m_progress_bar->set_min_width(m_mid_column->min_width());
    m_progress_bar->set_min_height(3_fpx);
    m_progress_bar->set_margin({0_fpx, 5_fpx, 0_fpx, 0_fpx});
    m_progress_bar->set_flex_shrink(0.f);

    m_progress_percent_text =
        m_mid_column->emplace_back<Yoga::Text>(std::to_string(progress) + "%");
    m_progress_percent_text->set_margin({0.f, 0.f, 0.f, 0.f});
    m_progress_percent_text->set_flex_shrink(0.f);
}

void PopNotificationView::layout_type_text()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutText>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_text_layout(layout_data->text);
}

void PopNotificationView::layout_type_header_text()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutHeaderText>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_header_layout(layout_data->header);
    basic_mid_text_layout(layout_data->text);
}

void PopNotificationView::layout_type_header()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutHeader>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon, /*top_row_only=*/true);
    basic_mid_header_layout(layout_data->header);
}

void PopNotificationView::layout_type_image_header()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutImageHeader>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->image_path, /*top_row_only=*/true);
    basic_mid_header_layout(layout_data->header);
}

void PopNotificationView::layout_type_image_text()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutImageText>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->image_path);
    basic_mid_layout();
    basic_mid_text_layout(layout_data->text);
}

void PopNotificationView::layout_type_image_header_text()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutImageHeaderText>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->image_path);
    basic_mid_layout();
    basic_mid_header_layout(layout_data->header);
    basic_mid_text_layout(layout_data->text);
}

void PopNotificationView::layout_type_text_buttons()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutTextButtons>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_text_layout(layout_data->text);
    basic_mid_buttons_layout(layout_data->buttons);
}

void PopNotificationView::layout_type_header_text_buttons()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutHeaderTextButtons>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_header_layout(layout_data->header);
    basic_mid_text_layout(layout_data->text);
    basic_mid_buttons_layout(layout_data->buttons);
}

void PopNotificationView::layout_type_text_progress()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutTextProgress>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_text_layout(layout_data->text);
    basic_mid_progress_layout(layout_data->progress);
}

void PopNotificationView::layout_type_header_text_progress()
{
    const auto* layout_data =
        std::get_if<PopNotificationLayoutHeaderTextProgress>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_header_layout(layout_data->header);
    basic_mid_text_layout(layout_data->text);
    basic_mid_progress_layout(layout_data->progress);
}

void PopNotificationView::layout_type_header_progress()
{
    const auto* layout_data = std::get_if<PopNotificationLayoutHeaderProgress>(&m_state->layout);
    ASSERT(layout_data);
    basic_layout(layout_data->icon);
    basic_mid_layout();
    basic_mid_header_layout(layout_data->header);
    basic_mid_progress_layout(layout_data->progress);
}

void PopNotificationView::update_text(const std::string& text)
{
    if (m_text && text != m_text->text()) {
        m_text->set_text(text);
    }
}

void PopNotificationView::update_header(const std::string& text)
{
    if (m_header && text != m_header->text()) {
        m_header->set_text(text);
    }
}

void PopNotificationView::update_image(const std::string& image_path)
{
    if (m_left_icon && m_left_icon->image() != image_path) {
        m_left_icon->set_image(image_path);
    }
}

void PopNotificationView::update_buttons(const std::vector<PopNotificationButtonData>& buttons)
{
    if (!m_button_line)
        return;
    for (size_t i = 0; i < buttons.size(); i++) {
        if (m_buttons.size() <= i) {
            style_button(m_buttons.emplace_back(
                m_button_line->emplace_back<Yoga::LayoutButton>(buttons[i].text)
            ));
        }
        if (m_buttons[i]->label() != buttons[i].text) {
            m_buttons[i]->set_label(buttons[i].text);
        }

        m_buttons[i]->callbacks().action = [cb = buttons[i].callback, this]()
        {
            ASSERT(cb);
            // This code is called from render function -> removing notification now might trigger destroying object that is rendering right now.
            Biz::Platform::PlatformServices::instance()
                .main_thread_dispatcher()
                .dispatch_on_main_thread(
                    [cb, this, state_to_close = this->m_state]()
                    {
                        if (cb()) {
                            m_notification_list.on_notification_close_button(state_to_close);
                        }
                    }
                );
        };
    }
}

void PopNotificationView::update_progress(int progress)
{
    if (m_progress_percent_text) {
        m_progress_percent_text->set_text(std::to_string(progress) + "%");
    }
    if (m_progress_bar) {
        m_progress_bar->set_progress(progress);
    }
}

ImColor PopNotificationView::text_color() const
{
    return m_theme->color_imgui(Platform::Color::Text);
}

std::optional<Platform::Color> PopNotificationView::button_accent_color() const
{
    switch (m_state->level) {
    case PopNotificationLevel::Warning:
        return Platform::Color::Warning;
    case PopNotificationLevel::Error:
        return Platform::Color::Error;
    default:
        return std::nullopt;
    }
}

ImColor PopNotificationView::readable_accent_color(const ImColor& accent) const
{
    const ImColor background      = m_theme->color_imgui(Platform::Color::Button);
    const bool    dark_background = Imgui::is_dark(background);
    if (dark_background != Imgui::is_dark(accent)) {
        return accent;
    }
    return Imgui::adjust_brightness(accent, dark_background ? 1.6f : 0.6f);
}

void PopNotificationView::style_button(Yoga::LayoutButton* button) const
{
    button->set_background_color(Platform::Color::Button);

    const std::optional<Platform::Color> accent = button_accent_color();
    if (!accent) {
        button->set_label_color(text_color());
        button->set_background_border_width(0.f);
        return;
    }

    const ImColor accent_color = m_theme->color_imgui(*accent);
    button->set_label_color(readable_accent_color(accent_color));
    button->set_background_color_border(
        accent_color,
        Imgui::adjust_brightness(accent_color, 1.25f)
    );
    button->set_background_border_width((1_fpx).value);
}

} // namespace Slic3r::App::PopNotification
