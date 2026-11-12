#include "libslic3r/GCode/Thumbnails.hpp"

#include <magic_enum/magic_enum.hpp>
#include <qoi.h>
#include <jpeglib.h>
#include <jmorecfg.h>
#include <stdlib.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>
#include <string>
#include <cstdint>
#include <magic_enum/magic_enum.hpp>

#include "Slic3r/Domain/enum_bitmask.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep
#include "libslic3r/Point.hpp"
#include "miniz.h"

namespace Slic3r::GCodeThumbnails {

using namespace std::literals;
using Domain::GCodeThumbnailsFormat;

struct CompressedPNG : CompressedImageBuffer 
{
    ~CompressedPNG() override { if (data) mz_free(data); }
    std::string_view tag() const override { return "thumbnail"sv; }
};

struct CompressedJPG : CompressedImageBuffer
{
    ~CompressedJPG() override { free(data); }
    std::string_view tag() const override { return "thumbnail_JPG"sv; }
};

struct CompressedQOI : CompressedImageBuffer 
{
    ~CompressedQOI() override { free(data); }
    std::string_view tag() const override { return "thumbnail_QOI"sv; }
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_png(const Domain::Image& thumbnail)
{
    auto out  = std::make_unique<CompressedPNG>();
    out->data = tdefl_write_image_to_png_file_in_memory_ex(
        thumbnail.pixels.data(),
        thumbnail.width(),
        thumbnail.height(),
        4,
        &out->size,
        MZ_DEFAULT_LEVEL,
        false
    );
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_jpg(const Domain::Image& thumbnail)
{
    // This might be useless, but the C api takes non const ptrs, so better be safe.
    std::vector<unsigned char> rgba_pixels(thumbnail.pixels);
    const size_t row_size = thumbnail.width() * 4;

    // Store pointers to scanlines start for later use
    std::vector<unsigned char*> rows_ptrs;
    rows_ptrs.reserve(thumbnail.height());
    for (size_t y = 0; y < size_t(thumbnail.height()); ++y) {
        rows_ptrs.emplace_back(&rgba_pixels[y * row_size]);
    }

    std::vector<unsigned char> compressed_data(thumbnail.pixels.size());
    unsigned char* compressed_data_ptr = compressed_data.data();
    unsigned long compressed_data_size = thumbnail.pixels.size();

    jpeg_error_mgr err;
    jpeg_compress_struct info;
    info.err = jpeg_std_error(&err);
    jpeg_create_compress(&info);
    jpeg_mem_dest(&info, &compressed_data_ptr, &compressed_data_size);

    info.image_width      = thumbnail.width();
    info.image_height     = thumbnail.height();
    info.input_components = 4;
    info.in_color_space   = JCS_EXT_RGBA;

    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 85, TRUE);
    jpeg_start_compress(&info, TRUE);

    jpeg_write_scanlines(&info, rows_ptrs.data(), thumbnail.height());
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);

    // FIXME -> Add error checking

    auto out  = std::make_unique<CompressedJPG>();
    out->data = malloc(compressed_data_size);
    out->size = size_t(compressed_data_size);
    ::memcpy(out->data, (const void*) compressed_data.data(), out->size);
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_qoi(const Domain::Image& thumbnail)
{
    qoi_desc desc;
    desc.width      = thumbnail.width();
    desc.height     = thumbnail.height();
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;

    auto out = std::make_unique<CompressedQOI>();
    int size;
    out->data = qoi_encode((const void*) thumbnail.pixels.data(), &desc, &size);
    out->size = size;
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(
    const Domain::Image& thumbnail,
    Domain::GCodeThumbnailsFormat format
)
{
    switch (format) {
    case Domain::GCodeThumbnailsFormat::PNG:
    default:
        return compress_thumbnail_png(thumbnail);
    case Domain::GCodeThumbnailsFormat::JPG:
        return compress_thumbnail_jpg(thumbnail);
    case Domain::GCodeThumbnailsFormat::QOI:
        return compress_thumbnail_qoi(thumbnail);
    }
}

tl::expected<RequestParsingResult, ThumbnailErrors> parse_request(
    const std::string& request,
    const std::string_view default_extension
)
{
    if (request.empty())
        return RequestParsingResult{};

    std::istringstream is(request);
    std::string point_str;

    ThumbnailErrors errors;

    RequestParsingResult result;
    while (std::getline(is, point_str, ',')) {
        Domain::Size size;
        std::istringstream iss(point_str);
        std::string coord_str;

        if (!std::getline(iss, coord_str, 'x') || coord_str.empty()) {
            errors = Domain::enum_bitmask(errors | ThumbnailError::InvalidVal);
            continue;
        }
        std::istringstream(coord_str) >> size.width;

        if (!std::getline(iss, coord_str, '/') || coord_str.empty()) {
            errors = Domain::enum_bitmask(errors | ThumbnailError::InvalidVal);
            continue;
        }
        std::istringstream(coord_str) >> size.height;

        if (0 >= size.width || size.width >= 1000 || 0 >= size.height || size.height >= 1000) {
            errors = Domain::enum_bitmask(errors | ThumbnailError::OutOfRange);
        }
        std::string ext_str;
        std::getline(iss, ext_str);

        if (ext_str.empty()) {
            ext_str = default_extension;
        }

        boost::to_upper(ext_str);
        auto format{magic_enum::enum_cast<Domain::GCodeThumbnailsFormat>(ext_str)};

        if (!format) {
            format = Domain::GCodeThumbnailsFormat::PNG;
            errors = Domain::enum_bitmask(errors | ThumbnailError::InvalidExt);
        }

        result.sizes.push_back(size);
        result.formats.push_back(*format);
    }

    if (errors != Domain::enum_bitmask<ThumbnailError>()) {
        return tl::unexpected{errors};
    }
    return result;
}

} // namespace Slic3r::GCodeThumbnails
