#include "Extruder.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

Tool::Tool(uint16_t id, GCodeConfig* config) :
    m_id(id),
    m_config(config)
{
    reset();
}

Extruder::Extruder(uint16_t id, GCodeConfig* config) :
    Tool(id, config)
{

    // cache values that are going to be called often
    m_e_per_mm3 = this->extrusion_multiplier();
    if (!m_config->use_volumetric_e)
        m_e_per_mm3 /= this->filament_crossection();
}

Mill::Mill(uint16_t mill_id, GCodeConfig* config) :
    Tool(mill_id, config)
{
    m_mill_id = mill_id;
    m_id = mill_id + (uint16_t)config->retract_length.values.size();
}

double Tool::extrude(double dE)
{
    // in case of relative E distances we always reset to 0 before any output
    if (m_config->use_relative_e_distances)
        m_E = 0.;
    m_E          += dE;
    m_absolute_E += dE;
    if (dE < 0.)
        m_retracted -= dE;
    return dE;
}

/* This method makes sure the extruder is retracted by the specified amount
   of filament and returns the amount of filament retracted.
   If the extruder is already retracted by the same or a greater amount, 
   this method is a no-op.
   The restart_extra argument sets the extra length to be used for
   unretraction. If we're actually performing a retraction, any restart_extra
   value supplied will overwrite the previous one if any. */
double Tool::retract(double length, double restart_extra)
{
    // in case of relative E distances we always reset to 0 before any output
    if (m_config->use_relative_e_distances)
        m_E = 0.;
    double to_retract = std::max(0., length - m_retracted);
    if (to_retract > 0.) {
        m_E             -= to_retract;
        m_absolute_E    -= to_retract;
        m_retracted     += to_retract;
        m_restart_extra = restart_extra;
    }
    return to_retract;
}

double Tool::unretract()
{
    double dE = m_retracted + m_restart_extra;
    this->extrude(dE);
    m_retracted     = 0.;
    m_restart_extra = 0.;
    return dE;
}

// Used filament volume in mm^3.
double Tool::extruded_volume() const
{
    return m_config->use_volumetric_e ? 
        m_absolute_E + m_retracted :
        this->used_filament() * this->filament_crossection();
}

// Used filament length in mm.
double Tool::used_filament() const
{
    return m_config->use_volumetric_e ?
        this->extruded_volume() / this->filament_crossection() :
        m_absolute_E + m_retracted;
}

double Tool::filament_diameter() const
{
    return 0;
}

double Tool::filament_density() const
{
    return 0;
}

double Tool::filament_cost() const
{
    return 0;
}

double Tool::extrusion_multiplier() const
{
    return 0;
}

// Return a "retract_before_wipe" percentage as a factor clamped to <0, 1>
double Tool::retract_before_wipe() const
{
    return 0;
}

double Tool::retract_length() const
{
    return 0;
}

double Tool::retract_lift() const
{
    return 0;
}

int Tool::retract_speed() const
{
    return 0;
}

int Tool::deretract_speed() const
{
    return 0;
}

double Tool::retract_restart_extra() const
{
    return 0;
}

double Tool::retract_length_toolchange() const
{
    return 0;
}

double Tool::retract_restart_extra_toolchange() const
{
    return 0;
}

int Tool::temp_offset() const
{
    return 0;
}

int Tool::fan_offset() const
{
    return 0;
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

double Extruder::retract_lift() const
{
    return m_config->retract_lift.get_at(m_id);
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

int Extruder::temp_offset() const
{
    return m_config->extruder_temperature_offset.get_at(m_id);
}

int Extruder::fan_offset() const
{
    return m_config->extruder_fan_offset.get_at(m_id);
}

double Mill::retract_lift() const {
    return m_config->milling_z_lift.get_at(m_mill_id);
}

}
