#include "Slic3r/App/Render/ImguiRender.hpp"

#include "Slic3r/App/Render/CommandBuffer.hpp"
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"
#include "Slic3r/App/Render/ImguiFontHelper.hpp"
#include "Slic3r/App/Render/ImguiIconHelper.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Slic3r/Assert.hpp>

using Slic3r::Domain::SquareMatrix4f;

namespace Slic3r::App::Render {

namespace {
template <typename T>
T physical_pixel(T logical_pixel, float scale)
{
    return std::round(logical_pixel * scale);
}
}

ImguiRender::ImguiRender(Device& device) :
    m_device(device),
    m_vertex_format(
        {{VertexAttribType::Vertex, DataType::Float, 2, offsetof(ImDrawVert, pos)},
         {VertexAttribType::TexCoord0, DataType::Float, 2, offsetof(ImDrawVert, uv)},
         {VertexAttribType::Color, DataType::UByte, 4, offsetof(ImDrawVert, col), true, true}}
    ),
    m_font_helper(std::make_unique<ImguiFontHelper>())
{}

ImguiRender::~ImguiRender() = default;

ImFont* ImguiRender::font(Render::ImguiFontType type)
{
    return m_font_helper->font(type);
}

TexturePtr ImguiRender::icon_texture(
    Icon icon,
    int max_size,
    const std::unordered_map<std::string, std::string>& replace_strings
)
{
    return m_device.context().texture_manager().get_or_create_image(
        ImguiIconHelper::icon_path(icon),
        {physical_pixel(max_size, m_scale_factor), false, true, false, false, replace_strings}
    );
}

TexturePtr ImguiRender::image_texture(const std::string& image, int max_size)
{
    return m_device.context().texture_manager().get_or_create_image(
        image,
        {physical_pixel(max_size, m_scale_factor), false, true}
    );
}

void ImguiRender::invalidate_image_texture(const std::string& image)
{
    m_device.context().texture_manager().invalidate_image(image);
}

void ImguiRender::new_frame()
{
    if (m_shader == nullptr)
        init();
}

void ImguiRender::use_texture(TexturePtr texture)
{
    m_in_use_textures.push_back(texture);
}

void ImguiRender::set_scale_factor(float scale)
{
    m_scale_factor = scale;
}

void ImguiRender::init()
{
    m_geom   = std::make_unique<Geometry>(m_device, BufferUsage::StreamDraw);
    m_shader = m_device.context().shader_manager().shader("imgui");
    ASSERT(m_shader != nullptr, "Cannot load imgui shader");
}

void ImguiRender::setup_state(CommandBuffer& buffer, const ImDrawData* draw_data)
{
    buffer.set_blending(
        {{Render::BlendFactor::SrcAlpha, Render::BlendFactor::OneMinusSrcAlpha},
         {Render::BlendFactor::One, Render::BlendFactor::OneMinusSrcAlpha}}
    );
    buffer.set_blending_enabled(true);
    buffer.set_scissor_enabled(true);
    buffer.set_stencil_test_enabled(false);
    buffer.set_cull_face_enabled(false);
    buffer.set_depth_test_enabled(false);

    buffer.bind_shader(*m_shader);

    const int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height =
        static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    buffer.set_viewport({0, 0, fb_width, fb_height});
    const float left   = draw_data->DisplayPos.x;
    const float right  = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float top    = draw_data->DisplayPos.y;
    const float bottom = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    SquareMatrix4f projection = ortho(left, right, bottom, top, -1, 1).cast<float>();
    m_shader->set_uniform("ProjMtx", projection);
}

void ImguiRender::update_texture(ImTextureData* tex)
{
    switch (tex->Status) {
    case ImTextureStatus_OK:
        break;
    case ImTextureStatus_Destroyed:
        // Assert?
        break;
    case ImTextureStatus_WantCreate: {
        ASSERT(
            !m_imgui_dynamic_textures.contains(tex),
            "ImGui wants to create a texture, but that texture is already created."
        );

        const std::string name = "imgui_" + std::to_string(tex->UniqueID);

        TexturePtr created_texture =
            m_imgui_dynamic_textures
                .emplace(
                    tex,
                    m_device.context().texture_manager().get_or_create_dynamic(
                        name,
                        Domain::PixelFormat::RGBA8,
                        tex->Width,
                        tex->Height
                    )
                )
                .first->second;

        created_texture->set_data(
            Domain::PixelFormat::RGBA8,
            0,
            tex->Width,
            tex->Height,
            tex->GetPixels(),
            tex->BytesPerPixel * tex->Width * tex->Height
        );

        created_texture->set_filtering(TextureMinFilter::Linear, TextureMagFilter::Linear);
        created_texture->set_wrap_s(TextureWrap::ClampToEdge);
        created_texture->set_wrap_t(TextureWrap::ClampToEdge);

        // Crucial: make ImGui draw commands reference our Texture*
        tex->SetTexID((ImTextureID) created_texture.get());

        // Mark as handled (depending on API: either you set Status, or ImGui sets it when SetTexID is called)
        tex->Status = ImTextureStatus_OK;
        break;
    }
    case ImTextureStatus_WantUpdates: {
        if (m_imgui_dynamic_textures.contains(tex)) {
            TexturePtr texture = m_imgui_dynamic_textures.at(tex);

            for (ImTextureRect& update_rect : tex->Updates) {
                texture->set_sub_data(
                    Domain::PixelFormat::RGBA8,
                    0,
                    update_rect.x,
                    update_rect.y,
                    update_rect.w,
                    update_rect.h,
                    tex->GetPixelsAt(update_rect.x, update_rect.y),
                    true
                );
            }

            tex->Status = ImTextureStatus_OK;
        } else {
            // treat as create, but log warning
            SPDLOG_WARN(
                "Imgui texture arrived with ImTextureStatus_WantUpdates, but was not created before, creating..."
            );
            tex->Status = ImTextureStatus_WantCreate;
            update_texture(tex);
        }
        break;
    }
    case ImTextureStatus_WantDestroy: {
        ASSERT(
            m_imgui_dynamic_textures.contains(tex),
            "ImGui wants to destroy non-existing texture"
        );
        m_imgui_dynamic_textures.erase(tex);

        tex->SetTexID(0); // nullptr
        tex->Status = ImTextureStatus_Destroyed;
        break;
    }
    }
}

void ImguiRender::render(CommandBuffer& buffer, const ImDrawData* draw_data)
{
    if (draw_data->DisplaySize.x <= 0 || draw_data->DisplaySize.y <= 0)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures) {
        for (ImTextureData* tex : *draw_data->Textures) {
            update_texture(tex);
        }
    }

    const int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height =
        static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);

    // Will project scissor/clipping rectangles into framebuffer space
    const ImVec2 clip_off = draw_data->DisplayPos; // (0,0) unless using multi-viewports
    const ImVec2 clip_scale =
        draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    setup_state(buffer, draw_data);

    Texture* last_bound_texture = nullptr;

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        // Upload vertex/index buffers
        m_geom->upload(
            cmd_list->VtxBuffer.Data,
            cmd_list->VtxBuffer.Size,
            m_vertex_format,
            cmd_list->IdxBuffer.Data,
            cmd_list->IdxBuffer.Size,
            IndexTypeTraits<ImDrawIdx>::index_type
        );

        // SPDLOG_DEBUG("Uploading {} vertices, {} indices", cmd_list->VtxBuffer.Size, cmd_list->IdxBuffer.Size);

        buffer.bind_geometry(*m_geom, *m_shader);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    setup_state(buffer, draw_data);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect;
                clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width
                    && clip_rect.y < fb_height
                    && clip_rect.z >= 0.0f
                    && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    // glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));
                    buffer.set_scissor(
                        {(int) clip_rect.x,
                         (int) (fb_height - clip_rect.w),
                         (int) (clip_rect.z - clip_rect.x),
                         (int) (clip_rect.w - clip_rect.y)}
                    );

                    // Bind texture, Draw
                    auto* texture = (Texture*) pcmd->GetTexID();
                    if (texture) {
                        buffer.bind_texture(0, *texture);
                        last_bound_texture = texture;
                    }
                    buffer.draw(PrimitiveType::Triangles, pcmd->IdxOffset, pcmd->ElemCount);
                    // glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
                    // glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)));
                }
            }
        }
    }

    if (last_bound_texture) {
        buffer.unbind_texture(0, *last_bound_texture);
    }
    buffer.set_blending_enabled(false);
    buffer.set_scissor_enabled(false);
    buffer.set_scissor({0, 0, fb_width, fb_height});

    m_in_use_textures.clear();
}

} // namespace Slic3r::App::Render
