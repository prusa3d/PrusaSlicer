#pragma once

#include "Slic3r/Domain/PixelFormat.hpp"
#include "Types.hpp"
#include "WithInternal.hpp"

#include <array>
#include <string>

namespace Slic3r::App::Render {

class Device;

class Texture : public WithInternal
{
public:
    ~Texture() override;

    void set_data(
        Domain::PixelFormat format,
        int level,
        int w,
        int h,
        const void* data,
        size_t data_size
    );
    void set_sub_data(
        Domain::PixelFormat format,
        int level,
        int offset_x,
        int offset_y,
        int w,
        int h,
        const void* data,
        bool unpack_row_length = false
    );

    /**
     * @brief Allocates one level of all layers of a 2D array texture, filled by the data when given.
     *
     * @param data Layers one after another, or null to leave the content undefined.
     */
    void set_layered_data(
        Domain::PixelFormat format,
        int level,
        int width,
        int height,
        int layer_count,
        const void* data
    );

    /**
     * @brief Fills a rectangle of one layer of a 2D array texture.
     */
    void set_layer_sub_data(
        Domain::PixelFormat format,
        int level,
        int offset_x,
        int offset_y,
        int layer,
        int width,
        int height,
        const void* data
    );

    void set_filtering(TextureMinFilter min_filter, TextureMagFilter mag_filter);

    void set_object_name(const std::string& object_name);
    void set_wrap_s(TextureWrap wrap);
    void set_wrap_t(TextureWrap wrap);
    void set_wrap_r(TextureWrap wrap);
    void set_border_color(const std::array<float, 4>& color);

    int width() const;
    int height() const;

private:
    friend class Device;
    Texture(Device& device, TextureTarget target);

private:
    Device& m_device;

    int m_width  = 0;
    int m_height = 0;
};

} // namespace Slic3r::App::Render
