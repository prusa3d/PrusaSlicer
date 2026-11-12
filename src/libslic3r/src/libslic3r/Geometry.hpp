// Temporary proxy header.
#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Domain/Transformation.hpp"

namespace Slic3r::Geometry {
using Slic3r::Biz::Algorithms::Geometry::Orientation;

using Slic3r::Biz::Algorithms::Geometry::Orientation::ORIENTATION_CCW;
using Slic3r::Biz::Algorithms::Geometry::Orientation::ORIENTATION_CW;
using Slic3r::Biz::Algorithms::Geometry::Orientation::ORIENTATION_COLINEAR;

using Slic3r::Biz::Algorithms::Geometry::orient;
using Slic3r::Biz::Algorithms::Geometry::is_ccw;
using Slic3r::Biz::Algorithms::Geometry::ray_ray_intersection;
using Slic3r::Biz::Algorithms::Geometry::segment_segment_intersection;
using Slic3r::Biz::Algorithms::Geometry::segments_intersect;
using Slic3r::Biz::Algorithms::Geometry::foot_pt;
using Slic3r::Biz::Algorithms::Geometry::ray_point_distance_squared;
using Slic3r::Biz::Algorithms::Geometry::ray_point_distance;
using Slic3r::Biz::Algorithms::Geometry::ray_point_distance;
using Slic3r::Biz::Algorithms::Geometry::liang_barsky_line_clipping_interval;
using Slic3r::Biz::Algorithms::Geometry::liang_barsky_line_clipping;
using Slic3r::Biz::Algorithms::Geometry::directions_parallel;
using Slic3r::Biz::Algorithms::Geometry::directions_perpendicular;
using Slic3r::Biz::Algorithms::Geometry::contains;
using Slic3r::angle_to_0_2PI;
using Slic3r::Biz::Algorithms::Geometry::to_range_pi_pi;
using Slic3r::Biz::Algorithms::Geometry::simplify_polygons;
using Slic3r::Biz::Algorithms::Geometry::linint;
using Slic3r::Biz::Algorithms::Geometry::arrange;
using Slic3r::Biz::Algorithms::Geometry::assemble_transform;
using Slic3r::Domain::translation_transform;
using Slic3r::Domain::rotation_transform;
using Slic3r::Domain::scale_transform;
using Slic3r::Domain::extract_rotation;
using Slic3r::Domain::Transformation;
using Slic3r::Domain::TransformationSVD;
using Slic3r::Biz::Algorithms::Geometry::transform3d_from_string;
using Slic3r::Biz::Algorithms::Geometry::rotation_xyz_diff;
using Slic3r::Biz::Algorithms::Geometry::rotation_diff_z;
using Slic3r::Biz::Algorithms::Geometry::is_rotation_ninety_degrees;
using Slic3r::Biz::Algorithms::Geometry::trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only;
using Slic3r::Biz::Algorithms::Geometry::dir_to_spheric;
using Slic3r::Biz::Algorithms::Geometry::spheric_to_dir;
using Slic3r::Biz::Algorithms::Geometry::is_point_inside_polygon_corner;
} // namespace Slic3r::Geometry

#endif
