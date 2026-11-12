#pragma once

#include "Slic3r/App/Platform/AbstractAnimation.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"

namespace Slic3r::App::Scene {

class CameraTargetAnimation : public Platform::AbstractAnimation
{
public:
    CameraTargetAnimation(CameraTrackballController& trackball, const Domain::Vec3d& start_position,
        const Domain::Vec3d& final_position, double duration);

    /**
     * @name Partial implementation of AbstractAnimation protected interface
     * @{
     */
    void on_start() override;
    void on_update(double t) override;
    /**@}*/

private:
    CameraTrackballController& m_trackball;
    Domain::Vec3d m_start_position;
    Domain::Vec3d m_final_position;
};

} // namespace Slic3r::App::Scene

