///|/ Copyright (c) Prusa Research 2017 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/Extruder.pm:
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include <utility>

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class GCodeConfig;

class Extruder
{
public:
    Extruder(unsigned int id, GCodeConfig *config);
    ~Extruder() = default;

    unsigned int id() const { return m_id; }

    // Following three methods emit:
    // first  - extrusion delta
    // second - number to emit to G-code: This may be delta for relative mode or a distance from last reset_E() for absolute mode.
    // They also quantize the E axis to G-code resolution.
    std::pair<double, double> extrude(double dE);
    std::pair<double, double> retract(double retract_length, double restart_extra);
    std::pair<double, double> unretract();
    // How much to retract yet before retract_length is reached?
    // The value is quantized to G-code resolution.
    double                    retract_to_go(double retract_length) const;

    // Reset the current state of the E axis (this is only needed for relative extruder addressing mode anyways).
    // Returns true if the extruder was non-zero before reset.
    bool   reset_E() { bool modified = m_E != 0; m_E = 0.; return modified; }
    double e_per_mm(double mm3_per_mm) const { return mm3_per_mm * m_e_per_mm3; }
    double e_per_mm3() const { return m_e_per_mm3; }
    // Used filament volume in mm^3.
    double extruded_volume() const;
    // Used filament length in mm.
    double used_filament() const;

    // Getters for the PlaceholderParser.
    // Get current extruder position. Only applicable with absolute extruder addressing.
    double position() const { return m_E; }
    // Get current retraction value. Only non-negative values.
    double retracted() const { return m_retracted; }
    // Get extra retraction planned after
    double restart_extra() const { return m_restart_extra; }
    // Setters for the PlaceholderParser.
    // Set current extruder position. Only applicable with absolute extruder addressing.
    void   set_position(double e) { m_E = e; }
    // Sets current retraction value & restart extra filament amount if retracted > 0.
    void   set_retracted(double retracted, double restart_extra);
    
    double filament_diameter() const;
    double filament_crossection() const { return this->filament_diameter() * this->filament_diameter() * 0.25 * PI; }
    double filament_density() const;
    double filament_cost() const;
    double extrusion_multiplier() const;
    double retract_before_wipe() const;
    double retract_length() const;
    double retract_lift() const;
    int    retract_speed() const;
    int    deretract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;

private:
    // Private constructor to create a key for a search in std::set.
    Extruder(unsigned int id) : m_id(id) {}

    // Reference to GCodeWriter instance owned by GCodeWriter.
    GCodeConfig *m_config;
    // Print-wide global ID of this extruder.
    unsigned int m_id;
    // Current state of the extruder axis.
    // For absolute extruder addressing, it is the current state since the last reset (G92 E0) issued at the end of the last retraction.
    // For relative extruder addressing, it is the E axis difference emitted into the G-code the last time.
    double       m_E { 0 };
    // Current state of the extruder tachometer, used to output the extruded_volume() and used_filament() statistics.
    double       m_absolute_E { 0 };
    // Current positive amount of retraction.
    double       m_retracted { 0 };
    // When retracted, this value stores the extra amount of priming on deretraction.
    double       m_restart_extra { 0 };
    double       m_e_per_mm3;
};

// Sort Extruder objects by the extruder id by default.
inline bool operator==(const Extruder &e1, const Extruder &e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Extruder &e1, const Extruder &e2) { return e1.id() != e2.id(); }
inline bool operator< (const Extruder &e1, const Extruder &e2) { return e1.id() < e2.id(); }
inline bool operator> (const Extruder &e1, const Extruder &e2) { return e1.id() > e2.id(); }

}

#endif // slic3r_Extruder_hpp_
