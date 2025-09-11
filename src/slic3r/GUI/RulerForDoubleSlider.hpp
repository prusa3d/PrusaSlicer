///|/ Copyright (c) Prusa Research 2020 - 2022 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_RulerForDoubleSlider_hpp_
#define slic3r_GUI_RulerForDoubleSlider_hpp_

#include <stddef.h>
#include <vector>
#include <set>
#include <cstddef>

namespace DoubleSlider {

class Ruler 
{
    bool   m_is_valid       { false };
    double m_scale          { 1. };
    double m_min_val;
    double m_max_val;
    double m_scroll_step;
    size_t m_max_values_cnt;

public:

    double long_step;
    double short_step;
    std::vector<double> max_values;// max value for each object/instance in sequence print
    // > 1 for sequential print

    void init(const std::vector<double>& values, double scroll_step);
    void update(const std::vector<double>& values, double scroll_step);
    void set_scale(double scale);
    void invalidate() { m_is_valid = false; }
    bool is_ok() { return long_step > 0 && short_step > 0; }
    size_t count() { return max_values.size(); }
    bool valid() { return m_is_valid; }
};

} // DoubleSlider;



#endif // slic3r_GUI_RulerForDoubleSlider_hpp_
