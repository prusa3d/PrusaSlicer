#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;
class SegmentedIntersectionLine;
struct ExPolygonWithOffset;

class FillRectilinear : public Fill
{
public:
    Fill* clone() const override { return new FillRectilinear(*this); };
    ~FillRectilinear() override = default;
    virtual void init_spacing(coordf_t spacing, const FillParams& params) override;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    virtual std::vector<SegmentedIntersectionLine> _vert_lines_for_polygon(const ExPolygonWithOffset& poly_with_offset, const BoundingBox& bounding_box, const FillParams& params, coord_t line_spacing) const;

    // Fill by single directional lines, interconnect the lines along perimeters.
    bool fill_surface_by_lines(const Surface* surface, const FillParams& params, float angleBase, float pattern_shift, Polylines& polylines_out) const;

    // Fill by multiple sweeps of differing directions.
    struct SweepParams {
        float angle_base;
        float pattern_shift;
    };
    bool fill_surface_by_multilines(const Surface* surface, FillParams params, const std::initializer_list<SweepParams>& sweep_params, Polylines& polylines_out) const;
};

class FillMonotonic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonic(*this); };
    ~FillMonotonic() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;
    bool no_sort() const override { return true; }
};

class FillGrid : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillGrid(*this); };
    ~FillGrid() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillTriangles : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillTriangles(*this); };
    ~FillTriangles() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillStars : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillStars(*this); };
    ~FillStars() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCubic(*this); };
    ~FillCubic() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillRectilinearPeri : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillRectilinearPeri(*this); };
    ~FillRectilinearPeri() override = default;
    //Polylines fill_surface(const Surface *surface, const FillParams &params);
    void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out) const override;

};


class FillScatteredRectilinear : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillScatteredRectilinear(*this); };
    ~FillScatteredRectilinear() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) const override;

protected:
    float _layer_angle(size_t idx) const override;
    std::vector<SegmentedIntersectionLine> _vert_lines_for_polygon(const ExPolygonWithOffset& poly_with_offset, const BoundingBox& bounding_box, const FillParams& params, coord_t line_spacing) const override;
    coord_t _line_spacing_for_density(float density) const override;
};

class FillRectilinearSawtooth : public FillRectilinear {
public:

    Fill* clone() const override { return new FillRectilinearSawtooth(*this); };
    ~FillRectilinearSawtooth() override = default;
    void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out) const override;

};

class FillRectilinearWGapFill : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillRectilinearWGapFill(*this); };
    ~FillRectilinearWGapFill() override = default;
    void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out) const override;
    static void split_polygon_gap_fill(const Surface& surface, const FillParams& params, ExPolygons& rectilinear, ExPolygons& gapfill);
protected:
    virtual bool is_monotonic() const { return false;  }
};

class FillMonotonicWGapFill : public FillRectilinearWGapFill
{
public:

    Fill* clone() const override { return new FillMonotonicWGapFill(*this); };
    ~FillMonotonicWGapFill() override = default;
protected:
    virtual bool is_monotonic() const override { return true; }
};


}; // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
