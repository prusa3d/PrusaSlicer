#include "Slic3r/App/Render/LinearDataTexture.hpp"

#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Texture.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Math.hpp"

#include <algorithm>
#include <limits>

using Slic3r::Domain::PixelFormat;

namespace Slic3r::App::Render {

namespace {

constexpr size_t MAX_LINEAR_DATA_TEXTURE_WIDTH = 4096;

} // namespace

LinearDataTexture::LinearDataTexture(
    Device& device,
    const PixelFormat pixel_format,
    const std::string& object_name
) :
    m_pixel_format(pixel_format),
    m_width(std::min(MAX_LINEAR_DATA_TEXTURE_WIDTH, device.context().max_texture_size())),
    m_max_layer_height(device.context().max_texture_size()),
    // Every texel index fits a GLSL int.
    m_max_layer_count(
        std::min(
            device.context().max_array_texture_layers(),
            static_cast<size_t>(std::numeric_limits<int>::max()) / (m_width * m_max_layer_height)
        )
    ),
    m_texture(device.create_texture(TextureTarget::Texture2DArray))
{
    ASSERT(m_max_layer_count > 0);
    m_texture->set_object_name(object_name);
    // Integer textures are incomplete with a filter other than nearest.
    m_texture->set_filtering(TextureMinFilter::Nearest, TextureMagFilter::Nearest);
    m_texture->set_wrap_s(TextureWrap::ClampToEdge);
    m_texture->set_wrap_t(TextureWrap::ClampToEdge);
}

bool LinearDataTexture::reserve_texels(const size_t texel_count)
{
    ASSERT(texel_count <= m_width * m_max_layer_height * m_max_layer_count);
    if (texel_count <= this->texel_capacity()) {
        return false;
    }

    const size_t needed_row_count = ceil_div(texel_count, m_width);
    const size_t reserved_row_count =
        std::min(needed_row_count + needed_row_count / 2, m_max_layer_height * m_max_layer_count);

    // The rows are spread evenly over the fewest layers, at most one row per layer is wasted.
    m_layer_count  = ceil_div(reserved_row_count, m_max_layer_height);
    m_layer_height = ceil_div(reserved_row_count, m_layer_count);
    m_texture->set_layered_data(
        m_pixel_format,
        0,
        static_cast<int>(m_width),
        static_cast<int>(m_layer_height),
        static_cast<int>(m_layer_count),
        nullptr
    );

    return true;
}

void LinearDataTexture::
    upload_texels(const size_t first_texel, const size_t texel_count, const void* texel_data)
{
    ASSERT(first_texel + texel_count <= this->texel_capacity());

    const size_t bytes_per_texel = Domain::pixel_format_bytes_per_pixel(m_pixel_format);
    const std::byte* data        = static_cast<const std::byte*>(texel_data);
    const size_t end_texel       = first_texel + texel_count;
    size_t texel_idx             = first_texel;
    while (texel_idx < end_texel) {
        const size_t column         = texel_idx % m_width;
        const size_t row_in_texture = texel_idx / m_width;
        const size_t layer          = row_in_texture / m_layer_height;
        const size_t row            = row_in_texture % m_layer_height;

        // A part of a row, or whole rows up to the end of the layer.
        const bool uploads_whole_rows   = column == 0 && end_texel - texel_idx >= m_width;
        const size_t uploaded_row_count = uploads_whole_rows ?
            std::min((end_texel - texel_idx) / m_width, m_layer_height - row) :
            1;
        const size_t uploaded_row_width =
            uploads_whole_rows ? m_width : std::min(m_width - column, end_texel - texel_idx);

        m_texture->set_layer_sub_data(
            m_pixel_format,
            0,
            static_cast<int>(column),
            static_cast<int>(row),
            static_cast<int>(layer),
            static_cast<int>(uploaded_row_width),
            static_cast<int>(uploaded_row_count),
            data
        );

        const size_t uploaded_texel_count = uploaded_row_width * uploaded_row_count;
        texel_idx += uploaded_texel_count;
        data += uploaded_texel_count * bytes_per_texel;
    }
}

size_t LinearDataTexture::texel_capacity() const
{
    return m_width * m_layer_height * m_layer_count;
}

const TexturePtr& LinearDataTexture::texture() const
{
    return m_texture;
}

} // namespace Slic3r::App::Render
