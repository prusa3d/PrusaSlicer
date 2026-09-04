#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include <algorithm>
#include <string>
#include <vector>

#include "libslic3r/HwConfigUtils.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/ConfigUtils.hpp"

namespace Slic3r {

inline char get_extrusion_axis_char(const PrintConfigView &config)
{
    std::string axis = get_extrusion_axis(config);
    assert(axis.size() <= 1);
    // Return 0 for gcfNoExtrusion
    return axis.empty() ? 0 : axis[0];
}

class SpiralVase
{
public:
    SpiralVase() = delete;

    explicit SpiralVase(const PrintConfigView &config) : m_config(config)
    {
        m_reader.z() = (float) m_config.get<double>("z_offset");
        m_reader.set_extrusion_axis(get_extrusion_axis_char(config));
        m_reader.set_use_relative_e_distances(config.get<bool>("use_relative_e_distances"));

        const auto nozzle_diameter{Biz::Slicing::get_nozzle_diameters(config.hw_config())};
        const double max_nozzle_diameter = *std::max_element(nozzle_diameter.begin(), nozzle_diameter.end());
        m_max_xy_smoothing               = float(2. * max_nozzle_diameter);
    };

    void enable(bool enable)
    {
        m_transition_layer = enable && !m_enabled;
        m_enabled          = enable;
    }

    std::string process_layer(const std::string &gcode, bool last_layer);

private:
    const PrintConfigView &m_config;
    Biz::GCodeReader::GCodeReader m_reader;
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
