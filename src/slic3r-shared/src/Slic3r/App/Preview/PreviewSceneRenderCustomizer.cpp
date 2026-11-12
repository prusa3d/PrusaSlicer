#include "Slic3r/App/Preview/PreviewSceneRenderCustomizer.hpp"
#include "Slic3r/App/Preview/PreviewSceneLayer.hpp"

namespace Slic3r::App::Preview {

void PreviewSceneRenderCustomizer::on_opaque_pass_begin(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx)
{
    PreviewSceneLayer id = PreviewSceneLayer(layer_idx);

    cmd_buf.set_blending_enabled(false);
    cmd_buf.set_depth_write_enabled(true);
    cmd_buf.set_depth_test_enabled(id != PreviewSceneLayer::CogMarker);
    // FIXME: this is currently required because the segments_xxx vertex shaders produce triangles with inconsistent winding
    cmd_buf.set_cull_face_enabled(id != PreviewSceneLayer::Toolpaths);
}

void PreviewSceneRenderCustomizer::on_layer_begin(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx)
{
    PreviewSceneLayer id = PreviewSceneLayer(layer_idx);
    cmd_buf.set_depth_test_enabled(id != PreviewSceneLayer::CogMarker);
    // FIXME: this is currently required because the segments_xxx vertex shaders produce triangles with inconsistent winding
    cmd_buf.set_cull_face_enabled(id != PreviewSceneLayer::Toolpaths);
}

void PreviewSceneRenderCustomizer::on_layer_end(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx)
{
    cmd_buf.set_depth_test_enabled(true);
    cmd_buf.set_cull_face_enabled(true);
}

} // namespace Slic3r::App::Preview
