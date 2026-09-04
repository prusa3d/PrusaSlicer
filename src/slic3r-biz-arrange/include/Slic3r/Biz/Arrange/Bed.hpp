#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Bed.hpp"

namespace Slic3r::Biz::Arrange {
struct IBed
{
    virtual ~IBed()                                                                = default;
    virtual Domain::BoundingBox2crd bounding_box() const                           = 0;
    virtual Domain::ExPolygons ifp_convex(const Domain::Polygon& convexpoly) const = 0;
    virtual double area() const                                                    = 0;
};

struct InfiniteBed : public IBed
{
    InfiniteBed(const Domain::Vec2crd& center);
    Domain::BoundingBox2crd bounding_box() const final;
    Domain::ExPolygons ifp_convex(const Domain::Polygon& convexpoly) const final;
    double area() const final;

private:
    Domain::Point m_center;
};

enum class PivotPoint
{
    Center,
    BottomLeft,
    BottomRight,
    TopLeft,
    TopRight
};

struct RectangleBed : public IBed
{
    RectangleBed(
        const Domain::BoundingBox2crd& bb,
        const PivotPoint pivot_point         = {},
        const Domain::BedSegments segments   = {}
    );

    Domain::BoundingBox2crd bounding_box() const final;
    Domain::ExPolygons ifp_convex(const Domain::Polygon& convexpoly) const final;
    double area() const final;
    PivotPoint pivot_point() const;
    Domain::BedSegments segments() const;

private:
    Domain::BoundingBox2crd m_bb;
    PivotPoint m_pivot_point;
    Domain::BedSegments m_segments;
};

struct CircleBed : public IBed
{
    CircleBed(const Domain::Point center, const double radius);

    Domain::BoundingBox2crd bounding_box() const final;
    Domain::ExPolygons ifp_convex(const Domain::Polygon& convexpoly) const final;
    double area() const final;

private:
    Domain::Point m_center;
    double m_radius{};
};

struct IrregularBed : public IBed
{
    IrregularBed(const Domain::ExPolygons& poly);
    Domain::BoundingBox2crd bounding_box() const final;
    Domain::ExPolygons ifp_convex(const Domain::Polygon& convexpoly) const final;
    double area() const final;

private:
    Domain::ExPolygons m_poly;
};

} // namespace Slic3r::Biz::Arrange
