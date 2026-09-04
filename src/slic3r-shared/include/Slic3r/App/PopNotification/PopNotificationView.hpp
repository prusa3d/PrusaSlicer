#pragma once

#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <optional>
#include <variant>

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
class ProgressBar;
class Icon;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::PopNotification {

class PopNotificationView : public Yoga::Window, public Biz::DataObserver<PopNotificationData>
{
public:
    PopNotificationView(size_t index, const PopNotificationData& data, PopNotificationObservableList& notification_list);

    void on_resized() override {}

protected:
    void on_data_update() override;

private:
    PopNotificationObservableList& m_notification_list;

    Yoga::Item* m_top_row{nullptr};
    Yoga::Item* m_left_column{nullptr};
    Yoga::Item* m_top_mid{nullptr};
    Yoga::Item* m_right_column{nullptr};
    Yoga::Item* m_mid_column{nullptr};

    Yoga::Icon* m_left_icon{nullptr};
    Yoga::LayoutButton* m_close_button {nullptr};
    Yoga::Text* m_text{nullptr};
    Yoga::Text* m_header{nullptr};
    std::vector<Yoga::LayoutButton*> m_buttons;
    Yoga::Item* m_button_line{nullptr};
    Yoga::Text* m_progress_percent_text{nullptr};
    Yoga::ProgressBar* m_progress_bar{nullptr};

    PopNotificationLayout m_current_layout;
    PopNotificationLevel m_current_level;

    void reset();
    void layout();

    using LeftContent = std::variant<Render::Icon, std::string>; // Built-in icon or a filesystem image path

    void basic_layout(const LeftContent& left_content = Render::Icon::None, bool top_row_only = false);
    void basic_left_layout(Render::Icon icon_override);
    void basic_left_image_layout(const std::string& image_path);
    void basic_right_layout();

    void basic_mid_layout();
    void basic_mid_header_layout(const std::string& header);
    void basic_mid_text_layout(const std::string& text);
    void basic_mid_buttons_layout(const std::vector<PopNotificationButtonData>& buttons);
    void basic_mid_progress_layout(int progress);

    void layout_type_text();
    void layout_type_header_text();
    void layout_type_header();
    void layout_type_image_header();
    void layout_type_image_text();
    void layout_type_image_header_text();
    void layout_type_text_buttons();
    void layout_type_header_text_buttons();
    void layout_type_text_progress();
    void layout_type_header_text_progress();
    void layout_type_header_progress();

    void update_text(const std::string& text);
    void update_header(const std::string& text);
    void update_image(const std::string& image_path);
    void update_buttons(const std::vector<PopNotificationButtonData>& buttons);
    void update_progress(int progress);

    ImColor text_color() const;
    std::optional<Platform::Color> button_accent_color() const;
    ImColor readable_accent_color(const ImColor& accent) const;
    void style_button(Yoga::LayoutButton* button) const;
};
} // namespace Slic3r::App::PopNotification