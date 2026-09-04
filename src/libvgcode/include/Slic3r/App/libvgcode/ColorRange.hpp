#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <cfloat>

namespace Slic3r::App::libvgcode {

class FdmViewer;

//
// Color range types
//
enum class ColorRangeType : uint8_t
{
    Linear,
    Logarithmic,
    COUNT
};

static constexpr size_t COLOR_RANGE_TYPES_COUNT = size_t(ColorRangeType::COUNT);

static const Palette DEFAULT_RANGES_COLORS {{
    { 0.043f, 0.173f, 0.478f }, // bluish
    { 0.075f, 0.349f, 0.522f },
    { 0.110f, 0.533f, 0.569f },
    { 0.016f, 0.839f, 0.059f },
    { 0.667f, 0.949f, 0.000f },
    { 0.988f, 0.976f, 0.012f },
    { 0.961f, 0.808f, 0.039f },
    { 0.890f, 0.533f, 0.125f },
    { 0.820f, 0.408f, 0.188f },
    { 0.761f, 0.322f, 0.235f },
    { 0.580f, 0.149f, 0.086f }  // reddish
}};

class ColorRange
{
public:
    //
    // Constructor
    //
    explicit ColorRange(ColorRangeType type = ColorRangeType::Linear);
    //
    // Return the type of this ColorRange.
    //
    ColorRangeType type() const;
    //
    // Return the palette used by this ColorRange.
    // Default is DEFAULT_RANGES_COLORS
    //
    const Palette& palette() const;
    //
    // Set the palette to be used by this ColorRange.
    // The given palette must contain at least two colors.
    //
    void set_palette(const Palette& palette);
    //
    // Return the interpolated color at the given value.
    // Value is clamped to [get_range()[0]..get_range()[1]].
    //
    Domain::ColorRGB color_at(float value) const;
    //
    // Return the range of this ColorRange.
    // The range is detected during the call to Viewer::load().
    // [0] -> min
    // [1] -> max
    //
    const std::array<float, 2>& range() const;
    //
    // Return the values corresponding to the detected color bins of this ColorRange.
    // The size of the returned vector can be:
    // 1                - If only one value was detected while setting up this ColorRange.
    // 2                - If only two values were detected while setting up this ColorRange.
    // palette().size() - If more than two distinct values were detected while setting up this ColorRange.
    //
    std::vector<float> values() const;

    static const ColorRange DUMMY_COLOR_RANGE;

private:
    ColorRangeType m_type{ ColorRangeType::Linear };
    //
    // The palette used by this ColorRange
    // 
    Palette m_palette;
    //
    // [0] = min
    // [1] = max
    //
    std::array<float, 2> m_range{ FLT_MAX, -FLT_MAX };
    //
    // Count of different values passed to update()
    // 
    size_t m_count{ 0 };

    //
    // Use the passed value to update the range.
    //
    void update(float value);
    //
    // Reset the range
    // Call this method before reuse an instance of ColorRange.
    //
    void reset();

    friend class FdmViewer;
};

} // namespace Slic3r::App::libvgcode
