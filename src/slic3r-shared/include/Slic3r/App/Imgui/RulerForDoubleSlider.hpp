#pragma once

#include <vector>
#include <cstddef>

namespace Slic3r::App::Imgui::DoubleSlider {

class Ruler 
{
public:
    float long_step{ 0.0f };
    float short_step{ 0.0f };
    std::vector<float> max_values;// max value for each object/instance in sequence print
    // > 1 for sequential print

    void init(const std::vector<float>& values, float scroll_step);
    void update(const std::vector<float>& values, float scroll_step);
    void set_scale(float scale);
    void invalidate() { m_is_valid = false; }
    bool is_ok() const { return long_step > 0 && short_step > 0; }
    size_t count() const { return max_values.size(); }
    bool valid() const { return m_is_valid; }

private:
    bool m_is_valid{ false };
    float m_scale{ 1.0f };
    float m_min_val{ 0.0f };
    float m_max_val{ 0.0f };
    float m_scroll_step{ 0.0f };
    size_t m_max_values_cnt{ 0 };
};

} // namespace Slic3r::App::Imgui::DoubleSlider
