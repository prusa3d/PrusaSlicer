#pragma once

#include "Slic3r/Biz/Algorithms/Projection.hpp"
#include "Slic3r/Biz/Emboss/TextBender.hpp"

namespace Slic3r::Biz::Emboss {

class BendedProjection : public Algorithms::IProjection
{
public:
    BendedProjection(
        double depth,
        double width,
        double height,
        const Domain::Vec3d& center,
        float horizontal_bend,
        float vertical_curl
    );

    std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Vec2crd& p) const override;
    Domain::Vec3d project(const Domain::Vec3d& point) const override;
    std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const override;

private:
    double m_depth;
    double m_width;
    double m_height;
    Domain::Vec3d m_center;
    BendParams m_params;
};

} // namespace Slic3r::Biz::Emboss
