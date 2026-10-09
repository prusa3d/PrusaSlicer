#pragma once

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

/**
 * SLA support points and drain holes are stored with the volume they sit on, in its mesh
 * coordinates. These helpers convert them from and to model object coordinates, where the SLA
 * backend and the gizmos work with them.
 */
namespace Slic3r::Biz::Algorithms::SlaPoints {

/**
 * @brief Whether the volume is a part of the SLA geometry (a model part or a negative volume). The
 * points and holes of other volumes are kept, but not used.
 */
bool is_sla_geometry(const Domain::ModelVolume& volume);

/**
 * @brief Support points of the SLA geometry volumes in model object coordinates.
 */
Domain::SLA::SupportPoints support_points_in_object(const Domain::ModelObject& object);

/**
 * @brief Drain holes of the SLA geometry volumes in model object coordinates.
 */
Domain::SLA::DrainHoles drain_holes_in_object(const Domain::ModelObject& object);

/**
 * @brief Replaces the support points of the SLA geometry volumes with points given in model object
 * coordinates. Each point goes to the volume with the nearest surface. Dropped if the object has
 * no SLA geometry volume.
 */
void set_support_points_in_object(Domain::ModelObject& object, const Domain::SLA::SupportPoints& points);

/**
 * @brief Replaces the drain holes of the SLA geometry volumes with holes given in model object
 * coordinates. Each hole goes to the volume with the nearest surface. Dropped if the object has no
 * SLA geometry volume.
 */
void set_drain_holes_in_object(Domain::ModelObject& object, const Domain::SLA::DrainHoles& holes);

/**
 * @brief Whether any SLA geometry volume of the object has support points.
 */
bool has_support_points(const Domain::ModelObject& object);

/**
 * @brief Whether any SLA geometry volume of the object has drain holes.
 */
bool has_drain_holes(const Domain::ModelObject& object);

/**
 * @brief Removes the support points of all volumes.
 */
void clear_support_points(Domain::ModelObject& object);

/**
 * @brief Removes the drain holes of all volumes.
 */
void clear_drain_holes(Domain::ModelObject& object);

} // namespace Slic3r::Biz::Algorithms::SlaPoints
