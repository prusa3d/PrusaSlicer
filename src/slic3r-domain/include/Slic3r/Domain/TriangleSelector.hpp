#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace cereal {
class access;
} // namespace cereal

namespace Slic3r::Domain::TriangleSelector {

enum class TriangleStateType : uint8_t
{
    // Maximum is 3. The value is serialized in TriangleSelector into 2 bits.
    NONE     = 0,
    ENFORCER = 1,
    BLOCKER  = 2,
    // For the fuzzy skin, we use just two values (NONE and FUZZY_SKIN).
    FUZZY_SKIN = ENFORCER,
    // Maximum is 254. The value is serialized in TriangleSelector into 2, 6, or 14 bits using prefix codes.
    Extruder1 = ENFORCER,
    Extruder2 = BLOCKER,
    Extruder3,
    Extruder4,
    Extruder5,
    Extruder6,
    Extruder7,
    Extruder8,
    Extruder9,
    Extruder10,
    Extruder11,
    Extruder12,
    Extruder13,
    Extruder14,
    Extruder15,
    Extruder16
};

inline constexpr size_t TRIANGLE_STATE_TYPE_COUNT = 256;

struct TriangleBitStreamMapping
{
    // Index of the triangle to which we assign the bitstream containing splitting information.
    int triangle_idx = -1;
    // Index of the first bit of the bitstream assigned to this triangle.
    int bitstream_start_idx = -1;

    TriangleBitStreamMapping() = default;

    explicit TriangleBitStreamMapping(int triangleIdx, int bitstreamStartIdx) :
        triangle_idx(triangleIdx),
        bitstream_start_idx(bitstreamStartIdx)
    {}

    bool operator==(const TriangleBitStreamMapping& rhs) const;
    bool operator!=(const TriangleBitStreamMapping& rhs) const;
};

struct TriangleSplittingData
{
    // Vector of triangles and its indexes to the bitstream.
    std::vector<TriangleBitStreamMapping> triangles_to_split;
    // Bit stream containing splitting information.
    std::vector<bool> bitstream;
    // Array indicating which triangle state types are used (encoded inside bitstream).
    std::vector<bool> used_states{std::vector<bool>(TRIANGLE_STATE_TYPE_COUNT, false)};

    TriangleSplittingData() = default;

    bool operator==(const TriangleSplittingData& rhs) const;
    bool operator!=(const TriangleSplittingData& rhs) const;

    /**
     * Reset all used states before they are recomputed based on the bitstream.
     */
    void reset_used_states();

    /**
     * Update used states based on the bitstream. It just iterated over the bitstream from the bitstream_start_idx till the end.
     */
    void update_used_states(std::size_t bitstream_start_idx);

    /**
     * Returns the minimum painting version required to represent this data.
     * Version 1: Only states 0-16 are used (compatible with PrusaSlicer 2.9.4 and older).
     * Version 2: States 17-255 are used (requires the extended serialization format).
     */
    size_t minimum_required_painting_version() const;
};

/**
 * Decode leaf triangle state from the first nibble's code using prefix codes:
 *   xx != 0b11:                 state = xx (states 0-2)
 *   xx == 0b11, zzzz != 0b1110: state = zzzz + 3 (states 3-16)
 *   xx == 0b11, zzzz == 0b1110: state = next 8 bits + 17 (states 17-255)
 */
TriangleStateType decode_leaf_state(int code, const std::function<int()>& next_nibble);

} // namespace Slic3r::Domain::TriangleSelector
