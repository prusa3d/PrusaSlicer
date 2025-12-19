///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef FIBERPRINTSTEPS_HPP
#define FIBERPRINTSTEPS_HPP

#include <libslic3r/FiberPrint.hpp>
#include <stddef.h>
#include <string>

#include "libslic3r/libslic3r.h"

namespace Slic3r {

class FiberPrintObject;

class FiberPrint::Steps
{
private:
    FiberPrint *m_print = nullptr;

public:
    // where the per object operations start and end
    static const constexpr unsigned min_objstatus = 0;
    static const constexpr unsigned max_objstatus = 70;

private:
    const size_t objcount;

    // the coefficient that multiplies the per object status values which
    // are set up for <0, 100>. They need to be scaled into the whole process
    const double objectstep_scale;

    template<class...Args> void report_status(Args&&...args)
    {
        m_print->m_report_status(*m_print, std::forward<Args>(args)...);
    }

    double current_status() const { return m_print->m_report_status.status(); }
    void throw_if_canceled() const { m_print->throw_if_canceled(); }
    bool canceled() const { return m_print->canceled(); }

public:
    explicit Steps(FiberPrint *print);

    // Placeholder methods for future implementation
    // These will be implemented in later phases as functionality is added
    
    // void slice_model(FiberPrintObject& po);
    // void fiber_placement(FiberPrintObject& po);
    // void path_planning(FiberPrintObject& po);
    // void generate_gcode();

    void execute(FiberPrintObjectStep step, FiberPrintObject &obj);
    void execute(FiberPrintStep step);

    static std::string label(FiberPrintObjectStep step);
    static std::string label(FiberPrintStep step);

    double progressrange(FiberPrintObjectStep step) const;
    double progressrange(FiberPrintStep step) const;
};

} // namespace Slic3r

#endif // FIBERPRINTSTEPS_HPP

