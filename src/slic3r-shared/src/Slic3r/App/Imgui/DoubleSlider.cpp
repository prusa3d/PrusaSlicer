#include "Slic3r/App/Imgui/DoubleSlider.hpp"

#include <algorithm>

namespace Slic3r::App::Imgui::DoubleSlider {

ImU32 Control::fg_color() const
{
    return ImGui::GetColorU32({0.99f, 0.41f, 0.2f, 1.0f});
} // color from SidebarAfterSlice::render()

ImU32 Control::bg_color() const
{
    return ImGui::GetColorU32({0.5f, 0.5f, 0.5f, 1.0f});
}

ImU32 Control::tooltip_bg_color() const
{
    return m_theme->color_imgui(Platform::Color::WindowBg);
}

ImVec2 Control::DrawOptions::dummy_sz() const
{
    float x_ratio = has_ruler ? 3.375f : 2.0f;
    return ImVec2(x_ratio* font_size, font_size) * scale;
}

ImVec2 Control::DrawOptions::groove_sz() const
{
    return ImVec2(0.25f * font_size, 0.25f * font_size) * scale;
}

ImVec2 Control::DrawOptions::draggable_region_sz() const
{
    return ImVec2(1.25f * font_size, 1.25f * font_size) * scale;
}

ImVec2 Control::DrawOptions::text_dummy_sz() const
{
    return ImVec2(2.5f * font_size, 2.125f * font_size) * scale;//ImVec2(40.0f, 34.0f) * scale;
}

ImVec2 Control::DrawOptions::text_padding() const
{
    return ImVec2(5.0f, 2.0f) * scale;
}

ImVec2 Control::DrawOptions::triangle_offset() const
{
    return ImVec2(0.6f * font_size, 0.5f * font_size) * scale;//ImVec2(9.0f, 8.0f) * scale;
}

float Control::DrawOptions::thumb_radius() const
{
    return 0.5f * font_size * scale;// 8.0f * scale;
}

float Control::DrawOptions::thumb_border() const
{
    return 0.125f * font_size * scale;// 2.0f * scale;
}

float Control::DrawOptions::rounding() const
{
    return 0.125f * font_size * scale;// 2.0f * scale;
}

ImRect Control::DrawOptions::groove(const ImVec2& pos, const ImVec2& size, bool is_horizontal) const
{
    ImVec2 groove_start = is_horizontal ?
        ImVec2(pos.x + text_size.x, pos.y + size.y - groove_sz().y - dummy_sz().y) :
        ImVec2(pos.x + size.x - groove_sz().x - dummy_sz().x, pos.y + text_size.y);
    ImVec2 groove_size = is_horizontal ?
        ImVec2(size.x - 2 * (text_size.x), groove_sz().y) :
        ImVec2(groove_sz().x, size.y - 2 * text_size.y);

    return ImRect(groove_start, groove_start + groove_size);
}

ImRect Control::DrawOptions::draggable_region(const ImRect& groove, bool is_horizontal) const
{
    ImRect draggable_region = is_horizontal ?
        ImRect(groove.Min.x, groove.GetCenter().y, groove.Max.x, groove.GetCenter().y) :
        ImRect(groove.GetCenter().x, groove.Min.y, groove.GetCenter().x, groove.Max.y);
    draggable_region.Expand(is_horizontal ?
                            ImVec2(/*thumb_radius()*/0, draggable_region_sz().y) :
                            ImVec2(draggable_region_sz().x, 0));
    return draggable_region;
}

ImRect Control::DrawOptions::slider_line(const ImRect& draggable_region, const ImVec2& h_thumb_center, const ImVec2& l_thumb_center, bool is_horizontal) const
{
    ImVec2 mid = draggable_region.GetCenter();
    ImRect scroll_line = is_horizontal ?
        ImRect(ImVec2(l_thumb_center.x, mid.y - groove_sz().y / 2), ImVec2(h_thumb_center.x, mid.y + groove_sz().y / 2)) :
        ImRect(ImVec2(mid.x - groove_sz().x / 2, h_thumb_center.y), ImVec2(mid.x + groove_sz().x / 2, l_thumb_center.y));

    return scroll_line;
}

Control::Control(
    ImGuiSliderFlags flags,
    bool use_lower_thumb
) : Item()
    , m_selection(SelectedSlider::Undefined)
    , m_flags(flags)
    , m_draw_lower_thumb(use_lower_thumb)
{
    m_border_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

void Control::render(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    m_pos = to_im(pos);
    m_size = to_im(size);

    render_item_begin(pos, size);

    int higher_pos = m_higher_pos;
    int lower_pos = m_lower_pos;
    std::string higher_label = label(m_higher_pos);
    std::string lower_label = label(m_lower_pos);
    int temp_higher_pos = m_higher_pos;
    int temp_lower_pos = m_lower_pos;

    m_draw_opts.text_size = m_draw_opts.calc_text_size(label(m_max_pos));

    bool processed_mouse_wheel{ false };
    if (draw_slider(&higher_pos, &lower_pos, higher_label, lower_label, m_pos, m_size)) {
        processed_mouse_wheel =ImGui::GetIO().MouseWheel != 0.0f;
        if (temp_higher_pos != higher_pos) {
            m_higher_pos = higher_pos;
            if (m_combine_thumbs) {
                m_lower_pos = m_higher_pos;
            }
        }
        if (temp_lower_pos != lower_pos) {
            m_lower_pos = lower_pos;
        }
        if (m_callbacks.value_changed) {
            m_callbacks.value_changed();
        }
    }

    // Update activation state
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        // Explicitly activated by mouse
        m_is_activated = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    }
    else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Automatically activated if the window is focused (e.g., via Tab)
        m_is_activated = true;
    }

    if (callbacks().extra_render) {
        callbacks().extra_render();
    }

    if (processed_mouse_wheel && callbacks().request_extra_frame) {
        callbacks().request_extra_frame();
    }

    render_item_end(pos, size);
}

int Control::active_pos() const
{
    return m_selection == SelectedSlider::Lower ? m_lower_pos :
           m_selection == SelectedSlider::Higher ? m_higher_pos : -1;
}

void Control::set_lower_pos(const int lower_pos)
{
    m_selection = SelectedSlider::Lower;
    m_lower_pos = lower_pos;
    correct_lower_pos();
}

void Control::set_higher_pos(const int higher_pos)
{
    m_selection = SelectedSlider::Higher;
    m_higher_pos = higher_pos;
    correct_higher_pos();
}

void Control::set_selection_span(const int lower_pos, const int higher_pos)
{
    m_lower_pos = std::max(lower_pos, m_min_pos);
    m_higher_pos = std::max(std::min(higher_pos, m_max_pos), m_lower_pos);
    if (m_lower_pos < m_higher_pos)
        m_combine_thumbs = false;
}

void Control::set_max_pos(const int max_pos)
{
    m_max_pos = max_pos;
    correct_higher_pos();
}

void Control::update_draw_options(float scale, bool has_ruler)
{
    m_draw_opts.scale = scale;
    m_draw_opts.font_size = ImGui::GetStyle().FontSizeBase;
    m_draw_opts.has_ruler = has_ruler;
}

void Control::move_active_thumb(int delta)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.NavActive && !m_is_activated) {
        // Block input if Nav is active but this specific slider isn't the focus/active
        return;
    }

    if (m_selection == SelectedSlider::Undefined)
        m_selection = SelectedSlider::Higher;

    if (m_selection == SelectedSlider::Lower) {
        m_lower_pos += delta;
        correct_lower_pos();
    }
    else if (m_selection == SelectedSlider::Higher) {
        m_higher_pos += delta;
        correct_higher_pos();
    }
}

void Control::correct_lower_pos()
{
    if (m_lower_pos < m_min_pos)
        m_lower_pos = m_min_pos;
    else if (m_lower_pos > m_max_pos)
        m_lower_pos = m_max_pos;

    if ((m_lower_pos >= m_higher_pos && m_lower_pos <= m_max_pos) || m_combine_thumbs)
        m_higher_pos = m_lower_pos;
}

void Control::correct_higher_pos()
{
    if (m_higher_pos > m_max_pos)
        m_higher_pos = m_max_pos;
    else if (m_higher_pos < m_min_pos)
        m_higher_pos = m_min_pos;

    if ((m_higher_pos <= m_lower_pos && m_higher_pos >= m_min_pos) || m_combine_thumbs)
        m_lower_pos = m_higher_pos;
}

void Control::combine_thumbs(bool combine)
{
    m_combine_thumbs = combine;
    if (combine) {
        m_selection = SelectedSlider::Higher;
        correct_higher_pos();
    }
    else
        reset_positions();
}

void Control::reset_positions()
{
    set_lower_pos(m_min_pos);
    set_higher_pos(m_max_pos);
    (m_selection == SelectedSlider::Lower) ? correct_lower_pos() : correct_higher_pos();
}

std::string Control::label(int pos) const
{
    if (m_cb_get_label)
        return m_cb_get_label(pos);

    if (pos >= m_max_pos || pos < m_min_pos)
        return "ErrVal";

    return std::to_string(pos);
}

float Control::position_in_rect(int pos, const ImRect& rect) const
{
    int v_min = m_min_pos;
    int v_max = m_max_pos;

    float pos_ratio = (v_max - v_min) != 0 ? (float)(pos - v_min) / (float)(v_max - v_min) : 0.0f;
    float thumb_pos;
    if (is_horizontal())
        thumb_pos = rect.Min.x + (rect.Max.x - rect.Min.x) * pos_ratio;
    else {
        pos_ratio = 1.0f - pos_ratio;
        thumb_pos = rect.Min.y + (rect.Max.y - rect.Min.y) * pos_ratio;
    }
    return thumb_pos;
}

ImRect Control::active_thumb_rect() const
{
    return (m_selection == SelectedSlider::Lower) ? m_regions.lower_thumb : m_regions.higher_thumb;
}

bool Control::is_rclick_on_thumb()
{
    if (m_rclick_on_selected_thumb) {
        // discard right mouse click at list its value is checked to avoud reuse it on next frame
        m_rclick_on_selected_thumb = false;
        m_suppress_process_behavior = false;
        return true;
    }
    return false;
}

bool Control::is_lclick_on_thumb()
{
    if (m_lclick_on_selected_thumb) {
        // discard left mouse click at list its value is checked to avoud reuse it on next frame
        m_lclick_on_selected_thumb = false;
        m_suppress_process_behavior = false;
        return true;
    }
    return false;
}

bool Control::is_lclick_on_hovered_pos()
{
    if (m_lclick_on_hovered_pos) {
        // Discard left mouse click at hovered tick to avoud reuse it on next frame
        m_lclick_on_hovered_pos = false;
        return true;
    }
    return false;
}

void Control::draw_scroll_line(const ImRect& scroll_line, const ImRect& slideable_region)
{
    if (m_cb_draw_scroll_line)
        m_cb_draw_scroll_line(scroll_line, slideable_region);
    else
        ImGui::RenderFrame(scroll_line.Min, scroll_line.Max, fg_color(), false, m_draw_opts.rounding());
}

void Control::draw_background(const ImRect& slideable_region)
{
    if (slideable_region.Max.x < slideable_region.Min.x || slideable_region.Max.y < slideable_region.Min.y)
        return;
    ImVec2 groove_sz = m_draw_opts.groove_sz() * 0.55f;
    auto groove_center = slideable_region.GetCenter();
    ImRect groove = is_horizontal() ?
        ImRect(slideable_region.Min.x, groove_center.y - groove_sz.y, slideable_region.Max.x, groove_center.y + groove_sz.y) :
        ImRect(groove_center.x - groove_sz.x, slideable_region.Min.y, groove_center.x + groove_sz.x, slideable_region.Max.y);
    ImVec2 groove_padding = (is_horizontal() ? ImVec2(2.0f, 2.0f) : ImVec2(3.0f, 4.0f)) * m_draw_opts.scale;

    ImRect bg_rect = groove;
    bg_rect.Expand(groove_padding);

    // draw bg of scroll
    ImGui::RenderFrame(groove.Min, groove.Max, bg_color(), false, 0.5f * groove.GetWidth());
}

void Control::draw_label(std::string label, const ImRect& thumb, bool is_mirrored /*= false*/, bool with_border /*= false*/)
{
    if (label.empty() || label == "ErrVal")
        return;

    ImVec2 thumb_center = thumb.GetCenter();
    ImVec2 text_padding = m_draw_opts.text_padding();
    float  rounding = m_draw_opts.rounding();

    ImVec2 triangle_offset = m_draw_opts.triangle_offset();
    ImVec2 text_content_size = ImGui::CalcTextSize(label.c_str());
    ImVec2 text_size = text_content_size + text_padding * 2;
    ImVec2 text_start = is_horizontal() ?
        ImVec2(thumb.Max.x + triangle_offset.x, thumb_center.y - text_size.y) :
        ImVec2(thumb.Min.x - text_size.x - triangle_offset.x, thumb_center.y - text_size.y) ;

    if (is_mirrored)
        text_start = is_horizontal() ?
            ImVec2(thumb.Min.x - text_size.x - triangle_offset.x, thumb_center.y - text_size.y) :
            ImVec2(thumb.Min.x - text_size.x - triangle_offset.x, thumb_center.y) ;

    ImRect text_rect(text_start, text_start + text_size);

    if (with_border) {
        float rounding_b = 0.75f * rounding;

        ImRect text_rect_b(text_rect);
        text_rect_b.Expand(ImVec2(rounding_b, rounding_b));

        float triangle_offset_x_b = triangle_offset.x + rounding_b;
        float triangle_offset_y_b = triangle_offset.y + rounding_b;

        ImVec2 pos_1 = is_horizontal() ?
            ImVec2(text_rect_b.Min.x + rounding_b, text_rect_b.Max.y) :
            ImVec2(text_rect_b.Max.x - rounding_b, text_rect_b.Max.y);
        ImVec2 pos_2 = is_horizontal() ? pos_1 - ImVec2(triangle_offset_x_b, 0.f) : pos_1 - ImVec2(0.f, triangle_offset_y_b);
        ImVec2 pos_3 = is_horizontal() ? pos_1 - ImVec2(0.f, triangle_offset_y_b) : pos_1 + ImVec2(triangle_offset_x_b, 0.f);

        if (is_mirrored) {
            pos_1 = is_horizontal() ?
                ImVec2(text_rect_b.Max.x - rounding_b - 1, text_rect_b.Max.y - 1) :
                ImVec2(text_rect_b.Max.x - rounding_b, text_rect_b.Min.y);
            pos_2 = is_horizontal() ? pos_1 + ImVec2(triangle_offset_x_b, 0.f) : pos_1 + ImVec2(0.f, triangle_offset_y_b);
            pos_3 = is_horizontal() ? pos_1 - ImVec2(0.f, triangle_offset_y_b) : pos_1 + ImVec2(triangle_offset_x_b, 0.f);
        }

        ImGui::RenderFrame(text_rect_b.Min, text_rect_b.Max, bg_color(), true, rounding);
        ImGui::GetCurrentWindow()->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, bg_color());
    }

    ImVec2 pos_1 = is_horizontal() ?
        ImVec2(text_rect.Min.x + rounding, text_rect.Max.y) :
        ImVec2(text_rect.Max.x - rounding, text_rect.Max.y);
    ImVec2 pos_2 = is_horizontal() ? pos_1 - ImVec2(triangle_offset.x, 0.f) : pos_1 - ImVec2(0.f, triangle_offset.y);
    ImVec2 pos_3 = is_horizontal() ? pos_1 - ImVec2(0.f, triangle_offset.y) : pos_1 + ImVec2(triangle_offset.x, 0.f);

    if (is_mirrored) {
        pos_1 = is_horizontal() ?
            ImVec2(text_rect.Max.x - rounding-1, text_rect.Max.y-1) :
            ImVec2(text_rect.Max.x - rounding, text_rect.Min.y);
        pos_2 = is_horizontal() ? pos_1 + ImVec2(triangle_offset.x, 0.f) : pos_1 + ImVec2(0.f, triangle_offset.y);
        pos_3 = is_horizontal() ? pos_1 - ImVec2(0.f, triangle_offset.y) : pos_1 + ImVec2(triangle_offset.x, 0.f);
    }

    ImGui::RenderFrame(text_rect.Min, text_rect.Max, tooltip_bg_color(), true, rounding);
    ImGui::GetCurrentWindow()->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, tooltip_bg_color());
    ImGui::RenderText(text_start + text_padding, label.c_str());
};

void Control::draw_thumb(const ImVec2& center, bool mark/* = false*/)
{
    float thumb_radius = m_draw_opts.thumb_radius();
    bool is_hovered = ImGui::IsMouseHoveringRect({ center.x - thumb_radius, center.y - thumb_radius }, { center.x + thumb_radius, center.y + thumb_radius });
    float radius = is_hovered ? 1.1f * thumb_radius : thumb_radius;

    ImGui::GetCurrentWindow()->DrawList->AddCircleFilled(center, radius, border_color(), 16);
    ImGui::GetCurrentWindow()->DrawList->AddCircleFilled(center, 0.65f * radius, fg_color(), 16);

    if (mark)
      ImGui::GetCurrentWindow()->DrawList->AddCircleFilled(center, 0.4f * radius, border_color(), 16);
}

void Control::apply_regions(int higher_pos, int lower_pos, const ImRect& draggable_region)
{
    ImVec2 mid = draggable_region.GetCenter();
    float thumb_radius = m_draw_opts.thumb_radius();

    // set slideable region
    m_regions.higher_slideable_region = is_horizontal() ?
        ImRect(draggable_region.Min + ImVec2(m_draw_lower_thumb ? thumb_radius : 0, 0), draggable_region.Max) :
        ImRect(draggable_region.Min, draggable_region.Max - ImVec2(0, m_combine_thumbs ? 0 : thumb_radius));
    m_regions.lower_slideable_region = is_horizontal() ?
        ImRect(draggable_region.Min, draggable_region.Max - ImVec2(thumb_radius, 0)) :
        ImRect(draggable_region.Min + ImVec2(0, thumb_radius), draggable_region.Max);

    // initialize the thumbs.
    float higher_thumb_pos = position_in_rect(higher_pos, m_regions.higher_slideable_region);
    m_regions.higher_thumb = is_horizontal() ?
        ImRect(higher_thumb_pos - thumb_radius, mid.y - thumb_radius, higher_thumb_pos + thumb_radius, mid.y + thumb_radius) :
        ImRect(mid.x - thumb_radius, higher_thumb_pos - thumb_radius, mid.x + thumb_radius, higher_thumb_pos + thumb_radius);

    float lower_thumb_pos = position_in_rect(lower_pos, m_regions.lower_slideable_region);
    m_regions.lower_thumb = is_horizontal() ?
        ImRect(lower_thumb_pos - thumb_radius, mid.y - thumb_radius, lower_thumb_pos + thumb_radius, mid.y + thumb_radius) :
        ImRect(mid.x - thumb_radius, lower_thumb_pos - thumb_radius, mid.x + thumb_radius, lower_thumb_pos + thumb_radius);
}

void Control::check_and_correct_thumbs(int* higher_pos, int* lower_pos)
{
    if (!m_draw_lower_thumb || m_combine_thumbs)
        return;

    ImVec2 higher_thumb_center = m_regions.higher_thumb.GetCenter();
    ImVec2 lower_thumb_center  = m_regions.lower_thumb.GetCenter();
    float thumb_radius = m_draw_opts.thumb_radius();

    float higher_thumb_center_pos = is_horizontal() ? higher_thumb_center.x : higher_thumb_center.y;
    float lower_thumb_center_pos = is_horizontal() ? lower_thumb_center.x  : lower_thumb_center.y;

    if (is_horizontal()) {
        if (lower_thumb_center_pos + thumb_radius > higher_thumb_center_pos) {
            if (m_selection == SelectedSlider::Higher) {
                m_regions.higher_thumb = m_regions.lower_thumb;
                m_regions.higher_thumb.TranslateX(thumb_radius);
                *lower_pos = *higher_pos;
            }
            else {
                m_regions.lower_thumb = m_regions.higher_thumb;
                m_regions.lower_thumb.TranslateX(-thumb_radius);
                *higher_pos = *lower_pos;
            }
        }
    }
    else {
        if (higher_thumb_center_pos + thumb_radius > lower_thumb_center_pos) {
            if (m_selection == SelectedSlider::Higher) {
                m_regions.lower_thumb = m_regions.higher_thumb;
                m_regions.lower_thumb.TranslateY(thumb_radius);
                *lower_pos = *higher_pos;
            }
            else {
                m_regions.higher_thumb = m_regions.lower_thumb;
                m_regions.higher_thumb.TranslateY(-thumb_radius);
                *higher_pos = *lower_pos;
            }
        }
    }
}

static bool lclicked_on_thumb(ImGuiID id, const ImRect& region,
                             const ImS32 v_min, const ImS32 v_max,
                             const ImRect& thumb, ImGuiSliderFlags flags = 0)
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();

    if (context.ActiveId == id && context.ActiveIdSource == ImGuiInputSource_Mouse &&
        io.MouseReleased[0]) {
        ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;

        ImS32 v_range = (v_min < v_max ? v_max - v_min : v_min - v_max);
        float region_usable_sz = (region.Max[axis] - region.Min[axis]);
        float region_usable_pos_min = region.Min[axis];
        float region_usable_pos_max = region.Max[axis];

        float mouse_abs_pos = io.MousePos[axis];
        float mouse_pos_ratio = (region_usable_sz > 0.0f) ? ImClamp((mouse_abs_pos - region_usable_pos_min) / region_usable_sz, 0.0f, 1.0f) : 0.0f;
        if (axis == ImGuiAxis_Y)
            mouse_pos_ratio = 1.0f - mouse_pos_ratio;

        ImS32 v_new = v_min + (ImS32)(v_range * mouse_pos_ratio + 0.5f);

        // Output thumb position so it can be displayed by the caller
        ImS32 v_clamped = (v_min < v_max) ? ImClamp(v_new, v_min, v_max) : ImClamp(v_new, v_max, v_min);
        float thumb_pos_ratio = v_range != 0 ? ((float)(v_clamped - v_min) / (float)v_range) : 0.0f;
        thumb_pos_ratio = axis == ImGuiAxis_Y ? 1.0f - thumb_pos_ratio : thumb_pos_ratio;
        float thumb_pos = region_usable_pos_min + (region_usable_pos_max - region_usable_pos_min) * thumb_pos_ratio;

        ImVec2 new_thumb_center = axis == ImGuiAxis_Y ? ImVec2(thumb.GetCenter().x, thumb_pos) : ImVec2(thumb_pos, thumb.GetCenter().y);
        if (thumb.Contains(new_thumb_center))
            return true;
    }

    return false;
}

static bool behavior(ImGuiID id, const ImRect& region,
                     const ImS32 v_min, const ImS32 v_max,
                     ImS32* out_value, ImRect* out_thumb,
                     bool& is_dragging,
                     ImGuiSliderFlags flags = 0,
                     bool change_on_mouse_move = false)
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;

    ImVec2 thumb_sz = out_thumb->GetSize();
    ImS32 v_range = (v_min < v_max ? v_max - v_min : v_min - v_max);
    float region_usable_sz = (region.Max[axis] - region.Min[axis]);
    float region_usable_pos_min = region.Min[axis];
    float region_usable_pos_max = region.Max[axis];

    float mouse_abs_pos = io.MousePos[axis];
    float mouse_pos_ratio = (region_usable_sz > 0.0f) ? ImClamp((mouse_abs_pos - region_usable_pos_min) / region_usable_sz, 0.0f, 1.0f) : 0.0f;
    if (axis == ImGuiAxis_Y)
        mouse_pos_ratio = 1.0f - mouse_pos_ratio;

    // Process interacting with the slider
    ImS32 v_new = *out_value;
    bool value_changed = false;
    // wheel behavior
    ImRect mouse_wheel_responsive_region;
    if (axis == ImGuiAxis_X)
        mouse_wheel_responsive_region = ImRect(region.Min - ImVec2(thumb_sz.x / 2.0f, 0), region.Max + ImVec2(thumb_sz.x / 2.0f, 0));
    if (axis == ImGuiAxis_Y)
        mouse_wheel_responsive_region = ImRect(region.Min - ImVec2(0.0f, thumb_sz.y), region.Max + ImVec2(0.0f, thumb_sz.y));
    if (ImGui::ItemHoverable(mouse_wheel_responsive_region, id, ImGuiItemFlags_AllowDuplicateId)) {
        if (change_on_mouse_move)
            v_new = v_min + (ImS32)(v_range * mouse_pos_ratio + 0.5f);
        else {
            float mw = io.MouseWheel;
#if defined(__APPLE__)
            if (mw > 0.f) mw = 1.0f;
            if (mw < 0.f) mw = -1.0f;
#endif // __APPLE__
            float accer = io.KeyCtrl || io.KeyShift ? 5.0f : 1.0f;
            v_new = ImClamp(*out_value + (ImS32)(mw * accer), v_min, v_max);
        }
    }

    // drag behavior
    if (context.ActiveId == id) {
        if (context.ActiveIdSource == ImGuiInputSource_Mouse) {
            if (io.MouseReleased[0]) {
                ImGui::ClearActiveID();
                is_dragging = false;
            }
            if (io.MouseDown[0])
                v_new = v_min + (ImS32)(v_range * mouse_pos_ratio + 0.5f);
        }
    }

    // apply result, output value
    if (*out_value != v_new) {
        *out_value = v_new;
        value_changed = true;
    }

    // Output thumb position so it can be displayed by the caller
    ImS32 v_clamped = (v_min < v_max) ? ImClamp(*out_value, v_min, v_max) : ImClamp(*out_value, v_max, v_min);
    float thumb_pos_ratio = (v_range != 0) ? (float)(v_clamped - v_min) / (float)v_range : 0.0f;
    thumb_pos_ratio = axis == ImGuiAxis_Y ? 1.0f - thumb_pos_ratio : thumb_pos_ratio;
    float thumb_pos = region_usable_pos_min + (region_usable_pos_max - region_usable_pos_min) * thumb_pos_ratio;

    ImVec2 new_thumb_center = axis == ImGuiAxis_Y ? ImVec2(out_thumb->GetCenter().x, thumb_pos) : ImVec2(thumb_pos, out_thumb->GetCenter().y);
    *out_thumb = ImRect(new_thumb_center - thumb_sz * 0.5f, new_thumb_center + thumb_sz * 0.5f);

    return value_changed;
}

bool Control::draw_slider(int* higher_pos, int* lower_pos, const std::string& higher_label, const std::string& lower_label,
    const ImVec2& pos, const ImVec2& size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGuiID id = window->GetID(m_name.c_str());

    ImGui::ItemSize(size);

    // get slider groove size
    ImRect groove = m_draw_opts.groove(pos, size, is_horizontal());

    // get active(draggable) region.
    ImRect draggable_region = m_draw_opts.draggable_region(groove, is_horizontal());

    if (m_is_dragging || (ImGui::ItemHoverable(draggable_region, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseDown[0])) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        m_is_dragging = true;
    }

    // set slideable regions and thumbs.
    apply_regions(*higher_pos, *lower_pos, draggable_region);

    // select and mark higher thumb by default
    if (m_selection == SelectedSlider::Undefined)
        m_selection = SelectedSlider::Higher;

    // Processing interacting

    if (ImGui::ItemHoverable(m_regions.higher_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[0])
        m_selection = SelectedSlider::Higher;

    if (m_draw_lower_thumb && !m_combine_thumbs &&
        ImGui::ItemHoverable(m_regions.lower_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[0])
        m_selection = SelectedSlider::Lower;

    {
        // detect left click on selected thumb
        const ImRect& active_thumb = (m_selection == SelectedSlider::Higher) ? m_regions.higher_thumb : m_regions.lower_thumb;
        if (ImGui::ItemHoverable(active_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[0]) {
            m_active_thumb = active_thumb;
            m_suppress_process_behavior = true;
        }
        else if (ImGui::ItemHoverable(active_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseReleased[0]) {
            const ImRect& slideable_region = (m_selection == SelectedSlider::Higher) ? m_regions.higher_slideable_region : m_regions.lower_slideable_region;
            if (lclicked_on_thumb(id, slideable_region, m_min_pos, m_max_pos, m_active_thumb, m_flags)) {
                m_suppress_process_behavior = true;
                m_lclick_on_selected_thumb = true;
            }
        }

        if (ImGui::ItemHoverable(active_thumb, id, ImGuiItemFlags_AllowDuplicateId) && ImGui::IsMouseDragging(0))
            // invalidate active thumb clicking
            m_active_thumb = ImRect();

        // detect left click on hovered region
        if (ImGui::ItemHoverable(m_hovered_region, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[0]) {
            // clear active ID to avoid a process of behavior()
            if (context.ActiveId == id && context.ActiveIdSource == ImGuiInputSource_Mouse)
                ImGui::ClearActiveID();
        }
        else if (ImGui::ItemHoverable(m_hovered_region, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseReleased[0]) {
            const ImRect& slideable_region = (m_selection == SelectedSlider::Higher) ? m_regions.higher_slideable_region : m_regions.lower_slideable_region;
            if (lclicked_on_thumb(id, slideable_region, m_min_pos, m_max_pos, m_hovered_region, m_flags))
                m_lclick_on_hovered_pos = true;
        }
    }

    // update thumb position
    bool pos_changed = false;
    if (!m_suppress_process_behavior) {
        if (m_selection == SelectedSlider::Higher)
            pos_changed = behavior(id, m_regions.higher_slideable_region, m_min_pos, m_max_pos,
                higher_pos, &m_regions.higher_thumb, m_is_dragging, m_flags);
        else if (m_draw_lower_thumb && !m_combine_thumbs)
            pos_changed = behavior(id, m_regions.lower_slideable_region, m_min_pos, m_max_pos,
                lower_pos, &m_regions.lower_thumb, m_is_dragging, m_flags);

        // check thumbs poss and correct them if needed
        check_and_correct_thumbs(higher_pos, lower_pos);
    }
    const ImRect& slideable_region = (m_selection == SelectedSlider::Higher) ? m_regions.higher_slideable_region : m_regions.lower_slideable_region;
    const ImRect& active_thumb = (m_selection == SelectedSlider::Higher) ? m_regions.higher_thumb : m_regions.lower_thumb;

    ImRect mouse_pos_rc = active_thumb;
    std::string move_label;

    ImRect move_bb(pos, pos + size);
    move_bb.Min.x += left_dummy_sz().x;
    if (!pos_changed && ImGui::ItemHoverable(move_bb, id, ImGuiItemFlags_AllowDuplicateId) && !ImGui::IsMouseDragging(0)) {
        auto sl_region = slideable_region;
        if (!is_horizontal() && m_draw_opts.has_ruler)
            sl_region.Max.x += m_draw_opts.dummy_sz().x;
        behavior(id, sl_region, m_min_pos, m_max_pos,
                 &m_mouse_pos, &mouse_pos_rc, m_is_dragging, m_flags, true);
        move_label = label_on_move(m_mouse_pos);
    }

    // detect right click on selected thumb
    if (ImGui::ItemHoverable(active_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[1])
        m_rclick_on_selected_thumb = true;
    if ((!ImGui::ItemHoverable(active_thumb, id, ImGuiItemFlags_AllowDuplicateId) && io.MouseClicked[1]) ||
        io.MouseClicked[0])
        m_rclick_on_selected_thumb = false;

    ImRect item_bb(pos, pos + size);
    if (m_suppress_process_behavior && ImGui::ItemHoverable(item_bb, id, ImGuiItemFlags_AllowDuplicateId) && ImGui::IsMouseDragging(0))
        m_suppress_process_behavior = false;

    // render slider

    ImVec2 higher_thumb_center = m_regions.higher_thumb.GetCenter();
    ImVec2 lower_thumb_center  = m_regions.lower_thumb.GetCenter();

    ImRect scroll_line = m_draw_opts.slider_line(slideable_region, higher_thumb_center, lower_thumb_center, is_horizontal());

    if (m_cb_extra_draw)
        m_cb_extra_draw(slideable_region);

    // draw background
    draw_background(slideable_region);
    // draw scroll line
    draw_scroll_line(m_combine_thumbs ? groove : scroll_line, slideable_region);

    // draw thumbs with label
    draw_thumb(higher_thumb_center, m_selection == SelectedSlider::Higher && m_draw_lower_thumb);
    draw_label(higher_label, m_regions.higher_thumb);

    if (m_draw_lower_thumb && !m_combine_thumbs) {
        ImVec2 text_size = ImGui::CalcTextSize(lower_label.c_str()) + m_draw_opts.text_padding() * 2.f;
        bool mirror_label = is_horizontal() ? *lower_pos == 0 || (higher_thumb_center.x - lower_thumb_center.x < text_size.x) :
                                              (lower_thumb_center.y - higher_thumb_center.y < text_size.y);
        draw_thumb(lower_thumb_center, m_selection == SelectedSlider::Lower);
        draw_label(lower_label, m_regions.lower_thumb, mirror_label);
    }

    // draw label on mouse move
    if (m_show_move_label) {
        bool mirrored = move_label == lower_label && *lower_pos == 0;
        draw_label(move_label, mouse_pos_rc, mirrored, true);
    }

    return pos_changed;
}

} // namespace Slic3r::App::Imgui::DoubleSlider
