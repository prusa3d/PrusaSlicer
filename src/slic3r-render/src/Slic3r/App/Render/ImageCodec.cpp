#include "Slic3r/App/Render/ImageCodec.hpp"

#include <Slic3r/Log.hpp>
#include <Slic3r/Memory.hpp>
#include <Slic3r/Utils.hpp>
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"

#include <sstream>
#include <bit>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <png.h>
#include <nanosvg/nanosvg.h>
// #define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvgrast.h>
#include <jpeglib.h>

namespace Slic3r::App::Render {

using Domain::Image;
using Domain::Size;
using Domain::PixelFormat;
using Biz::Algorithms::ImageUtils::rescaled_with_preserved_ratio;
using Biz::Algorithms::ImageUtils::half_sampled;

void flip_pixels_in_y(Image::Data& pixels, size_t h, size_t row_stride)
{
    Image::Data temp_row(row_stride, 0);
    const auto h_half = h / 2;
    for (size_t row = 0; row < h_half; row++) {
        std::memcpy(temp_row.data(), &pixels[row * row_stride], row_stride);
        const auto opposite_row = h - row - 1;
        std::memcpy(&pixels[row * row_stride], &pixels[opposite_row * row_stride], row_stride);
        std::memcpy(&pixels[opposite_row * row_stride], temp_row.data(), row_stride);
    }
}

/**
 * @brief The PngInfoStruct class is a RAII for png_info & png_struct objects
 */
struct PngInfoStruct {
    PngInfoStruct() {
        png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (png) {
            info = png_create_info_struct(png);
        }
    }
    ~PngInfoStruct() {
        if (png && info) {
            png_destroy_read_struct(&png, &info, nullptr);
        } else if (png) {
            png_destroy_read_struct(&png, nullptr, nullptr);
        }
    }
    PngInfoStruct(const PngInfoStruct& rhs) = delete;
    PngInfoStruct& operator=(PngInfoStruct& rhs) = delete;

    bool valid() const { return png && info; }

    png_struct* png{nullptr};
    png_info* info{nullptr};
};

class PngReadCodec : public IImageLoadCodec
{
public:

    std::vector<Image> load(
        std::istream& is, const ImageLoadOptions& opts, Size* image_size = nullptr
    ) override;
    bool matches(const std::string& filename) override;

private:
    static void read_callback(
        png_struct* png_ptr, png_bytep out_bytes, png_size_t byte_count_to_read
    );
};

struct ImageDeleter {
    void operator()(NSVGimage* image) const { nsvgDelete(image); }
};
struct RasterizerDeleter {
    void operator()(NSVGrasterizer* rasterizer) const { nsvgDeleteRasterizer(rasterizer); }
};

using NSVGrasterizerPtr = std::unique_ptr<NSVGrasterizer, RasterizerDeleter>;
using NSVGimagePtr      = std::unique_ptr<NSVGimage, ImageDeleter>;

class SvgReadCodec : public IImageLoadCodec
{
public:
    SvgReadCodec();

    bool matches(const std::string& filename) override;
    std::vector<Image>
    load(std::istream& is, const ImageLoadOptions& opts, Size* image_size = nullptr) override;

private:
    static NSVGimagePtr load_svg(
        std::istream& input,
        const std::unordered_map<std::string, std::string>& replace_strings
    );

private:
    NSVGrasterizerPtr m_rasterizer;
};

// The layout of this struct is super important!
// It is because reinterpret_cast<JpegErrorManager*>(&jpeg_error_mgr)
// using the pointer to the first field. It is horrible,
// but it is a way to get the error out of libjpg and not crash the app.
struct JpegErrorManager
{
    jpeg_error_mgr manager{};
    jmp_buf jump_buffer{};
};

static void jpeg_error_exit(j_common_ptr cinfo)
{
    auto* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    longjmp(err->jump_buffer, 1);
}

class JpgReadCodec : public IImageLoadCodec
{
public:
    bool matches(const std::string& filename) override {
        return boost::algorithm::iends_with(filename, ".jpg")
            || boost::algorithm::iends_with(filename, ".jpeg");
    }

    std::vector<Image>
    load(std::istream& is, const ImageLoadOptions& opts, Size* image_size = nullptr) override
    {
        std::vector<unsigned char> buffer{
            std::istreambuf_iterator<char>(is),
            std::istreambuf_iterator<char>()
        };

        if (!is && !is.eof()) {
            SPDLOG_ERROR("failed reading JPEG stream");
            return {};
        }
        if (buffer.empty()) {
            SPDLOG_ERROR("empty JPEG stream");
            return {};
        }

        jpeg_decompress_struct cinfo{};
        JpegErrorManager jerr{};

        // Our handler jumps to jerr.jump_buffer, few lines bellow.
        jerr.manager.error_exit = jpeg_error_exit;
        cinfo.err = jpeg_std_error(&jerr.manager);

        bool created = false;
        bool started = false;

        // The rest of the function avoid a scope guard, as it would be triggered
        // by a longjmp here and trigger things that might not be valid.
        if (setjmp(jerr.jump_buffer)) {
            SPDLOG_ERROR("JPEG decode failed");

            if (started) {
                jpeg_abort_decompress(&cinfo);
            }

            if (created) {
                jpeg_destroy_decompress(&cinfo);
            }

            return {};
        }

        jpeg_create_decompress(&cinfo);
        created = true;

        jpeg_mem_src(&cinfo, buffer.data(),
                     static_cast<unsigned long>(buffer.size()));

        jpeg_read_header(&cinfo, TRUE);
        jpeg_start_decompress(&cinfo);
        started = true;

        const int width = static_cast<int>(cinfo.output_width);
        const int height = static_cast<int>(cinfo.output_height);
        const int channels = cinfo.output_components;

        if (image_size) {
            *image_size = {width, height};
        }

        if (channels != 3) {
            SPDLOG_ERROR("unsupported JPEG format (channels={})", channels);
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return {};
        }

        PixelFormat format{PixelFormat::RGB8};

        const std::size_t stride = static_cast<std::size_t>(width) * channels;
        const std::size_t size = static_cast<std::size_t>(height) * stride;

        std::vector<unsigned char> pixels(size);

        while (cinfo.output_scanline < cinfo.output_height) {
            unsigned char* row =
                pixels.data() + cinfo.output_scanline * stride;

            jpeg_read_scanlines(&cinfo, &row, 1);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);

        return {Image{
            format,
            width,
            height,
            std::move(pixels)
        }};
    }
};

std::vector<Image> PngReadCodec::load(
    std::istream& is, const ImageLoadOptions& opts, Size* image_size
)
{
    static const constexpr int PNG_SIG_BYTES = 8;
    std::vector<Image> ret;

    std::vector<png_byte> sig(PNG_SIG_BYTES, 0);
    is.read(reinterpret_cast<char*>(sig.data()), PNG_SIG_BYTES);
    if (!png_check_sig(sig.data(), PNG_SIG_BYTES)) {
        SPDLOG_ERROR("Failed parsing PNG header");
        return ret;
    }

    PngInfoStruct png_info_struct;
    if (!png_info_struct.valid()) {
        SPDLOG_ERROR("Failed create PNG parsing object");
        return ret;
    }

    png_set_read_fn(png_info_struct.png, static_cast<void*>(&is), read_callback);

    // Tell that we have already read the first bytes to check the signature
    png_set_sig_bytes(png_info_struct.png, PNG_SIG_BYTES);

    png_read_info(png_info_struct.png, png_info_struct.info);

    int image_width = png_get_image_width(png_info_struct.png, png_info_struct.info);
    int image_height = png_get_image_height(png_info_struct.png, png_info_struct.info);
    size_t color_type = png_get_color_type(png_info_struct.png, png_info_struct.info);
    size_t bit_depth = png_get_bit_depth(png_info_struct.png, png_info_struct.info);

    // Expand palette PNG to RGB/RGBA.
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_info_struct.png);
    }

    // Expand transparency chunk to alpha channel if present.
    if (png_get_valid(png_info_struct.png, png_info_struct.info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_info_struct.png);
    }

    // Convert 1/2/4-bit grayscale to 8-bit
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_info_struct.png);
    }

    // Transform Grayscale/Grayscale + Alpha to RGB/RGBA
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_info_struct.png);
    }

    // Strip 16-bit to 8-bit
    if (bit_depth == 16) {
        png_set_strip_16(png_info_struct.png);
    }

    // Recompute PNG info after all requested transforms.
    png_read_update_info(png_info_struct.png, png_info_struct.info);

    color_type = png_get_color_type(png_info_struct.png, png_info_struct.info);
    bit_depth  = png_get_bit_depth(png_info_struct.png, png_info_struct.info);
    size_t channels = png_get_channels(png_info_struct.png, png_info_struct.info);
    size_t pixel_stride = bit_depth / 8 * channels;
    PixelFormat out_format;

    switch (color_type) {
    case PNG_COLOR_TYPE_RGB:
        out_format = PixelFormat::RGB8;
        break;

    case PNG_COLOR_TYPE_RGBA:
        out_format = PixelFormat::RGBA8;
        break;

    default:
        SPDLOG_ERROR("Unsupported PNG format after expansion with color type: {}", color_type);
        return ret;
    }

    if (bit_depth != 8) {
        SPDLOG_ERROR("Unsupported PNG bit depth after expansion: {}", bit_depth);
        return ret;
    }

    const Size out_size{image_width, image_height};
    // Store PNG real size
    if (image_size) {
        *image_size = out_size;
    }

    Image::Data out_pixels(image_width * image_height * pixel_stride, 0);

    auto readbuf = static_cast<png_bytep>(out_pixels.data());
    for (size_t r = 0; r < static_cast<size_t>(image_height); ++r) {
        size_t r_idx = opts.flip_y ? image_height - r - 1 : r;
        png_read_row(png_info_struct.png, readbuf + r_idx * image_width * pixel_stride, nullptr);
    }

    const Size resolved_size = opts.resolve_to_size(out_size);

    Image img(out_format, image_width, image_height, std::move(out_pixels));
    if (resolved_size != out_size) {
        ret.emplace_back(rescaled_with_preserved_ratio(img, resolved_size));
    } else
        ret.emplace_back(std::move(img));

    if (opts.gen_mipmaps) {
        while (ret.back().width() > 1 || ret.back().height() > 1) {
            ret.emplace_back(half_sampled(ret.back()));
        }
    }

    if (opts.compressed) {
        for (auto& i : ret) {
            i = Biz::Algorithms::ImageUtils::compress(i);
        }
    }

    return ret;
}

void PngReadCodec::read_callback(
    png_struct* png_ptr, png_bytep out_bytes, png_size_t byte_count_to_read
)
{
    // Retrieve our input buffer through the png_ptr
    auto reader = static_cast<std::istream*>(png_get_io_ptr(png_ptr));

    if (!reader || !reader->good())
        return;

    reader->read(reinterpret_cast<char*>(out_bytes), byte_count_to_read);
}
bool PngReadCodec::matches(const std::string& filename)
{
    return boost::algorithm::iends_with(filename, ".png");
}

std::vector<Image> SvgReadCodec::load(
    std::istream& is, const ImageLoadOptions& opts, Size* image_size
)
{
    std::vector<Image> ret;
    NSVGimagePtr image = load_svg(is, opts.replace_strings);

    if (!image) {
        SPDLOG_ERROR("Failed parsing SVG file");
        return ret;
    }

    if (!m_rasterizer.get()) {
        SPDLOG_ERROR("SVG rasterizer failed");
        return ret;
    }

    Size size = opts.resolve_to_size(
        {static_cast<int>(image->width), static_cast<int>(image->height)}
    );

    float scale_w = static_cast<float>(size.width) / image->width;
    float scale_h = static_cast<float>(size.height) / image->height;

    // Store SVG size, not the scaled one
    if (image_size) {
        image_size->width = image->width;
        image_size->height = image->height;
    }

    do {
        // std::vector<unsigned char> data(n_pixels * 4, 0);
        Image::Data pixels(size.space() * 4, 0);
        nsvgRasterizeXY(
            m_rasterizer.get(), image.get(), 0, 0, scale_w, scale_h, pixels.data(), size.width, size.height,
            size.width * 4
        );

        if (opts.flip_y)
            flip_pixels_in_y(pixels, size.height, size.width * 4);

        // if (std::any_of(pixels.begin(), pixels.end(), [](auto x) { return x != 0; })) {
        //     SPDLOG_INFO("Non empty image with size {}x{} added", size.width, size.height);
        // } else {
        //     SPDLOG_INFO("Empty image with size {}x{} added", size.width, size.height);
        // }

        ret.emplace_back(PixelFormat::RGBA8, size.width, size.height, std::move(pixels));

        if (opts.gen_mipmaps) {
            if (size.width == 1 && size.height == 1)
                break;

            if (size.width > 1)
                size.width /= 2;
            if (size.height > 1)
                size.height /= 2;

            scale_w = (float) size.width / image->width;
            scale_h = (float) size.height / image->height;
        }
    } while (opts.gen_mipmaps);

    if (opts.compressed) {
        for (auto& i : ret) {
            i = Biz::Algorithms::ImageUtils::compress(i);
        }
    }

    return ret;
}

NSVGimagePtr SvgReadCodec::load_svg(
    std::istream& input,
    const std::unordered_map<std::string, std::string>& replace_strings
)
{
    const char* units = "px";
    const float dpi = 96;

    std::stringstream buffer;
    buffer << input.rdbuf();

    std::string svg = buffer.str();
    for (const auto& [from, to] : replace_strings) {
        boost::replace_all(svg, from, to);
    }
    return NSVGimagePtr(nsvgParse(svg.data(), units, dpi));
}

SvgReadCodec::SvgReadCodec() : m_rasterizer(nsvgCreateRasterizer()) {}

bool SvgReadCodec::matches(const std::string& filename)
{
    return boost::algorithm::iends_with(filename, ".svg");
}

ImageCodecManager::ImageCodecManager()
{
    // register known codecs
    m_loaders.emplace_back(new PngReadCodec());
    m_loaders.emplace_back(new SvgReadCodec());
    m_loaders.emplace_back(new JpgReadCodec());
}

ImageCodecManager& ImageCodecManager::instance()
{
    static ImageCodecManager inst;
    return inst;
}

IImageLoadCodec* ImageCodecManager::find_loader(const std::string& filename)
{
    for (const auto& reader : m_loaders) {
        if (reader->matches(filename)) {
            return reader.get();
        }
    }

    return nullptr;
}

Size ImageLoadOptions::resolve_to_size(const Size& source_size) const
{
    Size resolved_size =
        source_size.scaled(Size{max_size_px, max_size_px}, Size::ScaleMode::KeepAspectRatio);

    if (force_power_of_two) {
        int size = std::bit_ceil(
            static_cast<uint32_t>(std::max(resolved_size.width, resolved_size.height))
        );
        resolved_size.width = size;
        resolved_size.height = size;
    }

    return resolved_size;
}

} // namespace Slic3r::App::Render
