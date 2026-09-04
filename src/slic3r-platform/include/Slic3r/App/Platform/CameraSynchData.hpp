#pragma once

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::App::Platform {

struct CameraSynchData
{
    uint8_t type{0};
    Domain::Vec3d target{Domain::Vec3d::Zero()};
    Domain::Vec3d pivot{Domain::Vec3d::Zero()};
    Eigen::Quaterniond view_rotation{Eigen::Quaterniond::Identity()};
    Eigen::Affine3d model{Eigen::Affine3d::Identity()};
    double distance{0.0};
    double azimuth{0.0};
    double zenith{0.0};
    double zoom{1.0};
};

} // namespace Slic3r::App::Platform
