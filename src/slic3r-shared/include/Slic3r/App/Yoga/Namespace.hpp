#pragma once

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Assert.hpp"

#include "yoga/Yoga.h"
#include <imgui_internal.h>

#include <functional>

namespace Slic3r::App::Yoga {
using Vec2f = Slic3r::Domain::Vec2f;
using Vec4f = Slic3r::Domain::Vec4f;

// Used for rendering and sizing
using RenderPosFn = std::function<void(Vec2f, Vec2f)>;
using RenderFn    = std::function<void(Vec2f)>;

enum class Orientation
{
    Horizontal,
    Vertical,
    VerticalReverse,
};

enum class Direction
{
    LeftToRight,
    RightToLeft
};

enum class Position
{
    Top,
    Bottom,
    Left,
    Right
};

struct Unit
{
    enum class Type
    {
        Pixel, ///< This is a logical pixel, meaning WxWidgets are already converting from physical pixels
        FigmaPixel, ///< This is value as taken from Figma which has different root font size
        Point,
        // Milimeter,
        Rem, ///< Not really M-size but Root font size
        ViewportWidth, ///< % of Viewport x-width size
        ViewportHeight, ///< % of Viewport y-height size
        ViewportMin, ///< % of Viewport smaller size
        ViewportMax ///< % of Viewport larger size
    };

    constexpr Unit(float value = 0, Type type = Type::Pixel) : value(value), type(type) {}

    bool operator==(const Unit& other) const;
    bool operator!=(const Unit& other) const;

    Unit operator-() const
    {
        return Unit{-value, type};
    }

    Unit& operator*=(float scalar)
    {
        value *= scalar;
        return *this;
    }

    Unit& operator+=(Unit& unit)
    {
        ASSERT(unit.type == type);
        value += unit.value;
        return *this;
    }

    Unit& operator-=(Unit& unit)
    {
        ASSERT(unit.type == type);
        value -= unit.value;
        return *this;
    }

    Unit& operator/=(float scalar)
    {
        value /= scalar;
        return *this;
    }

    float value = 0;
    Type type;
};

Unit operator*(Unit unit, float scalar);
Unit operator*(float scalar, Unit unit);
Unit operator/(Unit unit, float scalar);
Unit operator+(Unit lhs, Unit rhs);
Unit operator-(Unit lhs, Unit rhs);
Unit operator*(Unit lhs, Unit rhs);
Unit operator/(Unit lhs, Unit rhs);

constexpr Unit operator""_px(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::Pixel);
}

constexpr Unit operator""_px(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::Pixel);
}

constexpr Unit operator""_fpx(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::FigmaPixel);
}

constexpr Unit operator""_fpx(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::FigmaPixel);
}

constexpr Unit operator""_pt(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::Point);
}

constexpr Unit operator""_pt(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::Point);
}

constexpr Unit operator""_rem(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::Rem);
}

constexpr Unit operator""_rem(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::Rem);
}

constexpr Unit operator""_ww(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportWidth);
}

constexpr Unit operator""_ww(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportWidth);
}

constexpr Unit operator""_wh(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportHeight);
}

constexpr Unit operator""_wh(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportHeight);
}

constexpr Unit operator""_wmin(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportMin);
}

constexpr Unit operator""_wmin(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportMin);
}

constexpr Unit operator""_wmax(long double value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportMax);
}

constexpr Unit operator""_wmax(unsigned long long value)
{
    return Unit(static_cast<float>(value), Unit::Type::ViewportMax);
}

struct Sides
{
    Sides(float size) : Sides(Unit{size}) {}

    Sides(const Unit& size = {})
    {
        left = right = top = bottom = size;
    }

    Sides(const Unit& horizontal, const Unit& vertical)
    {
        left = right = horizontal;
        top = bottom = vertical;
    }

    Sides(const Unit& left, const Unit& top, const Unit& right, const Unit& bottom) :
        left(left),
        right(right),
        top(top),
        bottom(bottom)
    {}

    inline Unit horizontal() const
    {
        ASSERT(left.type == right.type);
        return {left.value + right.value, left.type};
    }

    inline Unit vertical() const
    {
        ASSERT(top.type == bottom.type);
        return {top.value + bottom.value, top.type};
    }

    inline bool operator==(const Sides& rhs) const
    {
        return left == rhs.left && right == rhs.right && top == rhs.top && bottom == rhs.bottom;
    }

    inline bool operator!=(const Sides& rhs) const
    {
        return !(*this == rhs);
    }

    Unit left{0.f};
    Unit right{0.f};
    Unit top{0.f};
    Unit bottom{0.f};
};

using Margins  = Sides;
using Paddings = Sides;

enum class AlignH : int
{
    Left,
    Center,
    Right
};

enum class AlignV
{
    Top,
    Center,
    Bottom
};

enum class AlignU
{
    Start,
    Center,
    End
};

struct Align
{
    AlignH horizontal{AlignH::Left};
    AlignV vertical{AlignV::Top};

    bool operator==(const Align& rhs) const
    {
        return horizontal == rhs.horizontal && vertical == rhs.vertical;
    }

    bool operator!=(const Align& rhs) const
    {
        return !(*this == rhs);
    }

    YGAlign get_yoga_v_align() const;
    YGAlign get_yoga_h_align() const;
};

/**
 * Nearly identical to ScreenInfo but optimized for Yoga::Item tree
 */
struct SizeInfo
{
    int dpi{0}; ///<! convert 2D DPI to scalar value for convenience
    float dpi_scale_factor{1.f};
    // Viewport size is stored as integer for faster comparison purposes
    int viewport_size_x{0};
    int viewport_size_y{0};
    float root_font_size{0}; ///<! Same as ImGuiStyle::FontSizeBase
    float root_font_size_pt{0}; ///<! Original value

    int viewport_max() const;
    int viewport_min() const;

    bool operator==(const SizeInfo& other) const;
    bool operator!=(const SizeInfo& other) const;
};

/**
 * @brief Unit + result in logical pixels
 */
struct EvaluatedUnit
{
    Unit source;
    float result{};

    explicit EvaluatedUnit(const Unit& source = {0});

    void evaluate(const SizeInfo& size_info);
};

/**
 * @brief Sides + resul in logical pixels
 */
struct EvaluatedSides
{
    Sides source;

    float left   = 0;
    float right  = 0;
    float top    = 0;
    float bottom = 0;

    float result_horizontal() const;
    float result_vertical() const;

    void evaluate(const SizeInfo& size_info);
};

using EvaluatedMargins  = EvaluatedSides;
using EvaluatedPaddings = EvaluatedSides;

} // namespace Slic3r::App::Yoga
