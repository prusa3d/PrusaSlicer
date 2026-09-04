#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

class Validator;
class InputTextField;

/**
 * @brief The InputText class is a text input class which intentionally provides
 * NO decoration. The class itself is intended to be included in composed input
 * blocks such as InputTextField or InputTextArea etc.
 * The reasoning behind is that we can use Yoga layout while decorating text
 * input fields.
 */
class InputText : public Item
{
public:
    struct Callbacks
    {
        std::function<void(bool hovered)> hovered_changed{nullptr};
        std::function<void(bool active)> active_changed{nullptr};
        /**
         * @brief text_edited is fired only after editing is finished (e.g. Enter/ESC or item lost
         * it's focus) This is due to optional validator which is invoked just before this callback
         */
        std::function<void()> text_edited{nullptr};

        /**
         * @brief text_changed is fired on every changes
         */
        std::function<void()> text_changed{nullptr};

        /**
         * @brief text_entered is fired when user press key Enter/Return
         */
        std::function<void()> text_entered{nullptr};

        /**
         * @brief focus_lost, input text lost focus either by clicking outside, finishing editation,
         * or perhaps hiting ESC
         */
        std::function<void()> focus_lost{};
        /**
         * @brief focus_gained, input text got focus, most probably by clicking on it
         */
        std::function<void()> focus_gained{};

    protected:
        /**
         * @brief update_revert_button is used only by fiend InputTextField class to prodagate update_revert_button here
         */
        std::function<void()> update_revert_button{nullptr};
        friend class InputText;
        friend class InputTextField;
    };

    explicit InputText(const std::string& name = {});

    void render(const Vec2f& pos, const Vec2f& size) override;

    Callbacks& callbacks();

    /**
     * @note We assume UTF-8 encoding
     */
    const std::string& text() const;
    void set_text(const std::string& text);

    ImGuiInputTextFlags flags() const;
    void set_flags(ImGuiInputTextFlags flags);

    const std::string& hint() const;
    void set_hint(const std::string& hint);

    Validator* validator() const;
    void set_validator(std::unique_ptr<Validator> validator);

    Render::ImguiFontType font_type() const;
    void set_font_type(Render::ImguiFontType font_type);

    const std::string& override_label() const;
    void set_override_label(const std::string& override_label);

    bool hovered() const;
    /**
     * @return true if InputText is currently active (focused/edited)
     */
    bool active() const;

    bool has_focus() const;
    void request_focus();

    bool resizable() const;
    void set_resizable(bool resizable);

    void invalidate_min_size_calculation();

protected:
    Vec2f get_item_size() override;

    virtual void hovered_updated_internal();
    virtual void active_updated_internal();

private:
    Callbacks m_callbacks;

    /**
     * @note IntValidator and DoubleValidator are intended to be used with
     * ImGuiInputTextFlags_CharsDecimal you need to set that flag manually
     */
    std::unique_ptr<Validator> m_validator;

    std::string m_text;
    std::string
        m_override_label; ///< If set, this text will display instead of m_text (but it's not real)
    ImGuiInputTextFlags m_flags = 0;
    std::string m_hint;
    Render::ImguiFontType m_font_type = Render::ImguiFontType::Regular;

    ///////////// These are for resizable begin child //////////////
    ImGuiChildFlags m_child_flags   = ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY;
    ImGuiWindowFlags m_window_flags = 0;
    ////////////////////////////////////////////////////////////////

    bool m_active        = false;
    bool m_updated       = false; ///< Input was edited but not yet lost focus
    bool m_request_focus = false;
    bool m_has_focus     = false;
    bool m_hovered       = false;
    bool m_resizable     = false; ///< If true, height of the input text can be adjusted
};

} // namespace Slic3r::App::Yoga
