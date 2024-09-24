///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include <algorithm>
#include <cmath>

#include "RulerForDoubleSlider.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;
using namespace CustomGCode;

namespace DoubleSlider {

static const double PIXELS_PER_SM_DEFAULT  = 96./*DEFAULT_DPI*/ * 5. / 25.4;

void Ruler::init(const std::vector<double>& values, double scroll_step)
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

void Ruler::update(const std::vector<double>& values, double scroll_step)
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
        long_step = -1.0;
        return;
    }

    int pow = -2;
    int step = 0;
    auto end_it = std::find(values.begin() + 1, values.end(), values.front());

    while (pow < 3) {
        for (int istep : {1, 2, 5}) {
            double val = (double)istep * std::pow(10, pow);
            auto val_it = std::lower_bound(values.begin(), end_it, val - epsilon());

            if (val_it == values.end())
                break;
            int tick = val_it - values.begin();

            // find next tick with istep
            val *= 2;
            val_it = std::lower_bound(values.begin(), end_it, val - epsilon());
            // count of short ticks between ticks
            int short_ticks_cnt = val_it == values.end() ? tick : val_it - values.begin() - tick;

            if (lround(short_ticks_cnt * scroll_step) > pixels_per_sm) {
                step = istep;
                // there couldn't be more then 10 short ticks between ticks
                short_step = 0.1 * short_ticks_cnt;
                break;
            }
        }
        if (step > 0)
            break;
        pow++;
    }

    long_step = step == 0 ? -1.0 : (double)step * std::pow(10, pow);
    if (long_step < 0)
        short_step = long_step;
}

void Ruler::set_scale(double scale)
{
    if (!is_approx(m_scale, scale))
        m_scale = scale;
}

} // DoubleSlider


