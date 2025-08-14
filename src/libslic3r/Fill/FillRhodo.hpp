///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FillRhodo_hpp_
#define slic3r_FillRhodo_hpp_

#include <utility>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

class Point;

// Rhombic dodecahedron-inspired infill. Mimics hex/triangle phase transitions along Z.
class FillRhodo : public Fill
{
public:
	Fill* clone() const override { return new FillRhodo(*this); }
	bool use_bridge_flow() const override { return false; }
	bool is_self_crossing() override { return false; }

protected:
	void _fill_surface_single(
		const FillParams                &params,
		unsigned int                     thickness_layers,
		const std::pair<float, Point>   &direction,
		ExPolygon                        expolygon,
		Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillRhodo_hpp_


