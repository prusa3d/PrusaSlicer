#include "GCodeWriter.hpp"
#include "CustomGCode.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <assert.h>

#define FLAVOR_IS(val) this->config.gcode_flavor == val
#define FLAVOR_IS_NOT(val) this->config.gcode_flavor != val
#define COMMENT(comment) if (this->config.gcode_comments && !comment.empty()) gcode << " ; " << comment;
#define PRECISION(val, precision) std::fixed << std::setprecision(precision) << (val)
#define XYZF_NUM(val) PRECISION(val, 3)
#define E_NUM(val) PRECISION(val, 5)

namespace Slic3r {

    std::string GCodeWriter::PausePrintCode = "M601";

void GCodeWriter::apply_print_config(const PrintConfig &print_config)
{
    this->config.apply(print_config, true);
    m_extrusion_axis = this->config.get_extrusion_axis();
    m_single_extruder_multi_material = print_config.single_extruder_multi_material.value;
    m_max_acceleration = std::lrint((print_config.gcode_flavor.value == gcfMarlin || print_config.gcode_flavor.value == gcfLerdge || print_config.gcode_flavor.value == gcfKlipper) 
        && print_config.machine_limits_usage.value <= MachineLimitsUsage::Limits ?
        print_config.machine_max_acceleration_extruding.values.front() : 0);
}

void GCodeWriter::apply_print_region_config(const PrintRegionConfig& print_region_config)
{
    config_region = &print_region_config;
}

std::vector<uint16_t> GCodeWriter::extruder_ids() const {
    std::vector<uint16_t> out;
    out.reserve(m_extruders.size());
    for (const Extruder& e : m_extruders)
        out.push_back(e.id());
    return out;
}

std::vector<uint16_t> GCodeWriter::mill_ids() const {
    std::vector<uint16_t> out;
    out.reserve(m_millers.size());
    for (const Tool& e : m_millers)
        out.push_back(e.id());
    return out;
}

uint16_t GCodeWriter::first_mill() const {
    if (m_millers.empty()) {
        uint16_t max = 0;
        for (const Extruder& e : m_extruders)
            max = std::max(max, e.id());
        max++;
        return (uint16_t)max;
    } else return m_millers.front().id();
}
bool GCodeWriter::tool_is_extruder() const {
    return m_tool->id() < first_mill();
}
const Tool* GCodeWriter::get_tool(uint16_t id) const{
    for (const Extruder& e : m_extruders)
        if (id == e.id())
            return &e;
    for (const Tool& e : m_millers)
        if (id == e.id())
            return &e;
    return nullptr;
}

void GCodeWriter::set_extruders(std::vector<uint16_t> extruder_ids)
{
    std::sort(extruder_ids.begin(), extruder_ids.end());
    m_extruders.clear();
    m_extruders.reserve(extruder_ids.size());
    for (uint16_t extruder_id : extruder_ids)
        m_extruders.emplace_back(Extruder(extruder_id, &this->config));

    /*  we enable support for multiple extruder if any extruder greater than 0 is used
        (even if prints only uses that one) since we need to output Tx commands
        first extruder has index 0 */
    this->multiple_extruders = this->multiple_extruders || (*std::max_element(extruder_ids.begin(), extruder_ids.end())) > 0;
}

void GCodeWriter::set_mills(std::vector<uint16_t> mill_ids)
{
    std::sort(mill_ids.begin(), mill_ids.end());
    m_millers.clear();
    m_millers.reserve(mill_ids.size());
    for (uint16_t mill_id : mill_ids) {
        m_millers.emplace_back(Mill(mill_id, &this->config));
    }

    /*  we enable support for multiple extruder */
    this->multiple_extruders = this->multiple_extruders || !mill_ids.empty();
}

std::string GCodeWriter::preamble()
{
    std::ostringstream gcode;
    
    if (FLAVOR_IS_NOT(gcfMakerWare)) {
        gcode << "G21 ; set units to millimeters\n";
        gcode << "G90 ; use absolute coordinates\n";
    }
    if (FLAVOR_IS(gcfRepRap) || FLAVOR_IS(gcfMarlin) || FLAVOR_IS(gcfLerdge) || FLAVOR_IS(gcfTeacup) || FLAVOR_IS(gcfRepetier) || FLAVOR_IS(gcfSmoothie)
		 || FLAVOR_IS(gcfSprinter) || FLAVOR_IS(gcfKlipper) || FLAVOR_IS(gcfLerdge)) {
        if (this->config.use_relative_e_distances) {
            gcode << "M83 ; use relative distances for extrusion\n";
        } else {
            gcode << "M82 ; use absolute distances for extrusion\n";
        }
        gcode << this->reset_e(true);
    }
    
    return gcode.str();
}

std::string GCodeWriter::postamble() const
{
    std::ostringstream gcode;
    if (FLAVOR_IS(gcfMachinekit))
          gcode << "M2 ; end of program\n";
    return gcode.str();
}

std::string GCodeWriter::set_temperature(const unsigned int temperature, bool wait, int tool)
{
    //use m_tool if tool isn't set
    if (tool < 0 && m_tool != nullptr)
        tool = m_tool->id();

    //add offset
    int16_t temp_w_offset = int16_t(temperature);
    temp_w_offset += int16_t(get_tool(tool)->temp_offset());
    temp_w_offset = std::max(int16_t(0), std::min(int16_t(2000), temp_w_offset));

    if (m_last_temperature_with_offset == temp_w_offset && !wait)
        return "";
    if (wait && (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)))
        return "";
    
    std::string code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup) && FLAVOR_IS_NOT(gcfRepRap)) {
        code = "M109";
        comment = "set temperature and wait for it to be reached";
    } else {
        if (FLAVOR_IS(gcfRepRap)) { // M104 is deprecated on RepRapFirmware
            code = "G10";
        } else {
            code = "M104";
        }
        comment = "set temperature";
    }
    
    std::ostringstream gcode;
    gcode << code << " ";
    if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit)) {
        gcode << "P";
    } else if (FLAVOR_IS(gcfRepRap)) {
        gcode << "P" << tool << " S";
    } else {
        gcode << "S";
    }
    gcode << temp_w_offset;
    bool multiple_tools = this->multiple_extruders && ! m_single_extruder_multi_material;
    if (tool != -1 && (multiple_tools || FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) && FLAVOR_IS_NOT(gcfRepRap)) {
        gcode << " T" << tool;
    }
    gcode << " ; " << comment << "\n";
    
    if ((FLAVOR_IS(gcfTeacup) || FLAVOR_IS(gcfRepRap)) && wait)
        gcode << "M116 ; wait for temperature to be reached\n";
    
    m_last_temperature = temperature;
    m_last_temperature_with_offset = temp_w_offset;

    return gcode.str();
}

std::string GCodeWriter::set_bed_temperature(unsigned int temperature, bool wait)
{
    if (temperature == m_last_bed_temperature && (! wait || m_last_bed_temperature_reached))
        return std::string();

    m_last_bed_temperature = temperature;
    m_last_bed_temperature_reached = wait;

    std::string code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup)) {
        if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
            code = "M109";
        } else {
            code = "M190";
        }
        comment = "set bed temperature and wait for it to be reached";
    } else {
        code = "M140";
        comment = "set bed temperature";
    }
    
    std::ostringstream gcode;
    gcode << code << " ";
    if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit)) {
        gcode << "P";
    } else {
        gcode << "S";
    }
    gcode << temperature << " ; " << comment << "\n";
    
    if (FLAVOR_IS(gcfTeacup) && wait)
        gcode << "M116 ; wait for bed temperature to be reached\n";
    
    return gcode.str();
}

std::string GCodeWriter::set_fan(const unsigned int speed, bool dont_save, uint16_t default_tool)
{
    std::ostringstream gcode;

    const Tool *tool = m_tool == nullptr ? get_tool(default_tool) : m_tool;
    //add fan_offset
    int16_t fan_speed = int16_t(speed);
    if (tool != nullptr)
        fan_speed += int8_t(tool->fan_offset());
    fan_speed = std::max(int16_t(0), std::min(int16_t(100), fan_speed));

    //test if it's useful to write it
    if (m_last_fan_speed_with_offset != fan_speed || dont_save) {
        //save new current value
        if (!dont_save) {
            m_last_fan_speed = speed;
            m_last_fan_speed_with_offset = fan_speed;
        }
        
        // write it
        if (fan_speed == 0) {
            if (FLAVOR_IS(gcfTeacup)) {
                gcode << "M106 S0";
            } else if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
                gcode << "M127";
            } else {
                gcode << "M107";
            }
            if (this->config.gcode_comments) gcode << " ; disable fan";
            gcode << "\n";
        } else {
            if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
                gcode << "M126 T";
            } else {
                gcode << "M106 ";
                if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit)) {
                    gcode << "P";
                } else {
                    gcode << "S";
                }
                gcode << (255.0 * fan_speed / 100.0);
            }
            if (this->config.gcode_comments) gcode << " ; enable fan";
            gcode << "\n";
        }
    }
    return gcode.str();
}

void GCodeWriter::set_acceleration(unsigned int acceleration)
{
    // Clamp the acceleration to the allowed maximum.
    if (m_max_acceleration > 0 && acceleration > m_max_acceleration)
        acceleration = m_max_acceleration;

    if (acceleration == 0 || acceleration == m_current_acceleration)
        return;

    m_current_acceleration = acceleration;
}

std::string GCodeWriter::write_acceleration(){
    if (m_current_acceleration == m_last_acceleration || m_current_acceleration == 0)
        return "";

    m_last_acceleration = m_current_acceleration;

    std::ostringstream gcode;
	//try to set only printing acceleration, travel should be untouched if possible
    if (FLAVOR_IS(gcfRepetier)) {
        // M201: Set max printing acceleration
        gcode << "M201 X" << m_current_acceleration << " Y" << m_current_acceleration;
    } else if(FLAVOR_IS(gcfMarlin) || FLAVOR_IS(gcfLerdge) || FLAVOR_IS(gcfRepRap) || FLAVOR_IS(gcfSprinter)){
        // M204: Set printing acceleration
        gcode << "M204 P" << m_current_acceleration;
    } else {
        // M204: Set default acceleration
        gcode << "M204 S" << m_current_acceleration;
    }
    if (this->config.gcode_comments) gcode << " ; adjust acceleration";
    gcode << "\n";
    
    return gcode.str();
}

std::string GCodeWriter::reset_e(bool force)
{
    if (FLAVOR_IS(gcfMach3)
        || FLAVOR_IS(gcfMakerWare)
        || FLAVOR_IS(gcfSailfish))
        return "";
    
    if (m_tool != nullptr) {
        if (m_tool->E() == 0. && ! force)
            return "";
        m_tool->reset_E();
    }

    if (! m_extrusion_axis.empty() && ! this->config.use_relative_e_distances) {
        std::ostringstream gcode;
        gcode << "G92 " << m_extrusion_axis << "0";
        if (this->config.gcode_comments) gcode << " ; reset extrusion distance";
        gcode << "\n";
        return gcode.str();
    } else {
        return "";
    }
}

std::string GCodeWriter::update_progress(unsigned int num, unsigned int tot, bool allow_100) const
{
    if (FLAVOR_IS_NOT(gcfMakerWare) && FLAVOR_IS_NOT(gcfSailfish))
        return "";
    
    unsigned int percent = (unsigned int)floor(100.0 * num / tot + 0.5);
    if (!allow_100) percent = std::min(percent, (unsigned int)99);
    
    std::ostringstream gcode;
    gcode << "M73 P" << percent;
    if (this->config.gcode_comments) gcode << " ; update progress";
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::toolchange_prefix() const
{
    return FLAVOR_IS(gcfMakerWare) ? "M135 T" :
           FLAVOR_IS(gcfSailfish) ? "M108 T" :
           FLAVOR_IS(gcfKlipper) ? "ACTIVATE_EXTRUDER EXTRUDER=" :
           "T";
}

std::string GCodeWriter::toolchange(unsigned int tool_id)
{
    // set the new extruder
	/*auto it_extruder = Slic3r::lower_bound_by_predicate(m_extruders.begin(), m_extruders.end(), [tool_id](const Extruder &e) { return e.id() < tool_id; });
    assert(it_extruder != m_extruders.end() && it_extruder->id() == extruder_id);*/
    //less optimized but it's easier to modify and it's not needed, as it's not called often.
    bool found = false;
    for (Extruder& extruder : m_extruders) {
        if (tool_id == extruder.id()) {
            m_tool = &extruder;
            found = true;
            break;
        }
    }
    if (!found) {
        for (Tool& mill : m_millers) {
            if (tool_id == mill.id()) {
                m_tool = &mill;
                found = true;
                break;
            }
        }
    }

    // return the toolchange command
    // if we are running a single-extruder setup, just set the extruder and return nothing
    std::ostringstream gcode;
    if (this->multiple_extruders) {
        if (FLAVOR_IS(gcfKlipper)) {
            //check if we can use the tool_name field or not
            if (tool_id > 0 && tool_id < this->config.tool_name.values.size() && !this->config.tool_name.values[tool_id].empty()
                && this->config.tool_name.values[tool_id][0] != ('0' + tool_id)) {
                gcode << this->toolchange_prefix() << this->config.tool_name.values[tool_id];
            } else {
                gcode << this->toolchange_prefix() << "extruder";
                if (tool_id > 0)
                    gcode << tool_id;
            }
        } else {
            gcode << this->toolchange_prefix() << tool_id;
        }
        if (this->config.gcode_comments)
            gcode << " ; change extruder";
        gcode << "\n";
        gcode << this->reset_e(true);
    }
    return gcode.str();
}

std::string GCodeWriter::set_speed(double F, const std::string &comment, const std::string &cooling_marker) const
{
    assert(F > 0.);
    assert(F < 100000.);
    std::ostringstream gcode;
    gcode << "G1 F" << XYZF_NUM(F);
    COMMENT(comment);
    gcode << cooling_marker;
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::travel_to_xy(const Vec2d &point, const std::string &comment)
{
    std::ostringstream gcode;
    gcode << write_acceleration();

    m_pos.x() = point.x();
    m_pos.y() = point.y();
    
    gcode << "G1 X" << XYZF_NUM(point.x())
          <<   " Y" << XYZF_NUM(point.y())
          <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);
    COMMENT(comment);
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::travel_to_xyz(const Vec3d &point, const std::string &comment)
{
    /*  If target Z is lower than current Z but higher than nominal Z we
        don't perform the Z move but we only move in the XY plane and
        adjust the nominal Z by reducing the lift amount that will be 
        used for unlift. */
    if (!this->will_move_z(point.z())) {
        double nominal_z = m_pos.z() - m_lifted;
        m_lifted -= (point.z() - nominal_z);
        // In case that retract_lift == layer_height we could end up with almost zero in_m_lifted
        // and a retract could be skipped (https://github.com/prusa3d/PrusaSlicer/issues/2154
        if (std::abs(m_lifted) < EPSILON)
            m_lifted = 0.;
        return this->travel_to_xy(to_2d(point));
    }
    
    /*  In all the other cases, we perform an actual XYZ move and cancel
        the lift. */
    m_lifted = 0;
    m_pos = point;

    std::ostringstream gcode;
    gcode << write_acceleration();
    gcode << "G1 X" << XYZF_NUM(point.x())
          << " Y" << XYZF_NUM(point.y());
    if (config.z_step > SCALING_FACTOR)
        gcode << " Z" << PRECISION(point.z(), 6);
    else
        gcode << " Z" << XYZF_NUM(point.z());
    gcode <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);

    COMMENT(comment);
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::travel_to_z(double z, const std::string &comment)
{
    /*  If target Z is lower than current Z but higher than nominal Z
        we don't perform the move but we only adjust the nominal Z by
        reducing the lift amount that will be used for unlift. */
    if (!this->will_move_z(z)) {
        double nominal_z = m_pos.z() - m_lifted;
        m_lifted -= (z - nominal_z);
        if (std::abs(m_lifted) < EPSILON)
            m_lifted = 0.;
        return "";
    }
    
    /*  In all the other cases, we perform an actual Z move and cancel
        the lift. */
    m_lifted = 0;
    return this->_travel_to_z(z, comment);
}

std::string GCodeWriter::_travel_to_z(double z, const std::string &comment)
{
    m_pos.z() = z;

    std::ostringstream gcode;

    gcode << write_acceleration();    if (config.z_step > SCALING_FACTOR)
        gcode << "G1 Z" << PRECISION(z, 6);
    else
        gcode << "G1 Z" << XYZF_NUM(z);
    gcode <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);
    COMMENT(comment);
    gcode << "\n";
    return gcode.str();
}

bool GCodeWriter::will_move_z(double z) const
{
    /* If target Z is lower than current Z but higher than nominal Z
        we don't perform an actual Z move. */
    if (m_lifted > 0) {
        double nominal_z = m_pos.z() - m_lifted;
        if (z >= nominal_z && z <= m_pos.z())
            return false;
    }
    return true;
}

std::string GCodeWriter::extrude_to_xy(const Vec2d &point, double dE, const std::string &comment)
{
    assert(dE == dE);
    m_pos.x() = point.x();
    m_pos.y() = point.y();
    bool is_extrude = m_tool->extrude(dE) != 0;

    std::ostringstream gcode;
    gcode << write_acceleration();
    gcode << "G1 X" << XYZF_NUM(point.x())
        << " Y" << XYZF_NUM(point.y());
    if(is_extrude)
        gcode <<    " " << m_extrusion_axis << E_NUM(m_tool->E());
    COMMENT(comment);
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::extrude_to_xyz(const Vec3d &point, double dE, const std::string &comment)
{
    assert(dE == dE);
    m_pos.x() = point.x();
    m_pos.y() = point.y();
    m_lifted = 0;
    bool is_extrude = m_tool->extrude(dE) != 0;

    std::ostringstream gcode;
    gcode << write_acceleration();
    gcode << "G1 X" << XYZF_NUM(point.x())
        << " Y" << XYZF_NUM(point.y())
        << " Z" << XYZF_NUM(point.z() + m_pos.z());
    if (is_extrude)
            gcode <<    " " << m_extrusion_axis << E_NUM(m_tool->E());
    COMMENT(comment);
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::retract(bool before_wipe)
{
    double factor = before_wipe ? m_tool->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    //check for override
    if (config_region && config_region->print_retract_length >= 0) {
        return this->_retract(
            factor * config_region->print_retract_length,
            factor * m_tool->retract_restart_extra(),
            "retract"
        );
    }
    return this->_retract(
        factor * m_tool->retract_length(),
        factor * m_tool->retract_restart_extra(),
        "retract"
    );
}

std::string GCodeWriter::retract_for_toolchange(bool before_wipe)
{
    double factor = before_wipe ? m_tool->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    return this->_retract(
        factor * m_tool->retract_length_toolchange(),
        factor * m_tool->retract_restart_extra_toolchange(),
        "retract for toolchange"
    );
}

std::string GCodeWriter::_retract(double length, double restart_extra, const std::string &comment)
{
    std::ostringstream gcode;
    
    /*  If firmware retraction is enabled, we use a fake value of 1
        since we ignore the actual configured retract_length which 
        might be 0, in which case the retraction logic gets skipped. */
    if (this->config.use_firmware_retraction) length = 1;
    
    // If we use volumetric E values we turn lengths into volumes */
    if (this->config.use_volumetric_e) {
        double d = m_tool->filament_diameter();
        double area = d * d * PI/4;
        length = length * area;
        restart_extra = restart_extra * area;
    }
    
    double dE = m_tool->retract(length, restart_extra);
    assert(dE >= 0);
    assert(dE < 10000000);
    if (dE != 0) {
        if (this->config.use_firmware_retraction) {
            if (FLAVOR_IS(gcfMachinekit))
                gcode << "G22 ; retract\n";
            else
                gcode << "G10 ; retract\n";
        } else {
            gcode << "G1 " << m_extrusion_axis << E_NUM(m_tool->E())
                           << " F" << XYZF_NUM(m_tool->retract_speed() * 60.);
            COMMENT(comment);
            gcode << "\n";
        }
    }
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode << "M103 ; extruder off\n";
    
    return gcode.str();
}

std::string GCodeWriter::unretract()
{
    std::ostringstream gcode;
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode << "M101 ; extruder on\n";
    
    double dE = m_tool->unretract();
    assert(dE >= 0);
    assert(dE < 10000000);
    if (dE != 0) {
        if (this->config.use_firmware_retraction) {
            if (FLAVOR_IS(gcfMachinekit))
                 gcode << "G23 ; unretract\n";
            else
                 gcode << "G11 ; unretract\n";
            gcode << this->reset_e();
        } else {
            // use G1 instead of G0 because G0 will blend the restart with the previous travel move
            gcode << "G1 " << m_extrusion_axis << E_NUM(m_tool->E())
                           << " F" << XYZF_NUM(m_tool->deretract_speed() * 60.);
            if (this->config.gcode_comments) gcode << " ; unretract";
            gcode << "\n";
        }
    }
    
    return gcode.str();
}

/*  If this method is called more than once before calling unlift(),
    it will not perform subsequent lifts, even if Z was raised manually
    (i.e. with travel_to_z()) and thus _lifted was reduced. */
std::string GCodeWriter::lift()
{
    // check whether the above/below conditions are met
    double target_lift = 0;
    if(this->tool_is_extruder()){
        //these two should be in the Tool class methods....
        double above = this->config.retract_lift_above.get_at(m_tool->id());
        double below = this->config.retract_lift_below.get_at(m_tool->id());
        if (m_pos.z() >= above && (below == 0 || m_pos.z() <= below))
            target_lift = m_tool->retract_lift();
    } else {
        target_lift = m_tool->retract_lift();
    }

    // use the override if set
    if (target_lift > 0 && config_region && config_region->print_retract_lift.value >= 0) {
        target_lift = config_region->print_retract_lift.value;
    }

    if (this->extra_lift > 0) {
        target_lift += this->extra_lift;
        this->extra_lift = 0;
    }

    // compare against epsilon because travel_to_z() does math on it
    // and subtracting layer_height from retract_lift might not give
    // exactly zero
    if (std::abs(m_lifted) < EPSILON && target_lift > 0) {
        m_lifted = target_lift;
        return this->_travel_to_z(m_pos.z() + target_lift, "lift Z");
    }
    return "";
}

std::string GCodeWriter::unlift()
{
    std::string gcode;
    if (m_lifted > 0) {
        gcode += this->_travel_to_z(m_pos.z() - m_lifted, "restore layer Z");
    }
    m_lifted = 0;
    return gcode;
}

}
