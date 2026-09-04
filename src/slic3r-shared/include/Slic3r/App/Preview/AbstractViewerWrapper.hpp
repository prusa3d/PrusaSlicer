#pragma once

#include "DoubleSliderForLayers.hpp"

#include <Slic3r/App/Scene/Lights.hpp>

#include <functional>
#include <array>

namespace Slic3r::App::Render {
class ImguiRender;
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class Scene;
class GeometryDataFactory;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {
class AbstractViewer;
} // namespace Slic3r::App::libvgcode

namespace Slic3r::App::Preview {

struct LayersSliderBaseFlags
{
    bool show_ruler{false};
    bool show_ruler_bg{false};
    bool show_estimated_times{false};
};

struct LayersSliderBaseCallbacks
{
    std::function<void()> on_thumb_move{nullptr};
    std::function<void(unsigned)> request_extra_frames{nullptr};
    AppConfigChangedCallback app_config_changed{nullptr};
};

struct ViewerWrapperBaseSettings
{
    LayersSliderBaseFlags layers_slider_base_flags;
    LayersSliderBaseCallbacks layers_slider_base_callbacks;
};

struct WrapperLayoutData
{
    float menubar_height{ 0.0f };
    std::array<float, 2> view_toolbar_size{ 0.0f, 0.0f };
    float collapse_toolbar_height{ 0.0f };
    float scale_factor{ 1.0f };
};

struct LegendParams
{
    bool visible{ true };
    bool enabled{ true };
    bool settings_visible{ false };
    bool is_shown() const { return enabled && visible; }
};

class AbstractViewerWrapper
{
public:
    AbstractViewerWrapper() = default;
    AbstractViewerWrapper(AbstractViewerWrapper&&) = delete;
    AbstractViewerWrapper(const AbstractViewerWrapper&) = delete;
    AbstractViewerWrapper& operator=(AbstractViewerWrapper&&) = delete;
    AbstractViewerWrapper& operator=(const AbstractViewerWrapper&) = delete;
    virtual ~AbstractViewerWrapper() = default;

    virtual bool init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory) = 0;
    virtual const libvgcode::AbstractViewer& viewer() const = 0;
    virtual libvgcode::AbstractViewer& viewer() = 0;

    virtual void set_scene(Scene::Scene& scene) = 0;
    virtual void clear_scene()                  = 0;

    virtual void render_scene() = 0;
    virtual void render_imgui() = 0;

    virtual bool has_data() const;
    virtual void reset() = 0;

    std::unique_ptr<DoubleSliderForLayers> unload_double_slider_layers();

    void slider_layers_move_current_thumb(int delta) { m_slider_layers->move_current_thumb(delta); }
    void slider_layers_jump_to_value() { m_slider_layers->jump_to_value(); }

    void set_layers_slider_base_flags(LayersSliderBaseFlags flags);

protected:

    Yoga::Passthrough<DoubleSliderForLayers>   m_slider_layers;
    LegendParams            m_legend_params;
};

} // namespace Slic3r::App::Preview
