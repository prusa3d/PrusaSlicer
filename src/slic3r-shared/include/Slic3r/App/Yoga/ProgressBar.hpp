#pragma once
#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

class Text;

class ProgressBar : public Rectangle
{
public:
    explicit ProgressBar();

    double progress() const;
    void set_progress(int progress, const std::string& overlay = std::string());

    bool show_overlay() const;
    void set_show_overlay(bool show_overlay);

    const ImColor& progress_fill() const;
    const ImColor& overlay_color() const;

    void set_progress_fill(const ImColor& fill);
    void set_overlay_color(const ImColor& color) const;

    void on_resized() override;

private:
    void update_area_width();

private:
    Rectangle* m_progress_area{nullptr};
    Text* m_overlay{nullptr};

    int m_value{0};
    bool m_show_overlay{false};
    bool m_progress_set_on_resized{false};
};

} // namespace Slic3r::App::Yoga
