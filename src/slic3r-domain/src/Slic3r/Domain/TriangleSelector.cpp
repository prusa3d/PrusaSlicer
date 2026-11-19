#include "Slic3r/Domain/TriangleSelector.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::Domain::TriangleSelector {

bool TriangleBitStreamMapping::operator==(const TriangleBitStreamMapping& rhs) const
{
    return this->triangle_idx == rhs.triangle_idx
        && this->bitstream_start_idx == rhs.bitstream_start_idx;
}

bool TriangleBitStreamMapping::operator!=(const TriangleBitStreamMapping& rhs) const
{
    return !(rhs == *this);
}

bool TriangleSplittingData::operator==(const TriangleSplittingData& rhs) const
{
    return this->triangles_to_split == rhs.triangles_to_split
        && this->bitstream == rhs.bitstream
        && this->used_states == rhs.used_states;
}

bool TriangleSplittingData::operator!=(const TriangleSplittingData& rhs) const
{
    return !(rhs == *this);
}

void TriangleSplittingData::reset_used_states()
{
    this->used_states.resize(TRIANGLE_STATE_TYPE_COUNT, false);
    std::fill(this->used_states.begin(), this->used_states.end(), false);
}

void TriangleSplittingData::update_used_states(const size_t bitstream_start_idx)
{
    ASSERT(bitstream_start_idx < this->bitstream.size());
    ASSERT(!this->bitstream.empty() && this->bitstream.size() != bitstream_start_idx);
    ASSERT((this->bitstream.size() - bitstream_start_idx) % 4 == 0);

    if (this->bitstream.empty() || this->bitstream.size() == bitstream_start_idx)
        return;

    size_t nibble_idx = bitstream_start_idx;

    auto read_next_nibble = [&data_bitstream = std::as_const(this->bitstream),
                             &nibble_idx]() -> uint8_t
    {
        ASSERT(nibble_idx + 3 < data_bitstream.size());
        uint8_t code = 0;
        for (size_t bit_idx = 0; bit_idx < 4; ++bit_idx) {
            code |= data_bitstream[nibble_idx++] << bit_idx;
        }

        return code;
    };

    while (nibble_idx < this->bitstream.size()) {
        const uint8_t code = read_next_nibble();

        if (const bool is_split = (code & 0b11) != 0; is_split) {
            continue;
        }

        const uint8_t facet_state = static_cast<uint8_t>(decode_leaf_state(code, read_next_nibble));
        ASSERT(facet_state < this->used_states.size());
        if (facet_state >= this->used_states.size()) {
            continue;
        }

        this->used_states[facet_state] = true;
    }
}

void TriangleSplittingData::append(const TriangleSplittingData& appended, const int facet_offset)
{
    // The mappings have to stay sorted by the facet index.
    ASSERT(
        this->triangles_to_split.empty()
        || appended.triangles_to_split.empty()
        || this->triangles_to_split.back().triangle_idx
            < appended.triangles_to_split.front().triangle_idx + facet_offset
    );

    const int bitstream_offset = static_cast<int>(this->bitstream.size());
    for (const TriangleBitStreamMapping& mapping : appended.triangles_to_split) {
        this->triangles_to_split.emplace_back(
            mapping.triangle_idx + facet_offset,
            mapping.bitstream_start_idx + bitstream_offset
        );
    }

    this->bitstream
        .insert(this->bitstream.end(), appended.bitstream.begin(), appended.bitstream.end());

    ASSERT(this->used_states.size() == appended.used_states.size());
    for (size_t state_idx = 0; state_idx < this->used_states.size(); ++state_idx) {
        this->used_states[state_idx] =
            this->used_states[state_idx] || appended.used_states[state_idx];
    }

    // All facets of an unpainted mesh are in the NONE state.
    if (appended.triangles_to_split.empty()) {
        this->used_states[static_cast<int>(TriangleStateType::NONE)] = true;
    }
}

size_t TriangleSplittingData::minimum_required_painting_version() const
{
    // States 0-16 fit into version 1 encoding (2 or 6 bits).
    // States 17-255 require version 2 (14 bits).
    return std::find(this->used_states.begin() + 17, this->used_states.end(), true)
            != this->used_states.end() ?
        2 :
        1;
}

TriangleStateType decode_leaf_state(const int code, const std::function<int()>& next_nibble)
{
    if ((code & 0b1100) != 0b1100) {
        return TriangleStateType(code >> 2);
    }

    if (const int second_nibble = next_nibble(); second_nibble != 0b1110) {
        return TriangleStateType(second_nibble + 3);
    }

    const int lo_nibble = next_nibble();
    const int hi_nibble = next_nibble();
    return TriangleStateType((lo_nibble | (hi_nibble << 4)) + 17);
}

} // namespace Slic3r::Domain::TriangleSelector
