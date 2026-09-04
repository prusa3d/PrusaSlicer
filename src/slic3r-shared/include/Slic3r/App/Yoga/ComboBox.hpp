#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/RevertableControl.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

class Validator;

class ComboBox : public Yoga::Item, public Yoga::RevertableControl
{
public:
    struct Callbacks
    {
        /**
         * @brief text_edited is fired only after editing is finished (e.g. Enter/ESC or item lost
         * it's focus) This is due to optional validator which is invoked just before this callback
         */
        std::function<void()> text_edited{nullptr};
        std::function<void(int index)> selection_changed{nullptr};
    };

    explicit ComboBox(const std::string& name = {});
    ComboBox(std::initializer_list<std::string> items = {}, const std::string& name = {});

    template <std::ranges::range Container>
        requires std::convertible_to<std::ranges::range_value_t<Container>, std::string>
    ComboBox(Container&& data, const std::string& name = {}) : ComboBox(name)
    {
        if constexpr (std::is_rvalue_reference_v<Container&&>) {
            m_items.insert(
                m_items.end(),
                std::make_move_iterator(std::begin(data)),
                std::make_move_iterator(std::end(data))
            );
        } else {
            m_items.insert(m_items.end(), std::begin(data), std::end(data));
        }
        set_current_index(0);
    }

    ~ComboBox();

    void render(const Vec2f& pos, const Vec2f& size) override;

    Callbacks& callbacks();

    Tooltip& tooltip();

    const std::vector<std::string>& items() const;
    void set_items(const std::vector<std::string>& items);

    int current_index() const;
    void set_current_index(int current_index);
    std::string current_label() const;
    /**
     * @note this seter can only be used for editable comboboxes, otherwise use set_current_index
     */
    void set_current_label(const std::string& current_label);

    bool editable() const;
    void set_editable(bool editable);

    ImGuiComboFlags flags() const;
    void set_flags(ImGuiComboFlags flags);

    Validator* validator() const;
    void set_validator(std::unique_ptr<Validator> validator);

    void set_default(int default_index);
    bool is_changed_value() const override;
    void reset() override;

    const std::string& override_label() const;
    void set_override_label(const std::string& override_label);

    Render::ImguiFontType label_font_type() const;
    void set_label_font_type(Render::ImguiFontType label_font_type);

    const ImColor& label_color() const;
    void set_label_color(const ImColor& label_color);

protected:
    Vec2f get_item_size() override;

private:
    ////////////////////// Copied from ImGui //////////////////////////////////////
    bool YGBeginCombo(
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
    );
    bool BeginComboPopup(ImGuiID popup_id, const ImRect& bb, ImGuiComboFlags flags);
    ///////////////////////////////////////////////////////////////////////////////

protected:
    std::vector<std::string> m_items;

    Tooltip* m_tooltip = nullptr;

private:
    Callbacks m_callbacks;

    /**
     * holds editable field buffer of current_label
     * std::array because otherwise we would have to set up ImGui callbacks
     * in order to use std::string
     */
    std::array<char, 2'048> m_buffer = {0};
    int m_current_index              = 0;
    int m_default_index              = 0;
    std::string m_current_label;
    ImGuiComboFlags m_flags = 0;
    bool m_editable         = false;
    std::unique_ptr<Validator> m_validator;
    bool m_updated = false;
    bool m_hovered = false;
    std::string m_override_label;
    Render::ImguiFontType m_label_font_type = Render::ImguiFontType::Regular;
    ImColor m_label_color;
};

} // namespace Slic3r::App::Yoga
