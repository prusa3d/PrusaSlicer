#pragma once

#include <cmath>

namespace Slic3r::App::Scene {

struct CameraProjectionParameters
{
    static double orthographic_zoom_from_perspective(double perspective_zoom)
    {
        return 1 / (REF_Z * std::tan((REF_FOVY * M_PI) / (2 * 180 * perspective_zoom)));
    }

    static double perspective_zoom_from_orthographic(double ortho_zoom)
    {
        return (REF_FOVY * M_PI) / (180 * 2 * std::atan(1 / (REF_Z * ortho_zoom)));
    }

    static constexpr double REF_FOVY = 30.0;

    // static constexpr double Z_NEAR = 10;
    // static constexpr double Z_FAR = 1000.0;
    static constexpr double REF_Z = 800;

    static constexpr double PERSPECTIVE_MIN_ZOOM = 0.3;
    static constexpr double PERSPECTIVE_MAX_ZOOM = 200;

    static double orthographic_min_zoom()
    {
        return orthographic_zoom_from_perspective(PERSPECTIVE_MIN_ZOOM);
    }

    static double orthographic_max_zoom()
    {
        return orthographic_zoom_from_perspective(PERSPECTIVE_MAX_ZOOM);
    }
};


}
