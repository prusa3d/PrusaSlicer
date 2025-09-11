///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) 2021 Raphael Sobik
///|/ Copyright (c) 2021 Martin Budden
///|/ Copyright (c) 2020 Paul Arden @ardenpm
///|/ Copyright (c) 2016 Joseph Lenox @lordofhyphens
///|/ Copyright (c) 2016 Chow Loong Jin @hyperair
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/ Copyright (c) 2015 Maciej Dębski
///|/ Copyright (c) 2015 Alexander Rössler @machinekoder
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GCodeWriter.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <cassert>
#include <cinttypes>

#include "libslic3r/libslic3r.h"

#ifdef __APPLE__
    #include <boost/spirit/include/karma.hpp>
#endif

#define FLAVOR_IS(val) this->config.gcode_flavor == val
#define FLAVOR_IS_NOT(val) this->config.gcode_flavor != val

using namespace std::string_view_literals;

namespace Slic3r {

// static
bool GCodeWriter::supports_separate_travel_acceleration(GCodeFlavor flavor)
{
    return (flavor == gcfRepetier || flavor == gcfMarlinFirmware ||  flavor == gcfRepRapFirmware);
}

void GCodeWriter::apply_print_config(const PrintConfig &print_config)
{
    this->config.apply(print_config, true);
    m_extrusion_axis = get_extrusion_axis(this->config);
    m_single_extruder_multi_material = print_config.single_extruder_multi_material.value;
    bool use_mach_limits = print_config.gcode_flavor.value == gcfMarlinLegacy
                        || print_config.gcode_flavor.value == gcfMarlinFirmware
                        || print_config.gcode_flavor.value == gcfRepRapFirmware;
    m_max_acceleration = static_cast<unsigned int>(std::round((use_mach_limits && print_config.machine_limits_usage.value == MachineLimitsUsage::EmitToGCode) ?
        print_config.machine_max_acceleration_extruding.values.front() : 0));
    m_max_travel_acceleration = static_cast<unsigned int>(std::round((use_mach_limits && print_config.machine_limits_usage.value == MachineLimitsUsage::EmitToGCode && supports_separate_travel_acceleration(print_config.gcode_flavor.value)) ?
        print_config.machine_max_acceleration_travel.values.front() : 0));
}

void GCodeWriter::set_extruders(std::vector<unsigned int> extruder_ids)
{
    std::sort(extruder_ids.begin(), extruder_ids.end());
    m_extruders.clear();
    m_extruders.reserve(extruder_ids.size());
    for (unsigned int extruder_id : extruder_ids)
        m_extruders.emplace_back(Extruder(extruder_id, &this->config));
    
    /*  we enable support for multiple extruder if any extruder greater than 0 is used
        (even if prints only uses that one) since we need to output Tx commands
        first extruder has index 0 */
    this->multiple_extruders = (*std::max_element(extruder_ids.begin(), extruder_ids.end())) > 0;
}

std::string GCodeWriter::preamble()
{
    std::ostringstream gcode;
    
    if (FLAVOR_IS_NOT(gcfMakerWare)) {
        gcode << "G21 ; set units to millimeters\n";
        gcode << "G90 ; use absolute coordinates\n";
    }
    if (FLAVOR_IS(gcfRepRapSprinter) ||
        FLAVOR_IS(gcfRepRapFirmware) ||
        FLAVOR_IS(gcfMarlinLegacy) ||
        FLAVOR_IS(gcfMarlinFirmware) ||
        FLAVOR_IS(gcfKlipper) ||
        FLAVOR_IS(gcfTeacup) ||
        FLAVOR_IS(gcfRepetier) ||
        FLAVOR_IS(gcfSmoothie))
    {
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

std::string GCodeWriter::set_temperature(unsigned int temperature, bool wait, int tool) const
{
    if (wait && (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)))
        return {};
    
    std::string_view code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup) && FLAVOR_IS_NOT(gcfRepRapFirmware)) {
        code = "M109"sv;
        comment = "set temperature and wait for it to be reached"sv;
    } else {
        if (FLAVOR_IS(gcfRepRapFirmware)) { // M104 is deprecated on RepRapFirmware
            code = "G10"sv;
        } else {
            code = "M104"sv;
        }
        comment = "set temperature"sv;
    }
    
    std::ostringstream gcode;
    gcode << code << " ";
    if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit)) {
        gcode << "P";
    } else {
        gcode << "S";
    }
    gcode << temperature;
    bool multiple_tools = this->multiple_extruders && ! m_single_extruder_multi_material;
    if (tool != -1 && (multiple_tools || FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish) || FLAVOR_IS(gcfRepRapFirmware)) ) {
        if (FLAVOR_IS(gcfRepRapFirmware)) {
            gcode << " P" << tool;
        } else {
            gcode << " T" << tool;
        }
    }
    gcode << " ; " << comment << "\n";
    
    if ((FLAVOR_IS(gcfTeacup) || FLAVOR_IS(gcfRepRapFirmware)) && wait)
        gcode << "M116 ; wait for temperature to be reached\n";
    
    return gcode.str();
}

std::string GCodeWriter::set_bed_temperature(unsigned int temperature, bool wait)
{
    if (temperature == m_last_bed_temperature && (! wait || m_last_bed_temperature_reached))
        return {};

    m_last_bed_temperature = temperature;
    m_last_bed_temperature_reached = wait;

    std::string_view code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup)) {
        if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
            code = "M109"sv;
        } else {
            code = "M190"sv;
        }
        comment = "set bed temperature and wait for it to be reached"sv;
    } else {
        code = "M140"sv;
        comment = "set bed temperature"sv;
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



std::string GCodeWriter::set_chamber_temperature(unsigned int temperature, bool wait, bool accurate) const
{
    std::string_view code, comment;
    if (wait) {
        code = "M191"sv;
        comment = "set chamber temperature and wait for it to be reached"sv;
    } else {
        code = "M141"sv;
        comment = "set chamber temperature"sv;
    }
    
    std::ostringstream gcode;
    gcode << code << (accurate ? " R" : " S") << temperature << " ; " << comment << "\n";
    
    return gcode.str();
}



std::string GCodeWriter::set_acceleration_internal(Acceleration type, unsigned int acceleration)
{
    // Clamp the acceleration to the allowed maximum.
    if (type == Acceleration::Print && m_max_acceleration > 0 && acceleration > m_max_acceleration)
        acceleration = m_max_acceleration;
    if (type == Acceleration::Travel && m_max_travel_acceleration > 0 && acceleration > m_max_travel_acceleration)
        acceleration = m_max_travel_acceleration;

    // Are we setting travel acceleration for a flavour that supports separate travel and print acc?
    bool separate_travel = (type == Acceleration::Travel && supports_separate_travel_acceleration(this->config.gcode_flavor));

    auto& last_value = separate_travel ? m_last_travel_acceleration : m_last_acceleration ;
    if (acceleration == 0 || acceleration == last_value)
        return {};
    
    last_value = acceleration;
    
    std::ostringstream gcode;
    if (FLAVOR_IS(gcfRepetier))
        gcode << (separate_travel ? "M202 X" : "M201 X") << acceleration << " Y" << acceleration;
    else if (FLAVOR_IS(gcfRepRapFirmware) || FLAVOR_IS(gcfMarlinFirmware))
        gcode << (separate_travel ? "M204 T" : "M204 P") << acceleration;
    else
        gcode << "M204 S" << acceleration;

    if (this->config.gcode_comments) gcode << " ; adjust acceleration";
    gcode << "\n";
    
    return gcode.str();
}

std::string GCodeWriter::reset_e(bool force)
{
    return
        FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish) || this->config.use_relative_e_distances ||
        (m_extruder != nullptr && ! m_extruder->reset_E() && ! force) || 
        m_extrusion_axis.empty() ?
        std::string{} :
        std::string("G92 ") + m_extrusion_axis + (this->config.gcode_comments ? "0 ; reset extrusion distance\n" : "0\n");
}

std::string GCodeWriter::update_progress(unsigned int num, unsigned int tot, bool allow_100) const
{
    if (FLAVOR_IS_NOT(gcfMakerWare) && FLAVOR_IS_NOT(gcfSailfish))
        return {};
    
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
           FLAVOR_IS(gcfSailfish)  ? "M108 T" : "T";
}

std::string GCodeWriter::toolchange(unsigned int extruder_id)
{
    // set the new extruder
	auto it_extruder = Slic3r::lower_bound_by_predicate(m_extruders.begin(), m_extruders.end(), [extruder_id](const Extruder &e) { return e.id() < extruder_id; });
    assert(it_extruder != m_extruders.end() && it_extruder->id() == extruder_id);
    m_extruder = &*it_extruder;

    // return the toolchange command
    // if we are running a single-extruder setup, just set the extruder and return nothing
    std::ostringstream gcode;
    if (this->multiple_extruders) {
        gcode << this->toolchange_prefix() << extruder_id;
        if (this->config.gcode_comments)
            gcode << " ; change extruder";
        gcode << "\n";
        gcode << this->reset_e(true);
    }
    return gcode.str();
}

std::string GCodeWriter::set_speed(double F, const std::string_view comment, const std::string_view cooling_marker) const
{
    assert(F > 0.);
    assert(F < 100000.);

    GCodeG1Formatter w;
    w.emit_f(F);
    w.emit_comment(this->config.gcode_comments, comment);
    w.emit_string(cooling_marker);
    return w.string();
}

std::string GCodeWriter::travel_to_xy_force(const Vec2d &point, const std::string_view comment)
{
    GCodeG1Formatter w;
    w.emit_xy(point);
    w.emit_f(this->config.travel_speed.value * 60.0);
    w.emit_comment(this->config.gcode_comments, comment);
    m_pos.head<2>() = point.head<2>();
    return w.string();
}

std::string GCodeWriter::travel_to_xy(const Vec2d &point, const std::string_view comment)
{
    if (std::abs(point.x() - m_pos.x()) < GCodeFormatter::XYZ_EPSILON
        && std::abs(point.y() - m_pos.y()) < GCodeFormatter::XYZ_EPSILON)
    {
        return "";
    } else {
        return this->travel_to_xy_force(point, comment);
    }
}

std::string GCodeWriter::travel_to_xy_G2G3IJ(const Vec2d &point, const Vec2d &ij, const bool ccw, const std::string_view comment)
{
    assert(std::abs(point.x()) < 1200.);
    assert(std::abs(point.y()) < 1200.);
    assert(std::abs(ij.x()) < 1200.);
    assert(std::abs(ij.y()) < 1200.);
    assert(std::abs(ij.x()) >= 0.001 || std::abs(ij.y()) >= 0.001);
 
    m_pos.head<2>() = point.head<2>();

    GCodeG2G3Formatter w(ccw);
    w.emit_xy(point);
    w.emit_ij(ij);
    w.emit_comment(this->config.gcode_comments, comment);
    return w.string();
}

std::string GCodeWriter::travel_to_xyz(const Vec3d &to, const std::string_view comment)
{
    if (std::abs(to.x() - m_pos.x()) < GCodeFormatter::XYZ_EPSILON
        && std::abs(to.y() - m_pos.y()) < GCodeFormatter::XYZ_EPSILON
        && std::abs(to.z() - m_pos.z()) < GCodeFormatter::XYZ_EPSILON)
    {
        return "";
    } else if (
        std::abs(to.x() - m_pos.x()) < GCodeFormatter::XYZ_EPSILON
        && std::abs(to.y() - m_pos.y()) < GCodeFormatter::XYZ_EPSILON)
    {
        return this->travel_to_z_force(to.z(), comment);
    } else if (
        std::abs(to.z() - m_pos.z()) < GCodeFormatter::XYZ_EPSILON)
    {
        return this->travel_to_xy_force(to.head<2>(), comment);
    } else {
        return this->travel_to_xyz_force(to, comment);
    }
}

std::string GCodeWriter::travel_to_xyz_force(const Vec3d &to, const std::string_view comment) {
    GCodeG1Formatter w;
    w.emit_xyz(to);

    double speed = this->config.travel_speed.value;
    const double speed_z = this->config.travel_speed_z.value;

    if (speed_z) {
        const Vec3d move{to - m_pos};
        const double distance{move.norm()};
        const double abs_unit_vector_z{std::abs(move.z())/distance};
        // De-compose speed into z vector component according to the movement unit vector.
        const double speed_vector_z{abs_unit_vector_z * speed};
        if (speed_vector_z > speed_z) {
            // Re-compute speed so that the z component is exactly speed_z.
            speed = speed_z / abs_unit_vector_z;
        }
    }
    w.emit_f(speed * 60.0);

    w.emit_comment(this->config.gcode_comments, comment);
    m_pos = to;
    return w.string();
}

std::string GCodeWriter::travel_to_z(double z, const std::string_view comment)
{
    if (std::abs(m_pos.z() - z) < GCodeFormatter::XYZ_EPSILON) {
        return "";
    } else {
        return this->travel_to_z_force(z, comment);
    }
}

std::string GCodeWriter::travel_to_z_force(double z, const std::string_view comment)
{
    double speed = this->config.travel_speed_z.value;
    if (speed == 0.)
        speed = this->config.travel_speed.value;

    GCodeG1Formatter w;
    w.emit_z(z);
    w.emit_f(speed * 60.0);
    w.emit_comment(this->config.gcode_comments, comment);
    m_pos.z() = z;
    return w.string();
}

std::string GCodeWriter::extrude_to_xy(const Vec2d &point, double dE, const std::string_view comment)
{
    //assert(dE != 0);
    assert(std::abs(dE) < 1000.0);

    m_pos.head<2>() = point.head<2>();

    GCodeG1Formatter w;
    w.emit_xy(point);
    w.emit_e(m_extrusion_axis, m_extruder->extrude(dE).second);
    w.emit_comment(this->config.gcode_comments, comment);
    return w.string();
}

std::string GCodeWriter::extrude_to_xyz(const Vec3d &point, double dE, const std::string_view comment)
{
    m_pos = point;

    GCodeG1Formatter w;
    w.emit_xyz(point);
    w.emit_e(m_extrusion_axis, m_extruder->extrude(dE).second);
    w.emit_comment(this->config.gcode_comments, comment);
    return w.string();
}

std::string GCodeWriter::extrude_to_xy_G2G3IJ(const Vec2d &point, const Vec2d &ij, const bool ccw, double dE, const std::string_view comment)
{
    assert(std::abs(dE) < 1000.0);
    assert(dE != 0);
    assert(std::abs(point.x()) < 1200.);
    assert(std::abs(point.y()) < 1200.);
    assert(std::abs(ij.x()) < 1200.);
    assert(std::abs(ij.y()) < 1200.);
    assert(std::abs(ij.x()) >= 0.001 || std::abs(ij.y()) >= 0.001);

    m_pos.head<2>() = point.head<2>();

    GCodeG2G3Formatter w(ccw);
    w.emit_xy(point);
    w.emit_ij(ij);
    w.emit_e(m_extrusion_axis, m_extruder->extrude(dE).second);
    w.emit_comment(this->config.gcode_comments, comment);
    return w.string();
}

#if 0
std::string GCodeWriter::extrude_to_xyz(const Vec3d &point, double dE, const std::string_view comment)
{
    m_pos = point;
    m_lifted = 0;
    m_extruder->extrude(dE);
    
    GCodeG1Formatter w;
    w.emit_xyz(point);
    w.emit_e(m_extrusion_axis, m_extruder->E());
    w.emit_comment(this->config.gcode_comments, comment);
    return w.string();
}
#endif

std::string GCodeWriter::retract(bool before_wipe)
{
    double factor = before_wipe ? m_extruder->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    return this->_retract(
        factor * m_extruder->retract_length(),
        m_extruder->retract_restart_extra(),
        "retract"
    );
}

std::string GCodeWriter::retract_for_toolchange(bool before_wipe)
{
    double factor = before_wipe ? m_extruder->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    return this->_retract(
        factor * m_extruder->retract_length_toolchange(),
        m_extruder->retract_restart_extra_toolchange(),
        "retract for toolchange"
    );
}

std::string GCodeWriter::_retract(double length, double restart_extra, const std::string_view comment)
{
    assert(std::abs(length) < 1000.0);
    assert(std::abs(restart_extra) < 1000.0);

    /*  If firmware retraction is enabled, we use a fake value of 1
        since we ignore the actual configured retract_length which 
        might be 0, in which case the retraction logic gets skipped. */
    if (this->config.use_firmware_retraction)
        length = 1;
    
    // If we use volumetric E values we turn lengths into volumes */
    if (this->config.use_volumetric_e) {
        double d = m_extruder->filament_diameter();
        double area = d * d * PI/4;
        length = length * area;
        restart_extra = restart_extra * area;
    }
    
    std::string gcode;
    if (auto [dE, emitE] = m_extruder->retract(length, restart_extra);  dE != 0) {
        if (this->config.use_firmware_retraction) {
            gcode = FLAVOR_IS(gcfMachinekit) ? "G22 ; retract\n" : "G10 ; retract\n";
        } else if (! m_extrusion_axis.empty()) {
            GCodeG1Formatter w;
            w.emit_e(m_extrusion_axis, emitE);
            w.emit_f(m_extruder->retract_speed() * 60.);
            w.emit_comment(this->config.gcode_comments, comment);
            gcode = w.string();
        }
    }
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode += "M103 ; extruder off\n";

    return gcode;
}

std::string GCodeWriter::unretract()
{
    std::string gcode;
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode = "M101 ; extruder on\n";
    
    if (auto [dE, emitE] = m_extruder->unretract(); dE != 0) {
        if (this->config.use_firmware_retraction) {
            gcode += FLAVOR_IS(gcfMachinekit) ? "G23 ; unretract\n" : "G11 ; unretract\n";
            gcode += this->reset_e();
        } else if (! m_extrusion_axis.empty()) {
            // use G1 instead of G0 because G0 will blend the restart with the previous travel move
            GCodeG1Formatter w;
            w.emit_e(m_extrusion_axis, emitE);
            w.emit_f(m_extruder->deretract_speed() * 60.);
            w.emit_comment(this->config.gcode_comments, " ; unretract");
            gcode += w.string();
        }
    }
    
    return gcode;
}

void GCodeWriter::update_position(const Vec3d &new_pos)
{
    m_pos = new_pos;
}

std::string GCodeWriter::set_fan(const GCodeFlavor gcode_flavor, bool gcode_comments, unsigned int speed)
{
    std::ostringstream gcode;
    if (speed == 0) {
        switch (gcode_flavor) {
        case gcfTeacup:
            gcode << "M106 S0"; break;
        case gcfMakerWare:
        case gcfSailfish:
            gcode << "M127";    break;
        default:
            gcode << "M107";    break;
        }
        if (gcode_comments)
            gcode << " ; disable fan";
        gcode << "\n";
    } else {
        switch (gcode_flavor) {
        case gcfMakerWare:
        case gcfSailfish:
            gcode << "M126";    break;
        case gcfMach3:
        case gcfMachinekit:
            gcode << "M106 P" << 255.0 * speed / 100.0; break;
        default:
            gcode << "M106 S" << 255.0 * speed / 100.0; break;
        }
        if (gcode_comments) 
            gcode << " ; enable fan";
        gcode << "\n";
    }
    return gcode.str();
}

std::string GCodeWriter::set_fan(unsigned int speed) const
{
    return GCodeWriter::set_fan(this->config.gcode_flavor, this->config.gcode_comments, speed);
}

void GCodeFormatter::emit_axis(const char axis, const double v, size_t digits) {
    assert(digits <= 9);
    static constexpr const std::array<int, 10> pow_10{1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
    *ptr_err.ptr++ = ' '; *ptr_err.ptr++ = axis;

    char *base_ptr = this->ptr_err.ptr;
    auto  v_int    = int64_t(std::round(v * pow_10[digits]));
    // Older stdlib on macOS doesn't support std::from_chars at all, so it is used boost::spirit::karma::generate instead of it.
    // That is a little bit slower than std::to_chars but not much.
#ifdef __APPLE__
    boost::spirit::karma::generate(this->ptr_err.ptr, boost::spirit::karma::int_generator<int64_t>(), v_int);
#else
    // this->buf_end minus 1 because we need space for adding the extra decimal point.
    this->ptr_err = std::to_chars(this->ptr_err.ptr, this->buf_end - 1, v_int);
#endif
    size_t writen_digits = (this->ptr_err.ptr - base_ptr) - (v_int < 0 ? 1 : 0);
    if (writen_digits < digits) {
        // Number is smaller than 10^digits, so that we will pad it with zeros.
        size_t remaining_digits = digits - writen_digits;
        // Move all newly inserted chars by remaining_digits to allocate space for padding with zeros.
        for (char *from_ptr = this->ptr_err.ptr - 1, *to_ptr = from_ptr + remaining_digits; from_ptr >= this->ptr_err.ptr - writen_digits; --to_ptr, --from_ptr)
            *to_ptr = *from_ptr;

        memset(this->ptr_err.ptr - writen_digits, '0', remaining_digits);
        this->ptr_err.ptr += remaining_digits;
    }

    // Move all newly inserted chars by one to allocate space for a decimal point.
    for (char *to_ptr = this->ptr_err.ptr, *from_ptr = to_ptr - 1; from_ptr >= this->ptr_err.ptr - digits; --to_ptr, --from_ptr)
        *to_ptr = *from_ptr;

    *(this->ptr_err.ptr - digits) = '.';
    for (size_t i = 0; i < digits; ++i) {
        if (*this->ptr_err.ptr != '0')
            break;
        this->ptr_err.ptr--;
    }
    if (*this->ptr_err.ptr == '.')
        this->ptr_err.ptr--;
    if ((this->ptr_err.ptr + 1) == base_ptr || *this->ptr_err.ptr == '-')
        *(++this->ptr_err.ptr) = '0';
    this->ptr_err.ptr++;

#if 0 // #ifndef NDEBUG
    {
        // Verify that the optimized formatter produces the same result as the standard sprintf().
        double v1 = atof(std::string(base_ptr, this->ptr_err.ptr).c_str());
        char buf[2048];
        sprintf(buf, "%.*lf", int(digits), v);
        double v2 = atof(buf);
        // Numbers may differ when rounding at exactly or very close to 0.5 due to numerical issues when scaling the double to an integer.
        // Thus the complex assert.
//        assert(v1 == v2);
        assert(std::abs(v1 - v) * pow_10[digits] < 0.50001);
        assert(std::abs(v2 - v) * pow_10[digits] < 0.50001);
    }
#endif // NDEBUG
}

} // namespace Slic3r
