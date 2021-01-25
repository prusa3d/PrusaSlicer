#ifndef slic3r_FillSmooth_hpp_
#define slic3r_FillSmooth_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class FillSmooth : public Fill
{
public:
    FillSmooth() {
        nbPass = 2;
        anglePass[0] = 0;
        anglePass[1] = float(M_PI/2);
        anglePass[2] = 0;
        fillPattern[0] = InfillPattern::ipRectilinearWGapFill;
        fillPattern[1] = InfillPattern::ipRectilinear;
        fillPattern[2] = InfillPattern::ipRectilinear;
        rolePass[0] = erSolidInfill;
        rolePass[1] = erTopSolidInfill;
        rolePass[2] = erTopSolidInfill;
        percentWidth[0] = 1;
        percentWidth[1] = 2;
        percentWidth[2] = 1.0;
        percentFlow[0] = 0.8;
        percentFlow[1] = 0.2;
        percentFlow[2] = 0.0;
        double extrusionMult = 1.0;
        percentFlow[0] *= extrusionMult;
        percentFlow[1] *= extrusionMult;
        percentFlow[2] *= extrusionMult;
        has_overlap[0] = false;
        has_overlap[1] = true;
        has_overlap[2] = false;
    }
    virtual Fill* clone() const{ return new FillSmooth(*this); }

    virtual Polylines fill_surface(const Surface *surface, const FillParams &params) const override;
    virtual void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out) const override;
    
protected:
    int nbPass=2;
    // this parameter is now erased by fill_smooth_width when available.
    double percentWidth[3];
    // this parameter is now modified by fill_smooth_distribution when available. (note that fill_top_flow_ratio can also increasse the params.flow_mult passed by fill.cpp)
    double percentFlow[3];
    //angle to add to base angle
    float anglePass[3];
    //if false, it won't overlap inside the perimeters
    bool has_overlap[3];
    // profile for base width, speed, etc.
    ExtrusionRole rolePass[3];
    //fill algorithm to call
    InfillPattern fillPattern[3];

    void perform_single_fill(const int idx, ExtrusionEntityCollection &eecroot, const Surface &srf_source,
        const FillParams &params, const double volume) const;
    void fill_expolygon(const int idx, ExtrusionEntityCollection &eec, const Surface &srf_to_fill,
        const FillParams &params, const double volume) const;
};


class FillSmoothTriple : public FillSmooth
{
public:
    FillSmoothTriple() {
        nbPass = 1; //3
        anglePass[0] = 0;
        anglePass[1] = float(M_PI / 2);
        anglePass[2] = float(M_PI / 12); //align with nothing
        fillPattern[0] = InfillPattern::ipHilbertCurve; //ipRectilinear
        fillPattern[1] = InfillPattern::ipConcentric;
        fillPattern[2] = InfillPattern::ipRectilinear;
        rolePass[0] = erTopSolidInfill;//erSolidInfill
        rolePass[1] = erSolidInfill;
        rolePass[2] = erTopSolidInfill;
        percentWidth[0] = 1; //0.8
        percentWidth[1] = 1.5;
        percentWidth[2] = 2.8;
        percentFlow[0] = 1; //0.7
        percentFlow[1] = 0.25;
        percentFlow[2] = 0.15;
        double extrusionMult = 1.0; //slight over-extrusion
        percentFlow[0] *= extrusionMult;
        percentFlow[1] *= extrusionMult;
        percentFlow[2] *= extrusionMult;
        has_overlap[0] = true;
        has_overlap[1] = true;
        has_overlap[2] = true;

    }
    virtual Fill* clone() const { return new FillSmoothTriple(*this); }

};

class FillSmoothHilbert : public FillSmooth
{
public:
    FillSmoothHilbert() {
        nbPass = 2;
        anglePass[0] = 0;
        anglePass[1] = float(M_PI / 4);
        anglePass[2] = float(M_PI / 4);
        fillPattern[0] = InfillPattern::ipHilbertCurve; //ipHilbertCurve
        fillPattern[1] = InfillPattern::ipHilbertCurve;
        fillPattern[2] = InfillPattern::ipRectilinear;
        rolePass[0] = erSolidInfill;
        rolePass[1] = erTopSolidInfill;
        rolePass[2] = erTopSolidInfill;
        percentWidth[0] = 1;
        percentWidth[1] = 1.5;
        percentWidth[2] = 1.0;
        percentFlow[0] = 1;
        percentFlow[1] = 0.0;
        percentFlow[2] = 0.0;
        double extrusionMult = 1.0;
        percentFlow[0] *= extrusionMult;
        percentFlow[1] *= extrusionMult;
        percentFlow[2] *= extrusionMult;
        has_overlap[0] = true;
        has_overlap[1] = false;
        has_overlap[2] = true;

    }
    virtual Fill* clone() const { return new FillSmoothHilbert(*this); }

};


} // namespace Slic3r

#endif // slic3r_FillSmooth_hpp_
