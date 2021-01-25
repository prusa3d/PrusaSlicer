#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class GCodeConfig;

class Tool
{
public:
    Tool(uint16_t id, GCodeConfig *config);
    virtual ~Tool() {}

    void   reset() {
        m_E             = 0;
        m_absolute_E    = 0;
        m_retracted     = 0;
        m_restart_extra = 0;
    }

    uint16_t id() const { return m_id; }

    virtual double extrude(double dE);
    virtual double retract(double length, double restart_extra);
    virtual double unretract();
    double E() const { return m_E; }
    void   reset_E() { m_E = 0.; }
    double e_per_mm(double mm3_per_mm) const { return mm3_per_mm * m_e_per_mm3; }
    double e_per_mm3() const { return m_e_per_mm3; }
    // Used filament volume in mm^3.
    virtual double extruded_volume() const;
    // Used filament length in mm.
    virtual double used_filament() const;
    
    virtual double filament_diameter() const;
    double filament_crossection() const { return this->filament_diameter() * this->filament_diameter() * 0.25 * PI; }
    virtual double filament_density() const;
    virtual double filament_cost() const;
    virtual double extrusion_multiplier() const;
    virtual double retract_before_wipe() const;
    virtual double retract_length() const;
    virtual double retract_lift() const;
    virtual int    retract_speed() const;
    virtual int    deretract_speed() const;
    virtual double retract_restart_extra() const;
    virtual double retract_length_toolchange() const;
    virtual double retract_restart_extra_toolchange() const;
    virtual int    temp_offset() const;
    virtual int    fan_offset() const;

protected:
    // Private constructor to create a key for a search in std::set.
    Tool(uint16_t id) : m_id(id) {}

    // Reference to GCodeWriter instance owned by GCodeWriter.
    GCodeConfig *m_config;
    // Print-wide global ID of this extruder.
    uint16_t    m_id;
    // Current state of the extruder axis, may be resetted if use_relative_e_distances.
    double       m_E;
    // Current state of the extruder tachometer, used to output the extruded_volume() and used_filament() statistics.
    double       m_absolute_E;
    // Current positive amount of retraction.
    double       m_retracted;
    // When retracted, this value stores the extra amount of priming on deretraction.
    double       m_restart_extra;
    double       m_e_per_mm3;
};


class Mill : public Tool
{
public:
    Mill(uint16_t mill_id, GCodeConfig* config);
    virtual ~Mill() {}
    double retract_lift() const override;

    uint16_t mill_id() const { return m_mill_id; }

protected:
    // Private constructor to create a key for a search in std::set.
    Mill(uint16_t tool_id) : Tool(tool_id) {}
    uint16_t    m_mill_id;
}; 

class Extruder : public Tool
{
public:
    Extruder(uint16_t id, GCodeConfig* config);
    virtual ~Extruder() {}

    double filament_diameter() const override;
    double filament_density() const override;
    double filament_cost() const override;
    double extrusion_multiplier() const override;
    double retract_before_wipe() const override;
    double retract_length() const override;
    double retract_lift() const override;
    int    retract_speed() const override;
    int    deretract_speed() const override;
    double retract_restart_extra() const override;
    double retract_length_toolchange() const override;
    double retract_restart_extra_toolchange() const override;
    int    temp_offset() const override;
    int    fan_offset() const override;

protected:
    // Private constructor to create a key for a search in std::set.
    Extruder(uint16_t id) : Tool(id) {}
};

// Sort Extruder objects by the extruder id by default.
inline bool operator==(const Tool& e1, const Tool& e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Tool& e1, const Tool& e2) { return e1.id() != e2.id(); }
inline bool operator< (const Tool& e1, const Tool& e2) { return e1.id() < e2.id(); }
inline bool operator> (const Tool& e1, const Tool& e2) { return e1.id() > e2.id(); }
inline bool operator==(const Extruder& e1, const Extruder& e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Extruder& e1, const Extruder& e2) { return e1.id() != e2.id(); }
inline bool operator< (const Extruder& e1, const Extruder& e2) { return e1.id() < e2.id(); }
inline bool operator> (const Extruder& e1, const Extruder& e2) { return e1.id() > e2.id(); }

}

#endif
