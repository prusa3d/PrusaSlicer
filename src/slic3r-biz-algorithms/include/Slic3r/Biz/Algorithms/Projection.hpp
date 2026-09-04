#pragma once

#include <optional>
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Algorithms {
/// <summary>
/// Project spatial point
/// </summary>
class IProject3d
{
public:
    virtual ~IProject3d() = default;
    /// <summary>
    /// Move point with respect to projection direction
    /// e.g. Orthogonal projection will move with point by direction
    /// e.g. Spherical projection need to use center of projection
    /// </summary>
    /// <param name="point">Spatial point coordinate</param>
    /// <returns>Projected spatial point</returns>
    virtual Domain::Vec3d project(const Domain::Vec3d& point) const = 0;
};

/// <summary>
/// Project 2d point into space
/// Could be plane, sphere, cylindric, ...
/// </summary>
class IProjection: public IProject3d
{
public:
    /// <summary>
    /// convert 2d point to 3d points
    /// </summary>
    /// <param name="p">2d coordinate</param>
    /// <returns>
    /// first - front spatial point
    /// second - back spatial point
    /// </returns>
    virtual std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Vec2crd& p) const = 0;

    /// <summary>
    /// Back projection
    /// </summary>
    /// <param name="p">Point to project</param>
    /// <param name="depth">[optional] Depth of 2d projected point. Be careful number is in 2d scale</param>
    /// <returns>Uprojected point when it is possible</returns>
    virtual std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const = 0;
};

} // namespace Slic3r::Biz::CGAL::Algorithms
