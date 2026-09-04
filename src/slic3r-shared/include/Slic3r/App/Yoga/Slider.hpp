#pragma once
#include "Slic3r/App/Yoga/Oval.hpp"

namespace Slic3r::App::Yoga {

class Circle;

class Slider : public Oval
{
public:
    struct Callbacks
    {
        std::function<void(double value)> value_changed{nullptr};
    };

    explicit Slider(double begin, double end, double step = 1.);
    explicit Slider();

    void render(const Vec2f& pos, const Vec2f& size) override;

    Callbacks& callbacks();

    double value() const;
    void set_value(double value);

    double begin() const;
    void set_begin_value(double begin);

    double end() const;
    void set_end_value(double end);

    double step() const;
    /**
     * @warning The step value must not be zero.
     */
    void set_step(double step);

protected:
    void on_resized() override;

private:
    void set_hovered(bool hovered);
    double clamp(double value);
    double snap_to_nearest(double value);
    void update_area_width();

private:
    Oval* m_area{nullptr};
    Circle* m_thumb{nullptr};
    Circle* m_knob{nullptr};

    double m_begin_value{0.};
    double m_end_value{0.};
    double m_step{0.};
    double m_value{0.};

    bool m_dragging{false};
    bool m_hovered{false};
    bool m_is_set_thumb_size{false};

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Yoga
