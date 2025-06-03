///|/ Copyright (c) Prusa Research 2018 - 2021 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/ Copyright (c) SuperSlicer 2018 - 2019 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstddef>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "FillShwartzDiamond.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

static double scaled_floor(double x,double scale){
	return std::floor(x/scale)*scale;
}

static Polylines make_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    // tolerance in scaled units. clamp the maximum tolerance as there's
    // no processing-speed benefit to do so beyond a certain point
    const double tolerance = std::min(line_spacing / 2, FillShwartzDiamond::PatternTolerance) / unscale<double>(scaleFactor);

    //scale factor for 5% : 8 712 388
    // 1z = 10^-6 mm ?
    const double z = gridZ / scaleFactor;
    Polylines result;

	//sin(x)*sin(y)*sin(z)-cos(x)*cos(y)*cos(z)=0
	//2*sin(x)*sin(y)*sin(z)-2*cos(x)*cos(y)*cos(z)=0
	//(cos(x-y)-cos(x+y))*sin(z)-(cos(x-y)+cos(x+y))*cos(z)=0
	//(sin(z)-cos(z))*cos(x-y)-(sin(z)+cos(z))*cos(x+y)=0
	const double a=sin(z)-cos(z);
	const double b=sin(z)+cos(z);
	//a*cos(x-y)-b*cos(x+y)=0
	//u=x-y, v=x+y
	const double minU=0-height;
	const double maxU=width-0;
	const double minV=0+0;
	const double maxV=width+height;
	//a*cos(u)-b*cos(v)=0
	if(std::abs(a)>=std::abs(b)) {//u(v)=acos(b/a*cos(v)) is a continuous line
		for(double uShift=scaled_floor(minU,2*M_PI);uShift<maxU+2*M_PI;uShift+=2*M_PI)
		{
			for(bool forwardRoot:{false,true})
			{
				result.emplace_back();
				for(double v=minV;v<maxV;v+=tolerance) {
					const double u=(forwardRoot?1.:-1.)*acos(b/a*cos(v))+uShift;
					const double x=(u+v)/2;
					const double y=(v-u)/2;
					result.back().points.emplace_back(x*scaleFactor,y*scaleFactor);
				}
			}
		}
	}
	else {//v(u)=acos(a/b*cos(u)) is a continuous line
		for(double vShift=scaled_floor(minV,2*M_PI);vShift<maxV+2*M_PI;vShift+=2*M_PI)
		{
			for(bool forwardRoot:{false,true})
			{
				result.emplace_back();
				for(double u=minU;u<maxU;u+=tolerance) {
					const double v=(forwardRoot?1.:-1.)*acos(a/b*cos(u))+vShift;
					const double x=(u+v)/2;
					const double y=(v-u)/2;
					result.back().points.emplace_back(x*scaleFactor,y*scaleFactor);
				}
			}
		}
	}
	//todo: select the step better
    return result;
}

// FIXME: needed to fix build on Mac on buildserver
constexpr double FillShwartzDiamond::PatternTolerance;

void FillShwartzDiamond::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon, 
    Polylines                       &polylines_out)
{
    auto infill_angle = float(this->angle + (CorrectionAngle * 2*M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    double      density_adjusted = std::max(0., params.density * DensityAdjust);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t     distance = coord_t(scale_(this->spacing) / density_adjusted);

    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2*M_PI*distance, 2*M_PI*distance)));

    // generate pattern
    Polylines polylines = make_waves(
        scale_(this->z),
        density_adjusted,
        this->spacing,
        ceil(bb.size()(0) / distance) + 1.,
        ceil(bb.size()(1) / distance) + 1.);

	// shift the polyline to the grid origin
	for (Polyline &pl : polylines)
		pl.translate(bb.min);

	polylines = intersection_pl(polylines, expolygon);

    if (! polylines.empty()) {
		// Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());
    }

	if (! polylines.empty()) {
		// connect lines
		size_t polylines_out_first_idx = polylines_out.size();
		if (params.dont_connect())
        	append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

	    // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
    }
}

} // namespace Slic3r
