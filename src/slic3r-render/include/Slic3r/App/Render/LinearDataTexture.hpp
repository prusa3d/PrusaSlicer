#pragma once

#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/Domain/PixelFormat.hpp"

#include <string>

namespace Slic3r::App::Render {

class Device;

/**
 * @brief Growable linear array of texels in a 2D array texture read by texelFetch, texel i lies in
 * layer (i / W) / H at (i % W, (i / W) % H), where (W, H, layers) is textureSize(sampler, 0).
 */
class LinearDataTexture
{
public:
    /**
     * @brief Creates an empty array, the memory is allocated by reserve_texels().
     *
     * @param object_name Name of the texture shown in graphics debuggers.
     */
    LinearDataTexture(
        Device& device,
        Domain::PixelFormat pixel_format,
        const std::string& object_name
    );

    /**
     * @brief Grows the capacity to at least texel_count with a reserve, growing loses the content.
     *
     * @return True when the texture was reallocated.
     */
    bool reserve_texels(size_t texel_count);

    /**
     * @brief Uploads a range of texels, which must fit in the capacity from reserve_texels().
     *
     * @param texel_data Data of the range, starting with the texel first_texel.
     */
    void upload_texels(size_t first_texel, size_t texel_count, const void* texel_data);

    /**
     * @brief Texture to bind to the sampler of the shader.
     */
    const TexturePtr& texture() const;

private:
    size_t texel_capacity() const;

    Domain::PixelFormat m_pixel_format;
    size_t m_width;
    size_t m_max_layer_height;
    size_t m_max_layer_count;
    size_t m_layer_height = 0;
    size_t m_layer_count  = 0;
    TexturePtr m_texture;
};

} // namespace Slic3r::App::Render
