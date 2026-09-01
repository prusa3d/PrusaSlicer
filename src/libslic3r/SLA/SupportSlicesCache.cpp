#include "SupportSlicesCache.hpp"

#include "SupportTreeSlicer.hpp"
#include "PrinterCorrections.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "boost/log/trivial.hpp"

namespace Slic3r {
namespace sla {

SupportSlicesCache::SupportSlicesCache(
        SupportTreeOutput&& support_tree_output,
        std::vector<ExPolygons>&& pad_slices,
        const std::vector<float>& heights,
        const std::vector<SliceRecord>& slice_records,
        int faded_layers,
        double elefant_foot_min_width,
        double elefant_foot_compensation,
        double absolute_correction
    )
    : m_support_tree_output{std::move(support_tree_output)},
      m_support_slices_bottom{std::move(pad_slices)},
      m_heights{heights},
      m_absolute_correction{absolute_correction}
{
    // Calculate how many layers should be precalculated (all layers containing pad and needing elephant foot compensation).
    size_t bottom_layers_num = size_t(std::min(int(m_heights.size()), std::max(faded_layers, int(m_support_slices_bottom.size()))));
    std::vector<float> heights_temp = heights;
    heights_temp.resize(std::min(heights_temp.size(), bottom_layers_num));

    // Slice and save supports for all layers in heights_temp. Merge with pad slices.
    std::vector<ExPolygons> slices = slice_support_tree(m_support_tree_output, heights_temp);
    m_support_slices_bottom.resize(std::max(m_support_slices_bottom.size(), slices.size()));
    for (size_t i = 0; i < slices.size(); ++i)
        for (ExPolygon& exp : slices[i])
            m_support_slices_bottom[i].emplace_back(std::move(exp));
    
    // Apply xy corrections on the saved slices.
    apply_printer_corrections(m_support_slices_bottom, SliceOrigin::soSupport, slice_records, faded_layers,
        elefant_foot_min_width, elefant_foot_compensation, absolute_correction);
}



ExPolygons SupportSlicesCache::calculate_support_slice(size_t idx) const
{
    if (idx >= m_heights.size())
        return ExPolygons{};

    // Try to get the slice from cache first.
    if (idx < m_support_slices_bottom.size())
        return m_support_slices_bottom[idx];

    // It was not in cache - we have to calculate the slice from scratch and apply
    // printer correction. We assume that all layers that needed elephant foot
    // corrections were in the cache, so we only apply absolute correction.
    ExPolygons slice = slice_support_tree_at_height(m_support_tree_output, m_heights[idx]);
    apply_absolute_correction(slice, m_absolute_correction);
    return slice;
}



double SupportSlicesCache::area() const
{
    return calculate_supports_area(m_support_tree_output);    
}

} // namespace sla
} // namespace Slic3r
