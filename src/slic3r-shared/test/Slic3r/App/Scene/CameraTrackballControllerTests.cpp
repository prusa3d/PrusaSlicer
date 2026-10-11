#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::App::Scene;

using Slic3r::Domain::Vec3d;

using Catch::Matchers::WithinAbs;

TEST_CASE("Free rotation keeps the camera looking at the target", "[CameraTrackballController]")
{
    Camera camera;
    CameraTrackballController trackball{camera};
    trackball.set_target(Domain::Vec3d(10.0, 20.0, 5.0));
    trackball.synchronize_pivot_with_target();
    const Vec3d target    = trackball.target();
    const double distance = (target - camera.position()).norm();

    trackball.rotate_around_pivot(Domain::Vec3d(0.3, -0.2, 0.4));

    REQUIRE((trackball.target() - target).norm() < 1e-9);
    const Vec3d to_target = trackball.target() - camera.position();
    REQUIRE_THAT(to_target.norm(), WithinAbs(distance, 1e-6));
    REQUIRE((to_target.normalized() - camera.forward()).norm() < 1e-9);
    REQUIRE(std::abs(camera.right().z()) > 1e-3);

    const double azimuth = trackball.azimuth();
    const double zenith  = trackball.zenith();
    const Vec3d orbit_forward(
        std::sin(zenith) * std::cos(azimuth),
        std::sin(zenith) * std::sin(azimuth),
        std::cos(zenith)
    );
    REQUIRE((orbit_forward - camera.forward()).norm() < 1e-9);
}

TEST_CASE(
    "Free rotation in a level view matches the constrained orbit around the pivot",
    "[CameraTrackballController]"
)
{
    Camera free_camera;
    CameraTrackballController free_trackball{free_camera};
    Camera constrained_camera;
    CameraTrackballController constrained_trackball{constrained_camera};
    for (CameraTrackballController* trackball : {&free_trackball, &constrained_trackball}) {
        trackball->set_azimuth_and_zenith(0.3, M_PI_2);
        trackball->set_pivot(Vec3d(100.0, -50.0, 20.0));
    }

    free_trackball.rotate_around_pivot(Vec3d(0.0, 0.4, 0.0));
    constrained_trackball.add_azimuth_and_zenith(0.4, 0.0);

    REQUIRE((free_camera.model().matrix() - constrained_camera.model().matrix()).norm() < 1e-9);
    REQUIRE((free_trackball.target() - constrained_trackball.target()).norm() < 1e-9);
}

TEST_CASE(
    "Recovery from the free rotation levels the camera in place",
    "[CameraTrackballController]"
)
{
    Camera camera;
    CameraTrackballController trackball{camera};
    trackball.rotate_around_pivot(Vec3d(0.3, -0.2, 0.4));
    const Vec3d position = camera.position();

    trackball.recover_from_free_rotation();

    REQUIRE_THAT(camera.right().z(), WithinAbs(0.0, 1e-9));
    REQUIRE((camera.position() - position).norm() < 1e-6);
}
