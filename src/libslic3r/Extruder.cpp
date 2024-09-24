///|/ Copyright (c) Prusa Research 2017 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/Extruder.pm:
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Extruder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "libslic3r/GCode/GCodeWriter.hpp"
#include "PrintConfig.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

Extruder::Extruder(unsigned int id, GCodeConfig *config) :
    m_id(id),
    m_config(config)
{
    // cache values that are going to be called often
    m_e_per_mm3 = this->extrusion_multiplier();
    if (! m_config->use_volumetric_e)
        m_e_per_mm3 /= this->filament_crossection();
}

std::pair<double, double> Extruder::extrude(double dE)
{
    assert(! std::isnan(dE));
    // in case of relative E distances we always reset to 0 before any output
    if (m_config->use_relative_e_distances)
        m_E = 0.;
    // Quantize extruder delta to G-code resolution.
    dE = GCodeFormatter::quantize_e(dE);
    m_E          += dE;
    m_absolute_E += dE;
    if (dE < 0.)
        m_retracted -= dE;
    return std::make_pair(dE, m_E);
}

/* This method makes sure the extruder is retracted by the specified amount
   of filament and returns the amount of filament retracted.
   If the extruder is already retracted by the same or a greater amount, 
   this method is a no-op.
   The restart_extra argument sets the extra length to be used for
   unretraction. If we're actually performing a retraction, any restart_extra
   value supplied will overwrite the previous one if any. */
std::pair<double, double> Extruder::retract(double retract_length, double restart_extra)
{
    assert(! std::isnan(retract_length));
    assert(! std::isnan(restart_extra) && restart_extra >= 0);
    // in case of relative E distances we always reset to 0 before any output
    if (m_config->use_relative_e_distances)
        m_E = 0.;
    // Quantize extruder delta to G-code resolution.
    double to_retract = this->retract_to_go(retract_length);
    if (to_retract > 0.) {
        m_E             -= to_retract;
        m_absolute_E    -= to_retract;
        m_retracted     += to_retract;
        m_restart_extra  = restart_extra;
    }
    return std::make_pair(to_retract, m_E);
}

double Extruder::retract_to_go(double retract_length) const
{
    return std::max(0., GCodeFormatter::quantize_e(retract_length - m_retracted));
}

std::pair<double, double> Extruder::unretract()
{
    auto [dE, emitE] = this->extrude(m_retracted + m_restart_extra);
    m_retracted     = 0.;
    m_restart_extra = 0.;
    return std::make_pair(dE, emitE);
}

// Setting the retract state from the script.
// Sets current retraction value & restart extra filament amount if retracted > 0.
void Extruder::set_retracted(double retracted, double restart_extra)
{
    if (retracted < - EPSILON)
        throw Slic3r::RuntimeError("Custom G-code reports negative z_retracted.");
    if (restart_extra < - EPSILON)
        throw Slic3r::RuntimeError("Custom G-code reports negative z_restart_extra.");

    if (retracted > EPSILON) {
        m_retracted     = retracted;
        m_restart_extra = restart_extra < EPSILON ? 0 : restart_extra;
    } else {
        m_retracted     = 0;
        m_restart_extra = 0;
    }
}

// Used filament volume in mm^3.
double Extruder::extruded_volume() const
{
    return m_config->use_volumetric_e ? 
        m_absolute_E + m_retracted :
        this->used_filament() * this->filament_crossection();
}

// Used filament length in mm.
double Extruder::used_filament() const
{
    return m_config->use_volumetric_e ?
        this->extruded_volume() / this->filament_crossection() :
        m_absolute_E + m_retracted;
}

double Extruder::filament_diameter() const
{
    return m_config->filament_diameter.get_at(m_id);
}

double Extruder::filament_density() const
{
    return m_config->filament_density.get_at(m_id);
}

double Extruder::filament_cost() const
{
    return m_config->filament_cost.get_at(m_id);
}

double Extruder::extrusion_multiplier() const
{
    return m_config->extrusion_multiplier.get_at(m_id);
}

// Return a "retract_before_wipe" percentage as a factor clamped to <0, 1>
double Extruder::retract_before_wipe() const
{
    return std::min(1., std::max(0., m_config->retract_before_wipe.get_at(m_id) * 0.01));
}

double Extruder::retract_length() const
{
    return m_config->retract_length.get_at(m_id);
}

int Extruder::retract_speed() const
{
    return int(floor(m_config->retract_speed.get_at(m_id)+0.5));
}

int Extruder::deretract_speed() const
{
    int speed = int(floor(m_config->deretract_speed.get_at(m_id)+0.5));
    return (speed > 0) ? speed : this->retract_speed();
}

double Extruder::retract_restart_extra() const
{
    return m_config->retract_restart_extra.get_at(m_id);
}

double Extruder::retract_length_toolchange() const
{
    return m_config->retract_length_toolchange.get_at(m_id);
}

double Extruder::retract_restart_extra_toolchange() const
{
    return m_config->retract_restart_extra_toolchange.get_at(m_id);
}

}
