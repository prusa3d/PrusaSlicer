///|/ Copyright (c) Prusa Research 2017 - 2021 Vojtěch Bubník @bubnikv
///|/
///|/ ported from lib/Slic3r/GCode/SpiralVase.pm:
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include <algorithm>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class SpiralVase
{
public:
    SpiralVase() = delete;

    explicit SpiralVase(const PrintConfig &config) : m_config(config)
    {
        m_reader.z() = (float) m_config.z_offset;
        m_reader.apply_config(m_config);

        const double max_nozzle_diameter = *std::max_element(config.nozzle_diameter.values.begin(), config.nozzle_diameter.values.end());
        m_max_xy_smoothing               = float(2. * max_nozzle_diameter);
    };

    void enable(bool enable)
    {
        m_transition_layer = enable && !m_enabled;
        m_enabled          = enable;
    }

    std::string process_layer(const std::string &gcode, bool last_layer);

private:
    const PrintConfig  &m_config;
    GCodeReader 		m_reader;
    float               m_max_xy_smoothing = 0.f;

    bool 				m_enabled = false;
    // First spiral vase layer. Layer height has to be ramped up from zero to the target layer height.
    bool 				m_transition_layer = false;
    // Whether to interpolate XY coordinates with the previous layer. Results in no seam at layer changes
    bool                m_smooth_spiral = true;
    std::vector<Vec2f>  m_previous_layer;
};
}

#endif // slic3r_SpiralVase_hpp_
