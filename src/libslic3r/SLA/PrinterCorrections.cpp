#include "PrinterCorrections.hpp"

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/ElephantFootCompensation.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {
namespace sla {



void apply_absolute_correction(ExPolygons& slice, double absolute_correction)
{
    if (coord_t clpr_offs = scaled(absolute_correction); clpr_offs != 0)
        slice = offset_ex(slice, float(clpr_offs));
}

void apply_printer_corrections(std::vector<ExPolygons>& slices, SliceOrigin o, const std::vector<SliceRecord>& slice_records, int faded_layers,
    double elefant_foot_min_width, double elefant_foot_compensation, double absolute_correction)
{
    if (scaled(absolute_correction) != 0) {
        for (size_t i = 0; i < slice_records.size(); ++i) {
            size_t idx = slice_records[i].get_slice_idx(o);
            if (idx < slices.size())
                apply_absolute_correction(slices[idx], absolute_correction);
        }
    }

    if (double start_efc = elefant_foot_compensation; start_efc > 0.) {
        size_t faded_lyrs = size_t(faded_layers);
        double min_w = elefant_foot_min_width / 2.;
        faded_lyrs = std::min(slice_records.size(), faded_lyrs);
        size_t faded_lyrs_efc = std::max(size_t(1), faded_lyrs - 1);

        auto efc = [start_efc, faded_lyrs_efc](size_t pos) {
            return (faded_lyrs_efc - pos) * start_efc / faded_lyrs_efc;
        };    
        for (size_t i = 0; i < faded_lyrs; ++i) {
            size_t idx = slice_records[i].get_slice_idx(o);
            if (idx < slices.size())
                slices[idx] = elephant_foot_compensation(slices[idx], min_w, efc(i));
        }
    }
}


}
}
