#ifndef PRINTER_CORRECTIONS_HPP
#define PRINTER_CORRECTIONS_HPP

#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {
namespace sla {

void apply_absolute_correction(ExPolygons& slice, double absolute_correction);

void apply_printer_corrections(std::vector<ExPolygons>& slices, SliceOrigin o, const std::vector<SliceRecord>& slice_records, int faded_layers,
    double elefant_foot_min_width, double elefant_foot_compensation, double absolute_correction);

}
}
#endif