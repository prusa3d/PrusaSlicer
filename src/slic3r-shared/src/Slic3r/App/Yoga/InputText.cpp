#include "Slic3r/App/Yoga/InputText.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"

#include <Slic3r/Log.hpp>

namespace Slic3r::App::Yoga {

struct InputTextCallback_UserData
{
    std::string* Str;
    ImGuiInputTextCallback ChainCallback;
    void* ChainCallbackUserData;
};

/**
 * @note copied from imgui_stdlib as we would like to have a backend in std::string
 */
static int InputTextCallback(ImGuiInputTextCallbackData* data)
{
    InputTextCallback_UserData* user_data = (InputTextCallback_UserData*) data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we
        // need to set them back to what we want.
        std::string* str = user_data->Str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = str->data();
    } else if (user_data->ChainCallback) {
        // Forward to user callback, if any
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}

/**
 * @note copied from imgui_stdlib as we would like to have a backend in std::string, but also
 * include size argument
 */
static bool YInputText(
    const char* label,
    const char* hint,
    std::string* str,
    ImVec2 size,
    ImGuiInputTextFlags flags,
    ImGuiInputTextCallback callback,
    void* user_data
)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str                   = str;
    cb_user_data.ChainCallback         = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return ImGui::InputTextEx(
        label,
        hint,
        str->data(),
        str->capacity() + 1,
        size,
        flags,
        InputTextCallback,
        &cb_user_data
    );
}

InputText::InputText(const std::string& name)
{
    set_object_name(name.empty() ? "InputText" : name);
}

void InputText::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    {
        Imgui::ScopedStyleColors colors(
            {{ImGuiCol_FrameBg, IM_COL32_BLACK_TRANS},
             {ImGuiCol_Border, IM_COL32_BLACK_TRANS},
             {ImGuiCol_BorderShadow, IM_COL32_BLACK_TRANS}}
        );

        ImGui::SetCursorScreenPos(to_im(pos));
        if (m_resizable) {
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(
                    YGFloatIsUndefined(min_width().value) ? 0 : min_width().value,
                    YGFloatIsUndefined(min_heigth().value) ? 0 : min_heigth().value
                ),
                ImVec2(
                    YGFloatIsUndefined(max_width().value) ? FLT_MAX : max_width().value,
                    YGFloatIsUndefined(max_height().value) ? FLT_MAX : max_height().value
                )
            );
            const std::string child_id = object_name() + "_child";
            ImGui::BeginChild(child_id.c_str(), to_im(size), m_child_flags, m_window_flags);

            // Each frame ImGui can have a different height, this will update Yoga size
            set_height(ImGui::GetWindowSize().y);
        }
        const std::string id = "###" + object_name();

        ImU32 text_color = ImGui::GetColorU32(enabled() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::PushStyleColor(ImGuiCol_Text, text_color);

        ImFont* font = m_imgui_render ? m_imgui_render->font(m_font_type) : nullptr;
        ImGui::PushFont(font, GImGui->FontSizeBase);

        ImGuiID input_id = ImGui::GetID(id.c_str());

        // Query before rendering
        bool activated = ImGui::GetActiveID() == input_id;
        bool changed   = false;

        if (m_request_focus) {
            m_request_focus = false;
            ImGui::SetKeyboardFocusHere();
        }

        const bool is_disabled = !enabled();
        if (is_disabled) {
            ImGui::BeginDisabled(true);
        }
        if (!activated && !m_override_label.empty()) {
            // We are overriding hint label
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, text_color);
            std::string empty;
            changed = YInputText(
                id.c_str(),
                m_override_label.c_str(),
                &empty,
                to_im(size),
                m_flags,
                {},
                nullptr
            );
            ImGui::PopStyleColor();
        } else {
            changed =
                YInputText(id.c_str(), m_hint.c_str(), &m_text, to_im(size), m_flags, {}, nullptr);
        }
        if (ImGui::IsItemActivated() && m_validator) {
            m_text = m_validator->string_without_unit();

            if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID())) {
                state->ReloadUserBufAndSelectAll();
            }
        }

        if (is_disabled) {
            ImGui::EndDisabled();
        }

        bool hovered = ImGui::IsItemHovered();
        if (m_hovered != hovered) {
            m_hovered = hovered;
            hovered_updated_internal();
            if (m_callbacks.hovered_changed) {
                m_callbacks.hovered_changed(m_hovered);
            }
        }

        ImGui::PopFont();

        ImGui::PopStyleColor();

        m_updated |= changed;

        if (m_updated) {
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                m_updated   = false;
                m_has_focus = false;
                if (m_validator) {
                    m_text = m_validator->process(m_text);
                }
                if (m_callbacks.update_revert_button) {
                    m_callbacks.update_revert_button();
                }
                if (m_callbacks.text_edited) {
                    m_callbacks.text_edited();
                }
                if (m_callbacks.focus_lost) {
                    m_callbacks.focus_lost();
                }
            }

            if (ImGui::IsItemEdited() && m_callbacks.text_changed) {
                m_callbacks.text_changed();
            }
        }

        if (m_has_focus && ImGui::IsItemDeactivated()) {
            if (m_validator) {
                m_text = m_validator->string_with_unit();

                if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID())) {
                    state->ReloadUserBufAndSelectAll();
                }
            }

            m_has_focus = false;
            if (m_callbacks.focus_lost) {
                m_callbacks.focus_lost();
            }
        }
        if (!m_has_focus && ImGui::IsItemActivated()) {
            m_has_focus = true;
            if (m_callbacks.focus_gained) {
                m_callbacks.focus_gained();
            }
        }

        if (m_active
            && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            && m_callbacks.text_entered)
        {
            m_callbacks.text_entered();
        }

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        bool active         = ImGui::GetActiveID() == window->GetID(id.c_str());
        if (m_active != active) {
            m_active = active;
            active_updated_internal();
            if (m_callbacks.active_changed) {
                m_callbacks.active_changed(active);
            }
        }

        if (m_resizable) {
            ImGui::EndChild();
        }
    }

    render_item_end(pos, size);
}

InputText::Callbacks& InputText::callbacks()
{
    return m_callbacks;
}

const std::string& InputText::text() const
{
    return m_text;
}

void InputText::set_text(const std::string& text)
{
    if (m_text == text) {
        return;
    }

    if (validator()) {
        m_text = validator()->process(text);
    } else {
        m_text = text;
    }
    if (m_callbacks.update_revert_button) {
        m_callbacks.update_revert_button();
    }
    if (m_callbacks.text_changed) {
        m_callbacks.text_changed();
    }
}

ImGuiInputTextFlags InputText::flags() const
{
    return m_flags;
}

void InputText::set_flags(ImGuiInputTextFlags flags)
{
    m_flags = flags;
}

const std::string& InputText::hint() const
{
    return m_hint;
}

void InputText::set_hint(const std::string& hint)
{
    m_hint = hint;
}

Validator* InputText::validator() const
{
    return m_validator.get();
}

void InputText::set_validator(std::unique_ptr<Validator> validator)
{
    m_validator = std::move(validator);
}

bool InputText::active() const
{
    return m_active;
}

bool InputText::has_focus() const
{
    return m_has_focus;
}

void InputText::request_focus()
{
    m_request_focus = true;
    set_style_dirty(); // request a new render loop
}

Vec2f InputText::get_item_size()
{
    return {40, ImGui::GetTextLineHeight() + GImGui->Style.FramePadding.y * 2.0f};
}

void InputText::hovered_updated_internal() {}

void InputText::active_updated_internal() {}

bool InputText::resizable() const
{
    return m_resizable;
}

void InputText::set_resizable(bool resizable)
{
    m_resizable = resizable;
}

const std::string& InputText::override_label() const
{
    return m_override_label;
}

void InputText::set_override_label(const std::string& override_label)
{
    m_override_label = override_label;
}

bool InputText::hovered() const
{
    return m_hovered;
}

Render::ImguiFontType InputText::font_type() const
{
    return m_font_type;
}

void InputText::set_font_type(Render::ImguiFontType font_type)
{
    m_font_type = font_type;
}

void InputText::invalidate_min_size_calculation()
{
    Item::invalidate_min_size_calculation();
}

} // namespace Slic3r::App::Yoga
