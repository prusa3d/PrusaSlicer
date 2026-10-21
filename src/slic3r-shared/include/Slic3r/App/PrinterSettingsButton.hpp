#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {
class Icon;
class Text;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PrinterSettingsButton : public Yoga::RectangleButton
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

    Yoga::LayoutButton* add_button(Render::Icon icon, const std::string& tooltip);
    Yoga::Icon* add_extra_icon(Render::Icon icon);

protected:
    Yoga::Icon* m_icon{nullptr};
    Yoga::Item* m_texts_wrapper{nullptr};
    Yoga::Item* m_btn_wrapper{nullptr};
    Yoga::Text* m_printer_name{nullptr};
    Yoga::Text* m_preset_name{nullptr};
    Yoga::LayoutButton* m_cog_btn{nullptr};

    bool m_is_visible_cog{false};

private:
    Yoga::LayoutButton* m_printers_btn{nullptr};

    bool m_is_visible_printers{false};
};

} // namespace Slic3r::App
