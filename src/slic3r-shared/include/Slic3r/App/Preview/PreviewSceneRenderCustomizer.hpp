#pragma once

#include "Slic3r/App/Scene/Scene.hpp"

namespace Slic3r::App::Preview {

class PreviewSceneRenderCustomizer : public Scene::MinimalSceneRenderCustomizer
{
    /**
     * @name Implementation of Platform::AbstractRenderModule public interface
     * @{
     */
    void on_opaque_pass_begin(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx) override;
    void on_layer_begin(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx) override;
    void on_layer_end(Render::CommandBuffer& cmd_buf, Scene::RenderLayerId layer_idx) override;
    /**@}*/
};

} // namespace Slic3r::App::Preview
