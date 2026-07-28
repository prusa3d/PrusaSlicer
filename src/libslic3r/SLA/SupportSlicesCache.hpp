#ifndef SUPPORT_SLICES_CACHE_HPP
#define SUPPORT_SLICES_CACHE_HPP

#include <vector>
#include "libslic3r/ExPolygon.hpp"
#include "SupportTreeTypes.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {


namespace sla {

class SupportSlicesCache {
public:
    SupportSlicesCache() = default;
    SupportSlicesCache(
        SupportTreeOutput&& support_tree_output,
        std::vector<ExPolygons>&& pad_slices,
        const std::vector<float>& heights,
        const std::vector<Slic3r::SliceRecord>& slice_records,
        int faded_layers,
        double elefant_foot_min_width,
        double elefant_foot_compensation,
        double absolute_correction
    );

    ExPolygons calculate_support_slice(size_t idx) const;
    size_t size() const { return m_heights.size(); }
    bool empty() const { return m_heights.empty(); }
    double area() const;

private:
    std::vector<ExPolygons> m_support_slices_bottom;
    SupportTreeOutput m_support_tree_output;
    std::vector<float> m_heights;
    double m_absolute_correction;
};
    
}
}
#endif
