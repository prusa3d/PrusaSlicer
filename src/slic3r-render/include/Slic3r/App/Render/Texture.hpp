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
    explicit Texture(Device& device);

private:
    Device& m_device;

    int m_width  = 0;
    int m_height = 0;
};

} // namespace Slic3r::App::Render
