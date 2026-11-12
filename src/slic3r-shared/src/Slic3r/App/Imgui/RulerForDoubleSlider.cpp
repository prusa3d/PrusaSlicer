#include "Slic3r/App/Imgui/RulerForDoubleSlider.hpp"

#include <cmath>
#include <algorithm>

namespace Slic3r::App::Imgui::DoubleSlider {

static constexpr float PIXELS_PER_SM_DEFAULT  = 96.0f/*DEFAULT_DPI*/ * 5.0f / 25.4f;
static constexpr float EPSILON = 0.0011f;

void Ruler::init(const std::vector<float>& values, float scroll_step)
{
    if (m_is_valid)
        return;
    max_values.clear();
    max_values.reserve(std::count(values.begin(), values.end(), values.front()));

    auto it = std::find(values.begin() + 1, values.end(), values.front());
    while (it != values.end()) {
        max_values.push_back(*(it - 1));
        it = std::find(it + 1, values.end(), values.front());
    }
    max_values.push_back(*(it - 1));

    m_is_valid = true;
    update(values, scroll_step);
}

void Ruler::update(const std::vector<float>& values, float scroll_step)
{
    if (!m_is_valid || values.empty() ||
        // check if need to update ruler in respect to input values
        (values.front() == m_min_val && values.back() == m_max_val && m_scroll_step == scroll_step && max_values.size() == m_max_values_cnt))
        return;

    m_min_val = values.front();
    m_max_val = values.back();
    m_scroll_step = scroll_step;
    m_max_values_cnt = max_values.size();

    int pixels_per_sm = lround(m_scale * PIXELS_PER_SM_DEFAULT);

    if (lround(scroll_step) > pixels_per_sm) {
        long_step = -1.0f;
        return;
    }

    int pow = -2;
    int step = 0;
    auto end_it = std::find(values.begin() + 1, values.end(), values.front());

    while (pow < 3) {
        for (int istep : {1, 2, 5}) {
          float val = float(istep) * std::pow(10.0f, float(pow));
          auto val_it = std::lower_bound(values.begin(), end_it, val - EPSILON);

            if (val_it == values.end())
                break;
            int tick = val_it - values.begin();

            // find next tick with istep
            val *= 2;
            val_it = std::lower_bound(values.begin(), end_it, val - EPSILON);
            // count of short ticks between ticks
            int short_ticks_cnt = val_it == values.end() ? tick : val_it - values.begin() - tick;

            if (lround(short_ticks_cnt * scroll_step) > pixels_per_sm) {
                step = istep;
                // there couldn't be more then 10 short ticks between ticks
                short_step = 0.1f * short_ticks_cnt;
                break;
            }
        }
        if (step > 0)
            break;
        pow++;
    }

    long_step = (step == 0) ? -1.0f : float(step) * std::pow(10.0f, float(pow));
    if (long_step < 0)
        short_step = long_step;
}

void Ruler::set_scale(float scale)
{
    if (std::abs(m_scale - scale) > 1e-4f)
        m_scale = scale;
}

} // namespace Slic3r::App::Imgui::DoubleSlider
