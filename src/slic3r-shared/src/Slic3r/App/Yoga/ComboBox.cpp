#include "Slic3r/App/Yoga/ComboBox.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

static float CalcMaxPopupHeightFromItemCount(int items_count)
{
    ImGuiContext& g = *GImGui;
    if (items_count <= 0)
        return FLT_MAX;
    return (g.FontSize + g.Style.ItemSpacing.y) * items_count
        - g.Style.ItemSpacing.y
        + (g.Style.WindowPadding.y * 2);
}

/**
 * @note copied from imgui internals, we need our custom styling
 */
bool ComboBox::YGBeginCombo(
    const char* label,
    const char* preview_value,
    ImVec2 size_arg,
    ImGuiComboFlags flags,
    bool editable,
    bool enabled,
    char* buffer,
    int buf_size,
    bool& edited,
    Validator* validator,
    ComboBox::Callbacks& callbacks,
    bool& hovered,
    ImFont* label_font,
    const ImColor& label_color
)
{
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

    ImGuiContext& g     = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGuiNextWindowDataFlags backup_next_window_data_flags = g.NextWindowData.HasFlags;
    g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume those values
    if (window->SkipItems)
        return false;

    const ImGuiStyle& style = g.Style;
    const ImGuiID id        = window->GetID(label);
    IM_ASSERT(
        (flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview))
        != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)
    ); // Can't use both flags together
    if (flags & ImGuiComboFlags_WidthFitPreview)
        IM_ASSERT(
            (flags & (ImGuiComboFlags_NoPreview | (ImGuiComboFlags) ImGuiComboFlags_CustomPreview))
            == 0
        );

    if (editable) {
        ImGui::SetNextItemAllowOverlap();
    }

    const float arrow_size =
        (flags & ImGuiComboFlags_NoArrowButton) ? 0.0f : ImGui::GetFrameHeight();
    const float preview_width =
        ((flags & ImGuiComboFlags_WidthFitPreview) && (preview_value != NULL)) ?
        ImGui::CalcTextSize(preview_value, NULL, true).x :
        0.0f;
    const float w = (flags & ImGuiComboFlags_NoPreview) ?
        arrow_size :
        ((flags & ImGuiComboFlags_WidthFitPreview) ?
             (arrow_size + preview_width + style.FramePadding.x * 2.0f) :
             ImGui::CalcItemWidth());

    const ImVec2 frame_size = ImGui::CalcItemSize(size_arg, w, style.FramePadding.y * 2.0f);

    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + frame_size);
    const ImRect total_bb(bb.Min, bb.Max);
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id, &bb))
        return false;

    // Open on click
    bool held;
    bool pressed           = enabled && ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
    bool popup_open        = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (pressed && !popup_open) {
        ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
        popup_open = true;
    }

    // Render shape
    const ImU32 frame_col = ImGui::GetColorU32(
        hovered ? ImGuiCol_FrameBgHovered : (popup_open ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg)
    );
    const float value_x2 = ImMax(bb.Min.x, bb.Max.x - arrow_size);
    ImGui::RenderNavCursor(bb, id);
    if (!(flags & ImGuiComboFlags_NoPreview))
        window->DrawList->AddRectFilled(
            bb.Min,
            bb.Max,
            frame_col,
            style.FrameRounding,
            popup_open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll
        );
    if (!(flags & ImGuiComboFlags_NoArrowButton)) {
        ImU32 text_col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        if (value_x2 + arrow_size - style.FramePadding.x <= bb.Max.x) {
            const float w = arrow_size - 2.f * style.FramePadding.x;
            const float h = g.FontSize;
            YGRenderArrow(
                window->DrawList,
                ImVec2(value_x2 + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
                ImVec2(w, h),
                text_col,
                ImGuiDir_Down,
                1.0f
            );
        }
    }
    ImGui::RenderFrameBorder(bb.Min, bb.Max, style.FrameRounding);

    // Custom preview
    if (flags & ImGuiComboFlags_CustomPreview) {
        g.ComboPreviewData.PreviewRect = ImRect(bb.Min.x, bb.Min.y, value_x2, bb.Max.y);
        IM_ASSERT(preview_value == NULL || preview_value[0] == 0);
        preview_value = NULL;
    }

    if (editable) {
        ImGui::SetCursorScreenPos(cursor_pos);
        const std::string lab = std::string(label) + "##inputtext";
        edited |= ImGui::InputTextEx(
            lab.c_str(),
            "",
            buffer,
            buf_size,
            ImVec2(value_x2 - bb.Min.x, bb.Max.y - bb.Min.y),
            ImGuiInputTextFlags_AutoSelectAll,
            nullptr
        );
        if (ImGui::IsItemActivated() && m_validator) {
            std::string without_unit = m_validator->string_without_unit();
            strncpy(
                buffer,
                without_unit.data(),
                std::min(without_unit.size(), static_cast<size_t>(buf_size))
            );
            buffer[std::min(without_unit.size(), static_cast<size_t>(buf_size) - 1)] = '\0';

            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID())) {
                state->ReloadUserBufAndSelectAll();
            }
        }

        if (edited && ImGui::IsItemDeactivatedAfterEdit()) {
            edited = false;
            if (validator) {
                std::string text(buffer);
                text = validator->process(text);
                strncpy(buffer, text.data(), std::min(text.size(), static_cast<size_t>(buf_size)));
                buffer[std::min(text.size(), static_cast<size_t>(buf_size) - 1)] = '\0';

                if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID())) {
                    state->ReloadUserBufAndSelectAll();
                }
            }
            if (callbacks.text_edited) {
                callbacks.text_edited();
            }
        } else if (ImGui::IsItemDeactivated() && validator) {
            std::string text = m_validator->string_with_unit();
            strncpy(buffer, text.data(), std::min(text.size(), static_cast<size_t>(buf_size)));
            buffer[std::min(text.size(), static_cast<size_t>(buf_size) - 1)] = '\0';

            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID())) {
                state->ReloadUserBufAndSelectAll();
            }
        }
    } else {
        // Render preview and label
        if (preview_value != NULL && !(flags & ImGuiComboFlags_NoPreview)) {
            ImGui::PushFont(label_font, GImGui->FontSizeBase);
            ImGui::PushStyleColor(ImGuiCol_Text, label_color.Value);
            if (g.LogEnabled)
                ImGui::LogSetNextTextDecoration("{", "}");
            ImGui::RenderTextClipped(
                bb.Min + style.FramePadding,
                ImVec2(value_x2, bb.Max.y),
                preview_value,
                NULL,
                NULL
            );
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }

    if (!popup_open)
        return false;

    g.NextWindowData.HasFlags = backup_next_window_data_flags;
    return BeginComboPopup(popup_id, bb, flags);
}

bool ComboBox::BeginComboPopup(ImGuiID popup_id, const ImRect& bb, ImGuiComboFlags flags)
{
    ImGuiContext& g = *GImGui;
    if (!ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None)) {
        g.NextWindowData.ClearFlags();
        return false;
    }

    // Set popup size
    float w = bb.GetWidth();
    if (g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint) {
        g.NextWindowData.SizeConstraintRect.Min.x =
            ImMax(g.NextWindowData.SizeConstraintRect.Min.x, w);
    } else {
        int popup_max_height_in_items = -1;
        if ((flags & ImGuiComboFlags_HeightMask_) == 0)
            popup_max_height_in_items = 12; // Default size
        IM_ASSERT(
            (flags & ImGuiComboFlags_HeightMask_) == 0
            || ImIsPowerOfTwo(flags & ImGuiComboFlags_HeightMask_)
        ); // Only one
        if (flags & ImGuiComboFlags_HeightRegular)
            popup_max_height_in_items = 8;
        else if (flags & ImGuiComboFlags_HeightSmall)
            popup_max_height_in_items = 4;
        else if (flags & ImGuiComboFlags_HeightLarge)
            popup_max_height_in_items = 20;
        ImVec2 constraint_min(0.0f, 0.0f), constraint_max(FLT_MAX, FLT_MAX);
        if ((g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize) == 0
            || g.NextWindowData.SizeVal.x
                <= 0.0f) // Don't apply constraints if user specified a size
            constraint_min.x = w;
        if ((g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize) == 0
            || g.NextWindowData.SizeVal.y <= 0.0f)
            constraint_max.y = CalcMaxPopupHeightFromItemCount(popup_max_height_in_items);
        ImGui::SetNextWindowSizeConstraints(constraint_min, constraint_max);
    }

    // This is essentially a specialized version of BeginPopupEx()
    char name[16];
    ImFormatString(
        name,
        IM_COUNTOF(name),
        "##Combo_%02d",
        g.BeginComboDepth
    ); // Recycle windows based on depth

    // Set position given a custom constraint (peak into expected window size so we can position it)
    // FIXME: This might be easier to express with an hypothetical SetNextWindowPosConstraints() function?
    // FIXME: This might be moved to Begin() or at least around the same spot where Tooltips and other Popups are calling FindBestWindowPosForPopupEx()?
    if (ImGuiWindow* popup_window = ImGui::FindWindowByName(name))
        if (popup_window->WasActive) {
            // Always override 'AutoPosLastDirection' to not leave a chance for a past value to affect us.
            ImVec2 size_expected               = ImGui::CalcWindowNextAutoFitSize(popup_window);
            popup_window->AutoPosLastDirection = (flags & ImGuiComboFlags_PopupAlignLeft) ?
                ImGuiDir_Left :
                ImGuiDir_Down; // Left = "Below, Toward Left", Down = "Below, Toward Right (default)"
            ImRect r_outer                     = ImGui::GetPopupAllowedExtentRect(popup_window);
            ImVec2 pos                         = ImGui::FindBestWindowPosForPopupEx(
                bb.GetBL(),
                size_expected,
                &popup_window->AutoPosLastDirection,
                r_outer,
                bb,
                ImGuiPopupPositionPolicy_ComboBox
            );
            ImGui::SetNextWindowPos(pos);
        }

    // We don't use BeginPopupEx() solely because we have a custom name string, which we could make an argument to BeginPopupEx()
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_Popup
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVarX(
        ImGuiStyleVar_WindowPadding,
        g.Style.FramePadding.x
    ); // Horizontally align ourselves with the framed text
    bool ret = ImGui::Begin(name, NULL, window_flags);
    ImGui::PopStyleVar();
    if (!ret) {
        ImGui::EndPopup();
        if (
            !g.IO.ConfigDebugBeginReturnValueOnce && !g.IO.ConfigDebugBeginReturnValueLoop
        ) // Begin may only return false with those debug tools activated.
            IM_ASSERT(0); // This should never happen as we tested for IsPopupOpen() above
        return false;
    }
    g.BeginComboDepth++;
    return true;
}

ComboBox::ComboBox(const std::string& name)
{
    m_label_color = m_theme->color_imgui(Platform::Color::Text);
    set_object_name(name.empty() ? "ComboBox" : name);
    m_tooltip = emplace_back<Tooltip>(this, std::string{}, std::string{});
}

ComboBox::ComboBox(std::initializer_list<std::string> items, const std::string& name) :
    ComboBox(name)
{
    m_items = items;
    set_current_index(0);
}

ComboBox::~ComboBox() {}

void ComboBox::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    {
        Imgui::ScopedStyleColors colors({
            {ImGuiCol_FrameBg, m_theme->color_imgui(Platform::Color::Button)},
            {ImGuiCol_FrameBgHovered,
             m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered)},
            {ImGuiCol_FrameBgActive,
             m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered)},
            {ImGuiCol_Text,
             m_theme->color_imgui(
                 Platform::Color::Text,
                 enabled() ? Platform::ColorGroup::Default : Platform::ColorGroup::Disabled
             )},
            {ImGuiCol_Border,
             m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)},
            {ImGuiCol_BorderShadow,
             m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)},
            {ImGuiCol_Header,
             m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)},
        });

        ImGui::SetCursorScreenPos(to_im(pos));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 2.0f);
        // ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);

        const std::string id = "###" + object_name();
        bool new_hovered     = false;
        if (YGBeginCombo(
                id.c_str(),
                m_override_label.empty() ? m_current_label.c_str() : m_override_label.c_str(),
                to_im(size),
                m_flags,
                m_editable,
                enabled(),
                m_buffer.data(),
                m_buffer.size(),
                m_updated,
                m_validator.get(),
                m_callbacks,
                new_hovered,
                m_imgui_render ? m_imgui_render->font(m_label_font_type) : nullptr,
                m_label_color
            ))
        {
            const ImVec2 im_size = to_im(size);
            for (int index = 0; index < static_cast<int>(m_items.size()); ++index) {
                ImGui::PushID(index);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.f, 0.5f));
                if (ImGui::Selectable(
                        m_items.at(index).c_str(),
                        m_override_label.empty() ? index == m_current_index : false,
                        0,
                        im_size
                    ))
                {
                    set_current_index(index);
                    if (m_callbacks.selection_changed) {
                        m_callbacks.selection_changed(index);
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        ImGui::PopStyleVar(1);

        if (m_hovered != new_hovered) {
            m_hovered = new_hovered;
            if (!m_tooltip->text().empty()) {
                m_hovered ? m_tooltip->open() : m_tooltip->close();
            }
        }
    }
    render_item_end(pos, size);
}

Tooltip& ComboBox::tooltip()
{
    return *m_tooltip;
}

ComboBox::Callbacks& ComboBox::callbacks()
{
    return m_callbacks;
}

const std::vector<std::string>& ComboBox::items() const
{
    return m_items;
}

void ComboBox::set_items(const std::vector<std::string>& items)
{
    if (m_items != items) {
        m_items = items;
        set_current_index(0);
    }
}

Vec2f ComboBox::get_item_size()
{
    return {50, ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f};
}

Render::ImguiFontType ComboBox::label_font_type() const
{
    return m_label_font_type;
}

void ComboBox::set_label_font_type(Render::ImguiFontType label_font_type)
{
    m_label_font_type = label_font_type;
}

const ImColor& ComboBox::label_color() const
{
    return m_label_color;
}

void ComboBox::set_label_color(const ImColor& label_color)
{
    m_label_color = label_color;
}

const std::string& ComboBox::override_label() const
{
    return m_override_label;
}

void ComboBox::set_override_label(const std::string& override_label)
{
    m_override_label = override_label;
}

Validator* ComboBox::validator() const
{
    return m_validator.get();
}

void ComboBox::set_validator(std::unique_ptr<Validator> validator)
{
    m_validator = std::move(validator);
}

void ComboBox::set_default(int default_index)
{
    m_default_index = default_index;
    update_revert_button();
}

bool ComboBox::is_changed_value() const
{
    return m_current_index != m_default_index;
}

void ComboBox::reset()
{
    set_current_index(m_default_index);
    if (m_callbacks.selection_changed) {
        m_callbacks.selection_changed(m_default_index);
    }
}

ImGuiComboFlags ComboBox::flags() const
{
    return m_flags;
}

void ComboBox::set_flags(ImGuiComboFlags flags)
{
    m_flags = flags;
}

bool ComboBox::editable() const
{
    return m_editable;
}

void ComboBox::set_editable(bool editable)
{
    m_editable = editable;
}

std::string ComboBox::current_label() const
{
    return m_editable ? std::string(m_buffer.data()) : m_current_label;
}

void ComboBox::set_current_label(const std::string& current_label)
{
    ASSERT(m_editable);
    ASSERT(current_label.size() < 2'048);

    const std::string label = m_validator ? m_validator->process(current_label) : current_label;

    if (m_current_label != label) {
        m_current_label = label;
        strncpy(m_buffer.data(), m_current_label.data(), m_current_label.size());
        m_buffer[m_current_label.size()] = '\0';
    }
}

int ComboBox::current_index() const
{
    return m_current_index;
}

void ComboBox::set_current_index(int current_index)
{
    if (m_items.empty()) {
        m_current_index = -1;
        m_current_label.clear();
        std::fill(m_buffer.begin(), m_buffer.end(), 0);
    } else {
        m_current_index = std::clamp(current_index, 0, static_cast<int>(m_items.size()) - 1);

        const std::string label = m_validator ? m_validator->process(m_items.at(m_current_index)) :
                                                m_items.at(m_current_index);

        if (m_current_label != label) {
            m_current_label = label;
            strncpy(m_buffer.data(), m_current_label.c_str(), m_current_label.size());
            m_buffer[m_current_label.size()] = '\0';
        }
    }
    update_revert_button();
}

} // namespace Slic3r::App::Yoga
