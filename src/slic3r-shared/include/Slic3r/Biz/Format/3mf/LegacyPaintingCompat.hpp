///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Format_3mf_LegacyPaintingCompat_hpp_
#define slic3r_Format_3mf_LegacyPaintingCompat_hpp_

namespace Slic3r::format_3MF::Legacy {

// XML attribute PrusaSlicer 2.x writes/reads on <triangle> elements, holding the
// per-triangle multi-material segmentation state as a compact string. The core-spec
// (3.x) writer additionally emits this attribute (dual-write) so that 2.x readers,
// which never look at Metadata/Slic3r_facets_annotation.json, still see MM painting.
constexpr const char* MM_SEGMENTATION_ATTR = "slic3rpe:mmu_segmentation";

// <metadata name="..."> key telling a 2.x reader which encoding version the inline
// MM_SEGMENTATION_ATTR values use (see TriangleSplittingData::minimum_required_painting_version()).
// The core-spec writer only ever dual-writes when the whole model needs version 1,
// so this is always written with value "1".
constexpr const char* MM_PAINTING_VERSION_METADATA_NAME = "slic3rpe:MmPaintingVersion";

} // namespace Slic3r::format_3MF::Legacy

#endif // slic3r_Format_3mf_LegacyPaintingCompat_hpp_
