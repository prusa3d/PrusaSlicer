///|/ Copyright (c) 2026 Sam Biggins
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CLIThumbnailRenderer_hpp_
#define slic3r_CLIThumbnailRenderer_hpp_

#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r { namespace CLI {

/// Headless thumbnail renderer for CLI mode using EGL offscreen context.
///
/// Creates an EGL pbuffer surface (no display server required), renders the
/// model mesh with the same Gouraud lighting as the GUI, and returns RGBA
/// pixel data in ThumbnailData format.
///
/// Falls back gracefully: if EGL initialization fails (no GPU, no driver),
/// render() returns an empty ThumbnailData and the caller should skip
/// thumbnail embedding silently.
class ThumbnailRenderer
{
public:
    /// Try to create an EGL offscreen context.
    /// Returns true on success, false if no rendering backend is available.
    static bool init();

    /// Release EGL resources.
    static void shutdown();

    /// Render a model to a thumbnail.
    /// @param model   The loaded model (uses all mesh geometry).
    /// @param width   Output width in pixels.
    /// @param height  Output height in pixels.
    /// @param color   RGBA color for the mesh (default: PrusaSlicer orange).
    static ThumbnailData render(
        const Model& model,
        unsigned int  width,
        unsigned int  height,
        float color_r = 1.0f,
        float color_g = 0.5f,
        float color_b = 0.0f,
        float color_a = 1.0f
    );

    static bool is_initialized() { return s_initialized; }

private:
    static bool s_initialized;
};

}} // namespace Slic3r::CLI

#endif // slic3r_CLIThumbnailRenderer_hpp_
