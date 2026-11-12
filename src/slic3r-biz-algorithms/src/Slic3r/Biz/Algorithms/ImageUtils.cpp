#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include <cstring>
#include <algorithm>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#if ENABLE_DEBUG_EXPORT_TO_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <filesystem>
#endif // ENABLE_DEBUG_EXPORT_TO_PNG

namespace Slic3r::Biz::Algorithms::ImageUtils {

using Domain::Image;
using Domain::Images;
using Domain::PixelFormat;
using Domain::Size;
using Domain::Sizes;

static void extractBlock(
    const unsigned char* src,
    int x,
    int y,
    int w,
    int h,
    int src_channels,
    unsigned char* block)
{
    const int bw = std::min(w - x, 4);
    const int bh = std::min(h - y, 4);

    const int rem[] =
        {
            0, 0, 0, 0, //
            0, 1, 0, 1, //
            0, 1, 2, 0, //
            0, 1, 2, 3 //
        };

    for (int i = 0; i < 4; ++i) {
        const int by = rem[(bh - 1) * 4 + i] + y;

        for (int j = 0; j < 4; ++j) {
            const int bx = rem[(bw - 1) * 4 + j] + x;

            const unsigned char* s = src + (by * w + bx) * src_channels;
            unsigned char* d = block + (i * 4 + j) * 4;

            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = (src_channels == 4) ? s[3] : 127;
        }
    }
}

static void rygCompress(
    unsigned char* dst,
    unsigned char* src,
    int w,
    int h,
    int src_channels,
    int isDxt5,
    int& compressed_size
)
{
    unsigned char block[64];
    int x, y;

    unsigned char* initial_dst = dst;

    for (y = 0; y < h; y += 4) {
        for (x = 0; x < w; x += 4) {
            extractBlock(src, x, y, w, h, src_channels, block);
            stb_compress_dxt_block(dst, block, isDxt5, STB_DXT_NORMAL);
            dst += isDxt5 ? 16 : 8;
        }
    }

    compressed_size = dst - initial_dst;
}

size_t pixel_format_channel_count(PixelFormat pf)
{
    // as there are only single byte channel values we can use just p
    return pixel_format_bytes_per_pixel(pf);
}

std::size_t channel_count(const Image& image)
{
    return pixel_format_channel_count(image.format());
}

std::size_t pixel_size(const Image& image)
{
    return pixel_format_bytes_per_pixel(image.format());
}

bool is_valid(const Image& image)
{
    return image.width() > 0
        && image.height() > 0
        && pixel_format_bytes_per_pixel(image.format())
            * size_t(image.width())
            * size_t(image.height())
        == image.pixels.size();
}

void blit(Image& destination, const Image& source, int x, int y)
{
    ASSERT(x >= 0 && y >= 0);
    ASSERT(destination.format() == source.format());

    const int x1               = std::min(destination.width(), x + source.width());
    const int y1               = std::min(destination.height(), y + source.height());
    const size_t pixel_bytes   = pixel_format_bytes_per_pixel(destination.format());
    const size_t blit_row_size = (x1 - x) * pixel_bytes;

    for (int yi = y; yi < y1; yi++) {
        const size_t dest_index = (yi * destination.width() + x) * pixel_bytes;
        const size_t src_index  = ((yi - y) * source.width()) * pixel_bytes;
        std::memcpy(&destination.pixels[dest_index], &source.pixels[src_index], blit_row_size);
    }
}

static stbir_pixel_layout stbir_layout_for_channels(size_t channels)
{
    switch (channels) {
    case 3: return STBIR_RGB;
    case 4: return STBIR_RGBA;
    default:
        throw std::runtime_error("Unsupported channel count");
    }
}

Image half_sampled(const Image& image)
{
    const int half_w = std::max(1, image.width() / 2);
    const int half_h = std::max(1, image.height() / 2);

    const size_t channels = channel_count(image);
    ASSERT(channels == pixel_size(image)); // only 8-bit channels supported currently

    Image::Data half_pixels(static_cast<size_t>(half_w) * half_h * channels);

    stbir_resize_uint8_linear(
        image.pixels.data(),
        image.width(),
        image.height(),
        image.width() * static_cast<int>(channels),
        half_pixels.data(),
        half_w,
        half_h,
        half_w * static_cast<int>(channels),
        stbir_layout_for_channels(channels)
    );

    return {image.format(), half_w, half_h, std::move(half_pixels)};
}

Image rescaled_with_preserved_ratio(const Image& image, const Size& target_size)
{
    const size_t channels = channel_count(image);
    ASSERT(channels == pixel_size(image)); // only 8-bit channels supported currently

    Image::Data result(static_cast<size_t>(target_size.width) * target_size.height * channels, 0);

    Size scaled_size{image.width(), image.height()};
    scaled_size.scale(target_size, Size::ScaleMode::KeepAspectRatio);

    Image::Data resized_pixels(
        static_cast<size_t>(scaled_size.width) * scaled_size.height * channels
    );

    stbir_resize_uint8_linear(
        image.pixels.data(),
        image.width(),
        image.height(),
        image.width() * static_cast<int>(channels),
        resized_pixels.data(),
        scaled_size.width,
        scaled_size.height,
        scaled_size.width * static_cast<int>(channels),
        stbir_layout_for_channels(channels)
    );

    ASSERT(target_size.width >= scaled_size.width);
    ASSERT(target_size.height >= scaled_size.height);

    const size_t offset_x = (target_size.width - scaled_size.width) / 2;
    const size_t offset_y = (target_size.height - scaled_size.height) / 2;

    for (size_t y = 0; y < static_cast<size_t>(scaled_size.height); ++y) {
        std::memcpy(
            result.data() + ((y + offset_y) * target_size.width + offset_x) * channels,
            resized_pixels.data() + y * scaled_size.width * channels,
            static_cast<size_t>(scaled_size.width) * channels
        );
    }

    return {image.format(), target_size.width, target_size.height, std::move(result)};
}

Domain::Image compress(const Domain::Image& image)
{
    DEBUG_ASSERT(image.format() == PixelFormat::RGB8 || image.format() == PixelFormat::RGBA8);

    const PixelFormat out_format =
        image.format() == PixelFormat::RGB8 ? PixelFormat::RGB_DXT1 : PixelFormat::RGBA_DXT5;

    const size_t blocks_x = (image.width() + 3) / 4;
    const size_t blocks_y = (image.height() + 3) / 4;
    const size_t expected_size =
        blocks_x * blocks_y * (out_format == PixelFormat::RGB_DXT1 ? 8 : 16);

    std::vector<uint8_t> compressed_data(expected_size);
    int compressed_size = 0;

    rygCompress(
        compressed_data.data(),
        const_cast<unsigned char*>(image.pixels.data()),
        image.width(),
        image.height(),
        image.format() == PixelFormat::RGB8 ? 3 : 4,
        out_format == Domain::PixelFormat::RGBA_DXT5,
        compressed_size
    );

    DEBUG_ASSERT(compressed_size == static_cast<int>(expected_size));
    compressed_data.resize(compressed_size);

    return Domain::Image(out_format, image.width(), image.height(), std::move(compressed_data));
}

void flip_vertical(Image& image)
{
    size_t pixel_bytes = pixel_format_bytes_per_pixel(image.format());
    size_t row_size    = image.width() * pixel_bytes;
    size_t half_height = image.height() / 2;
    for (size_t y = 0; y < half_height; ++y) {
        size_t top_index    = y * row_size;
        size_t bottom_index = (image.height() - 1 - y) * row_size;
        std::swap_ranges(
            image.pixels.begin() + top_index,
            image.pixels.begin() + top_index + row_size,
            image.pixels.begin() + bottom_index
        );
    }
}

void fill(Image& image, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const size_t n_channels = channel_count(image);
    size_t idx              = 0;
    for (auto& ch : image.pixels) {
        switch (idx) {
        case 0:
            ch = r;
            break;

        case 1:
            ch = g;
            break;

        case 2:
            ch = b;
            break;

        default:
            ch = a;
            break;
        }
        idx = (idx + 1) % n_channels;
    }
}

#if ENABLE_DEBUG_EXPORT_TO_PNG
void export_to_png_file(const Slic3r::Domain::Image& image, const std::string& path_prefix)
{
    int w = image.width();
    int h = image.height();
    int comp = int(channel_count(image));
    int stride_bytes = int(w * pixel_size(image));
    std::string filename = path_prefix + "_" + std::to_string(w) + "_" + std::to_string(h) + ".png";

    std::filesystem::path out(filename);
    out.remove_filename();
    if (!std::filesystem::exists(out))
        std::filesystem::create_directories(out);

    if (stbi_write_png(filename.c_str(), w, h, comp, image.pixels.data(), stride_bytes) == 0)
        PANIC("Unable to save thumbnail to file: " + filename);
}

void export_to_png_file(const Slic3r::Domain::Images& images, const std::string& path_prefix)
{
    for (const auto& image : images) {
        export_to_png_file(image, path_prefix);
    }
}
#endif // ENABLE_DEBUG_EXPORT_TO_PNG

} // namespace Slic3r::Biz::Algorithms::ImageUtils
