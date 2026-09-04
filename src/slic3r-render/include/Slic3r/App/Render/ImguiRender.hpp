#pragma once

#include "VertexAttribDesc.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <imgui/imgui.h>

#include <memory>
#include <string>
#include <list>
#include <unordered_map>

struct ImDrawData;

namespace Slic3r::App::Render {

class Device;
class CommandBuffer;
class Geometry;
class Shader;
class ImguiFontHelper;

class ImguiRender
{
public:
    explicit ImguiRender(Device& device);
    ~ImguiRender();

    ImFont* font(Render::ImguiFontType type);
    /**
     * @note do not forget to register texture with use_texture when rendering them
     */
    TexturePtr
    icon_texture(Icon icon, int max_size, const std::unordered_map<std::string, std::string>& replace_strings = {});
    /**
     * @note do not forget to register texture with use_texture when rendering them
     */
    TexturePtr image_texture(const std::string& image, int max_size);

    /**
     * @brief Drop the cached texture for the given image path.
     */
    void invalidate_image_texture(const std::string& image);

    void new_frame();
    void render(CommandBuffer& buffer, const ImDrawData* draw_data);

    /**
     * @brief use_texture - registers TexturePtr which is shared_ptr
     * to ImGuiRender, who will hold this pointer until end of the render cycle.
     * @note all textures rendered by Yoga::Item should be registered here
     */
    void use_texture(TexturePtr texture);

    void set_scale_factor(float scale);

private:
    void init();
    void setup_state(CommandBuffer& buffer, const ImDrawData* draw_data);
    /**
     * ImGui now creates/destroys texture dynamically
     * we need to handle all updates in our backend
     */
    void update_texture(ImTextureData* tex);

private:
    using ImGuiTextureMap = std::unordered_map<ImTextureData*, TexturePtr>;

    Device& m_device;
    VertexAttribsDesc m_vertex_format;
    std::unique_ptr<Geometry> m_geom;
    std::unique_ptr<ImguiFontHelper> m_font_helper;
    Shader* m_shader{nullptr};
    std::list<TexturePtr> m_in_use_textures;
    ImGuiTextureMap m_imgui_dynamic_textures;
    float m_scale_factor{1};
};

} // namespace Slic3r::App::Render
