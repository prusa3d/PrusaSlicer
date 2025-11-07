///|/ Copyright (c) 2025 Andre Vallestero
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FillRhodo_hpp_
#define slic3r_FillRhodo_hpp_

#include <stddef.h>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

class FillRhodo : public Fill
{
public:
	~FillRhodo() override {}
	bool is_self_crossing() override { return false; }
    bool has_consistent_pattern() const override { return true; }

protected:
	Fill* clone() const override { return new FillRhodo(*this); }
	float _layer_angle(size_t /*idx*/) const override { return 0.f; }
	void _fill_surface_single(
	    const FillParams                &params,
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction,
	    ExPolygon                        expolygon,
	    Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillRhodo_hpp_
