///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena
///|/ Copyright (c) Slic3r 2016 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/Fill/Concentric.pm:
///|/ Copyright (c) Prusa Research 2016 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

#include <stddef.h>
#include <initializer_list>
#include <cstddef>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r {

class PrintRegionConfig;
class Surface;

class FillRectilinear : public Fill
{
public:
    Fill* clone() const override { return new FillRectilinear(*this); }
    ~FillRectilinear() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return false; }

protected:
    // Fill by single directional lines, interconnect the lines along perimeters.
	bool fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out);


    // Fill by multiple sweeps of differing directions.
    struct SweepParams {
        float angle_base;
        float pattern_shift;
    };
    bool fill_surface_by_multilines(const Surface *surface, FillParams params, const std::initializer_list<SweepParams> &sweep_params, Polylines &polylines_out);

    // The extended bounding box of the whole object that covers any rotation of every layer.
    BoundingBox extended_object_bounding_box() const;
};

class FillAlignedRectilinear : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillAlignedRectilinear(*this); }
    ~FillAlignedRectilinear() override = default;

protected:
    // Always generate infill at the same angle.
    virtual float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillMonotonic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonic(*this); }
    ~FillMonotonic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
	bool no_sort() const override { return true; }
};

class FillMonotonicLines : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonicLines(*this); }
    ~FillMonotonicLines() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool no_sort() const override { return true; }
};

class FillGrid : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillGrid(*this); }
    ~FillGrid() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillTriangles : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillTriangles(*this); }
    ~FillTriangles() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillStars : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillStars(*this); }
    ~FillStars() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCubic(*this); }
    ~FillCubic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillSupportBase : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillSupportBase(*this); }
    ~FillSupportBase() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillZigZag : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillZigZag(*this); }
    ~FillZigZag() override = default;

    bool has_consistent_pattern() const override { return true; }
};

Points sample_grid_pattern(const ExPolygon &expolygon, coord_t spacing, const BoundingBox &global_bounding_box);
Points sample_grid_pattern(const ExPolygons &expolygons, coord_t spacing, const BoundingBox &global_bounding_box);
Points sample_grid_pattern(const Polygons &polygons, coord_t spacing, const BoundingBox &global_bounding_box);

} // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
