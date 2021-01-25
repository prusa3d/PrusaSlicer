#ifndef slic3r_GCodeWriter_hpp_
#define slic3r_GCodeWriter_hpp_

#include "libslic3r.h"
#include <string>
#include "Extruder.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCode/CoolingBuffer.hpp"

namespace Slic3r {

class GCodeWriter {
public:
    static std::string PausePrintCode;
    GCodeConfig config;
    bool multiple_extruders;
    // override from region
    const PrintRegionConfig* config_region = nullptr;
    
    GCodeWriter() : 
        multiple_extruders(false), m_extrusion_axis("E"), m_tool(nullptr),
        m_single_extruder_multi_material(false),
        m_last_acceleration(0), m_max_acceleration(0), m_last_fan_speed(0), 
        m_last_bed_temperature(0), m_last_bed_temperature_reached(true), 
        m_lifted(0)
        {}
    Tool*               tool()             { return m_tool; }
    const Tool*         tool()     const   { return m_tool; }

    std::string         extrusion_axis() const { return m_extrusion_axis; }
    void                apply_print_config(const PrintConfig &print_config);
    void                apply_print_region_config(const PrintRegionConfig& print_region_config);
    // Extruders are expected to be sorted in an increasing order.
    void                set_extruders(std::vector<uint16_t> extruder_ids);
    const std::vector<Extruder>& extruders() const { return m_extruders; }
    std::vector<uint16_t> extruder_ids() const;
    void                 set_mills(std::vector<uint16_t> extruder_ids);
    const std::vector<Mill>& mills() const { return m_millers; }
    std::vector<uint16_t> mill_ids() const;
    //give the first mill id or an id after the last extruder. Can be used to see if an id is an extruder or a mill
    uint16_t first_mill() const;
    bool tool_is_extruder() const;
    const Tool* get_tool(uint16_t id) const;
    std::string preamble();
    std::string postamble() const;
    std::string set_temperature(unsigned int temperature, bool wait = false, int tool = -1);
    std::string set_bed_temperature(unsigned int temperature, bool wait = false);
    unsigned int get_fan() { return m_last_fan_speed; }
    /// set fan at speed. Save it as current fan speed if !dont_save, and use tool default_tool if the internal m_tool is null (no toolchange done yet).
    std::string set_fan(unsigned int speed, bool dont_save = false, uint16_t default_tool = 0);
    void        set_acceleration(unsigned int acceleration);
    std::string write_acceleration();
    std::string reset_e(bool force = false);
    std::string update_progress(unsigned int num, unsigned int tot, bool allow_100 = false) const;
    // return false if this extruder was already selected
    bool        need_toolchange(unsigned int tool_id) const 
        { return m_tool == nullptr || m_tool->id() != tool_id; }
    std::string set_tool(unsigned int tool_id)
        { return this->need_toolchange(tool_id) ? this->toolchange(tool_id) : ""; }
    // Prefix of the toolchange G-code line, to be used by the CoolingBuffer to separate sections of the G-code
    // printed with the same extruder.
    std::string toolchange_prefix() const;
    std::string toolchange(unsigned int tool_id);
    std::string set_speed(double F, const std::string &comment = std::string(), const std::string &cooling_marker = std::string()) const;
    std::string travel_to_xy(const Vec2d &point, const std::string &comment = std::string());
    std::string travel_to_xyz(const Vec3d &point, const std::string &comment = std::string());
    std::string travel_to_z(double z, const std::string &comment = std::string());
    bool        will_move_z(double z) const;
    std::string extrude_to_xy(const Vec2d &point, double dE, const std::string &comment = std::string());
    std::string extrude_to_xyz(const Vec3d &point, double dE, const std::string &comment = std::string());
    std::string retract(bool before_wipe = false);
    std::string retract_for_toolchange(bool before_wipe = false);
    std::string unretract();
    std::string lift();
    std::string unlift();
    Vec3d       get_position() const { return m_pos; }

    void set_extra_lift(double extra_zlift) { this->extra_lift = extra_zlift; }
private:
	// Extruders are sorted by their ID, so that binary search is possible.
    std::vector<Extruder> m_extruders;
    std::vector<Mill> m_millers;
    std::string     m_extrusion_axis;
    bool            m_single_extruder_multi_material;
    Tool*           m_tool;
    unsigned int    m_last_acceleration;
    unsigned int    m_current_acceleration;
    // Limit for setting the acceleration, to respect the machine limits set for the Marlin firmware.
    // If set to zero, the limit is not in action.
    unsigned int    m_max_acceleration;
    unsigned int    m_last_fan_speed;
    unsigned int    m_last_fan_speed_with_offset;
    unsigned int    m_last_temperature;
    unsigned int    m_last_temperature_with_offset;
    unsigned int    m_last_bed_temperature;
    bool            m_last_bed_temperature_reached;
    double          m_lifted;
    Vec3d           m_pos = Vec3d::Zero();

    std::string _travel_to_z(double z, const std::string &comment);
    std::string _retract(double length, double restart_extra, const std::string &comment);

    // if positive, it's set, and the next lift wil have this extra lift
    double extra_lift = 0;
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeWriter_hpp_ */
