#pragma once

namespace Slic3r::Domain {
enum Axis {
	X=0,
	Y,
	Z,
	E,
	F,
	NUM_AXES,
	// For the GCodeReader to mark a parsed axis, which is not in "XYZEF", it was parsed correctly.
	UNKNOWN_AXIS = NUM_AXES,
	NUM_AXES_WITH_UNKNOWN,
};
}
