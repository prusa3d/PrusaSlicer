#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/App/Scene/Camera.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace Slic3r::App::Render {
class Framebuffer;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

enum class ShadingType : uint8_t
{
    Legacy,
    Shadows,
    AO,
    PBR
};

static const std::vector<std::string> SHADING_TYPE_NAMES = {
    "Legacy",
    "Shadows",
    "Ambient occlusion",
    "Physically based rendering"
};

class IGraphicsSettingsChangedListener
{
public:
    virtual ~IGraphicsSettingsChangedListener() = default;
    virtual void on_shading_type_changed(ShadingType shading_type) = 0;
};

struct Shadows
{
    bool bed_model_cast_shadow{ false };

    mutable float intensity{DEFAULT_INTENSITY};

    mutable int framebuffer_size{0};
    mutable std::optional<int> pending_framebuffer_size;
    mutable Render::Framebuffer* framebuffer{nullptr};
    mutable Camera light_cam;

    static constexpr int DEFAULT_FRAMEBUFFER_SIZE = 2048;
    static constexpr float DEFAULT_INTENSITY = 0.75f;
};

struct AmbientOcclusion
{
    mutable Domain::Index2 framebuffer_size{0, 0};
    mutable Domain::Index2 tex_fb_size{0, 0};
    mutable Domain::Index2 hblur_fb_size{0, 0};
    mutable Domain::Index2 vblur_fb_size{0, 0};
    mutable Render::Framebuffer* gbuffer_fb{nullptr};
    mutable Render::Framebuffer* ao_tex_fb{nullptr};
    mutable Render::Framebuffer* hblur_fb{nullptr};
    mutable Render::Framebuffer* vblur_fb{nullptr};

    mutable std::optional<size_t> pending_kernel_size;
    mutable std::vector<Domain::Vec3f> kernel;

    mutable size_t noise_size{0};
    mutable std::optional<size_t> pending_noise_size;
    mutable std::shared_ptr<Render::Texture> noise_tex{nullptr};

    mutable float intensity{DEFAULT_INTENSITY};
    mutable float radius{DEFAULT_RADIUS};
    mutable float bias{DEFAULT_BIAS};
    mutable float z_threshold{DEFAULT_Z_THRESHOLD};
    mutable size_t blur_filter_size{DEFAULT_BLUR_FILTER_SIZE};

    static constexpr int EYE_NORM_CLR_ATTR = 0;
    static constexpr int COLOR_CLR_ATTR = 1;

    static constexpr float DEFAULT_INTENSITY = 1.0f;
    static constexpr int DEFAULT_KERNEL_SIZE = 32;
    static constexpr int DEFAULT_NOISE_SIZE = 4;
    static constexpr float DEFAULT_RADIUS = 30.0f;
    static constexpr float DEFAULT_BIAS = 1.5f;
    static constexpr float DEFAULT_Z_THRESHOLD = 5.0f;
    static constexpr size_t DEFAULT_BLUR_FILTER_SIZE = 5;
};

struct PBR
{
    mutable float intensity{DEFAULT_INTENSITY};

    static constexpr float DEFAULT_INTENSITY = 20.0f;
};

class GraphicsSettings : public WithListeners<IGraphicsSettingsChangedListener>
{
public:
    ShadingType shading_type() const { return m_shading_type; }

    bool shadows_enabled() const { return m_shading_type > ShadingType::Legacy; }
    bool ao_enabled() const { return m_shading_type > ShadingType::Shadows; }
    bool pbr_enabled() const { return m_shading_type > ShadingType::AO; }

    bool bed_model_cast_shadow() const { return m_shadows.bed_model_cast_shadow; }
    int shadowsmap_size() const { return m_shadows.framebuffer_size; }
    float shadows_intensity() const { return m_shadows.intensity; }

    Domain::Index2 ao_framebuffer_size() const { return m_ao.tex_fb_size; }
    float ao_intensity() const { return m_ao.intensity; }
    size_t ao_kernel_size() const { return m_ao.kernel.size(); }
    size_t ao_noise_size() const { return m_ao.noise_size; }
    float ao_radius() const { return m_ao.radius; }
    float ao_bias() const { return m_ao.bias; }
    float ao_z_threshold() const { return m_ao.z_threshold; }
    size_t ao_blur_filter_size() const { return m_ao.blur_filter_size; }

    float pbr_intensity() const { return m_pbr.intensity; }

private:
    //
    // setters are accessible only through Scene class
    //
    void set_shading_type(ShadingType shading_type) {
        if (m_shading_type != shading_type) {
            m_shading_type = shading_type;
            invoke_listeners<IGraphicsSettingsChangedListener>([this](auto* l) { l->on_shading_type_changed(m_shading_type); });
        }
    }

    void set_default_shadows_intensity() { m_shadows.intensity = Shadows::DEFAULT_INTENSITY; }
    void set_bed_model_cast_shadow(bool cast) { m_shadows.bed_model_cast_shadow = cast; }
    void set_shadowsmap_size(int size) { m_shadows.pending_framebuffer_size = size; }
    void set_shadows_intensity(float intensity) { m_shadows.intensity = intensity; }

    void set_ao_intensity(float intensity) { m_ao.intensity = intensity; }
    void set_ao_kernel_size(size_t size) { m_ao.pending_kernel_size = size; }
    void set_ao_noise_size(size_t size) { m_ao.pending_noise_size = size; }
    void set_ao_radius(float radius) { m_ao.radius = radius; }
    void set_ao_bias(float bias) { m_ao.bias = bias; }
    void set_ao_z_threshold(float z_threshold) { m_ao.z_threshold = z_threshold; }
    void set_ao_blur_filter_size(size_t size) { m_ao.blur_filter_size = size; }

    void set_default_ao_intensity() { m_ao.intensity = AmbientOcclusion::DEFAULT_INTENSITY; }
    void set_default_ao_kernel_size() { m_ao.pending_kernel_size = AmbientOcclusion::DEFAULT_KERNEL_SIZE; }
    void set_default_ao_noise_size() { m_ao.pending_noise_size = AmbientOcclusion::DEFAULT_NOISE_SIZE; }
    void set_default_ao_radius() { m_ao.radius = AmbientOcclusion::DEFAULT_RADIUS; }
    void set_default_ao_bias() { m_ao.bias = AmbientOcclusion::DEFAULT_BIAS; }
    void set_default_ao_z_threshold() { m_ao.z_threshold = AmbientOcclusion::DEFAULT_Z_THRESHOLD; }
    void set_default_ao_blur_filter_size() { m_ao.blur_filter_size = AmbientOcclusion::DEFAULT_BLUR_FILTER_SIZE; }

    void set_pbr_intensity(float intensity) { m_pbr.intensity = intensity; }
    void set_default_pbr_intensity() { m_pbr.intensity = PBR::DEFAULT_INTENSITY; }

private:
    ShadingType m_shading_type{ ShadingType::PBR };
    Shadows m_shadows;
    AmbientOcclusion m_ao;
    PBR m_pbr;

    friend class Scene;
};

} // namespace Slic3r::App::Scene
