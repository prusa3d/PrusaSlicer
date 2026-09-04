#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include <stddef.h>
#include <map>
#include <string>
#include <array>
#include <vector>
#include <cstddef>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/CoolingBufferConfig.hpp"

namespace Slic3r {

class GCodeGenerator;
class Layer;
struct PerExtruderAdjustments;
class PrintConfigView;

// A standalone G-code filter, to control cooling of the print.
// The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
// and the print is modified to stretch over a minimum layer time.
//
// The simple it sounds, the actual implementation is significantly more complex.
// Namely, for a multi-extruder print, each material may require a different cooling logic.
// For example, some materials may not like to print too slowly, while with some materials 
// we may slow down significantly.
//
class CoolingBuffer {
public:
    CoolingBuffer(GCodeGenerator& gcodegen, const Biz::Slicing::CoolingBufferConfig& config);
    void        reset(const Vec3d &position);
    void        set_current_extruder(unsigned int extruder_id) { m_current_extruder = extruder_id; }
    std::string process_layer(std::string &&gcode, size_t layer_id, bool flush);
    std::string process_layer(const std::string &gcode, size_t layer_id, bool flush)
        { return this->process_layer(std::string(gcode), layer_id, flush); }

private:
	CoolingBuffer& operator=(const CoolingBuffer&) = delete;
    std::vector<PerExtruderAdjustments> parse_layer_gcode(const std::string &gcode, std::array<float, 5> &current_pos) const;
    float       calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);
    // Apply slow down over G-code lines stored in per_extruder_adjustments, enable fan if needed.
    // Returns the adjusted G-code.
    std::string apply_layer_cooldown(const std::string &gcode, size_t layer_id, float layer_time, std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

    /**
     * @brief Clamp fan speed to [min_fan_speed, max_fan_speed] range.
     */
    int clamp_fan_speed_to_allowed_range(int fan_speed) const;

    // G-code snippet cached for the support layers preceding an object layer.
    std::string                 m_gcode;
    // Internal data.
    std::vector<char>           m_axis;
    enum AxisIdx : int {
        X = 0, Y, Z, E, F, I, J, K, R, Count
    };
    std::array<float, 5>        m_current_pos;
    // Current known fan speed or -1 if not known yet.
    int                         m_fan_speed;
    // Cached from GCodeWriter.
    // Printing extruder IDs, zero based.
    std::vector<unsigned int>   m_extruder_ids;
    // Highest of m_extruder_ids plus 1.
    unsigned int                m_num_extruders { 0 };
    const std::string           m_toolchange_prefix;
    // Referencs GCodeGenerator::m_config, which is Domain::FullConfigFDM. While the PrintObjectConfig slice of Domain::FullConfigFDM is being modified,
    // the PrintConfig slice of Domain::FullConfigFDM is constant, thus no thread synchronization is required.
    Biz::Slicing::CoolingBufferConfig m_config;
    unsigned int                m_current_extruder;
};

}

#endif
