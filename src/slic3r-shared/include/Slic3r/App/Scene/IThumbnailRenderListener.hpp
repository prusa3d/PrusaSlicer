#pragma once

namespace Slic3r::App::Scene {

/**
 * @brief Listener interface for thumbnail rendering lifecycle events.
 *
 * Classes implementing this interface can register with Scene to receive
 * notifications when thumbnail rendering begins and ends. This allows
 * gizmos and other components to temporarily adjust scene state during
 * thumbnail capture (e.g., hiding gizmo-specific nodes, restoring original
 * volume visibility).
 */
class IThumbnailRenderListener
{
public:
    virtual ~IThumbnailRenderListener() = default;

    /**
     * @brief Called when thumbnail rendering is about to begin.
     *
     * Implementers should prepare the scene for thumbnail capture,
     * such as hiding temporary visualization nodes.
     */
    virtual void on_thumbnail_render_begin() = 0;

    /**
     * @brief Called when thumbnail rendering has finished.
     *
     * Implementers should restore any scene state that was modified
     * in on_thumbnail_render_begin().
     */
    virtual void on_thumbnail_render_end() = 0;
};

} // namespace Slic3r::App::Scene
