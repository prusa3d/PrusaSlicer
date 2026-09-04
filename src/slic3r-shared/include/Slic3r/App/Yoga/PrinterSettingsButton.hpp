#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class Icon;
class Text;
class LayoutButton;

class PrinterSettingsButton : public RectangleButton
{
public:
    explicit PrinterSettingsButton(const std::string& tooltip = {});

    void set_image(const std::string& image);
    void set_printer_name(const std::string& printer_name, bool is_modified = false);
    void set_preset_name(const std::string& preset_name);
    void set_printing_state(int state);

    void set_visible_printer(bool is_visible);
    std::function<void()>& on_printer();

    void set_visible_cog(bool is_visible);
    std::function<void()>& on_cog();

protected:
    void checked_updated_internal() override;
    void hovered_updated_internal() override;

    virtual void update_btns_visibility();

    LayoutButton* add_button(Render::Icon icon, const std::string& tooltip);
    Icon* add_extra_icon(Render::Icon icon);

protected:
    Icon* m_icon{nullptr};
    Item* m_texts_wrapper{nullptr};
    Item* m_btn_wrapper{nullptr};
    Text* m_preset_name{nullptr};
    LayoutButton* m_cog_btn{nullptr};

    bool m_is_visible_cog{false};

private:
    Text* m_printer_name{nullptr};
    LayoutButton* m_printers_btn{nullptr};

    bool m_is_visible_printers{false};
};

} // namespace Slic3r::App::Yoga
