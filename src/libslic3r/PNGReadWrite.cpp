#include "PNGReadWrite.hpp"
#include "Config.hpp" // for ConfigurationError

#include <memory>

// Get the correct sleep function:
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
#include <cstdlib>

#include <cstdio>
#include <iostream>
#include <png.h>
#include <fstream>
#include <vector>



#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>


namespace Slic3r { namespace png {

struct PNGDescr {
    png_struct *png = nullptr; png_info *info = nullptr;

    PNGDescr() = default;
    PNGDescr(const PNGDescr&) = delete;
    PNGDescr(PNGDescr&&) = delete;
    PNGDescr& operator=(const PNGDescr&) = delete;
    PNGDescr& operator=(PNGDescr&&) = delete;

    ~PNGDescr()
    {
        if (png && info) png_destroy_info_struct(png, &info);
        if (png) png_destroy_read_struct( &png, nullptr, nullptr);
    }
};

bool is_png(const ReadBuf &rb)
{
    static const constexpr int PNG_SIG_BYTES = 8;

#if PNG_LIBPNG_VER_MINOR <= 2
    // Earlier libpng versions had png_sig_cmp(png_bytep, ...) which is not
    // a const pointer. It is not possible to cast away the const qualifier from
    // the input buffer so... yes... life is challenging...
    png_byte buf[PNG_SIG_BYTES];
    auto inbuf = static_cast<const std::uint8_t *>(rb.buf);
    std::copy(inbuf, inbuf + PNG_SIG_BYTES, buf);
#else
    auto buf = static_cast<png_const_bytep>(rb.buf);
#endif

    return rb.sz >= PNG_SIG_BYTES && !png_sig_cmp(buf, 0, PNG_SIG_BYTES);
}

// Buffer read callback for libpng. It provides an allocated output buffer and
// the amount of data it desires to read from the input.
static void png_read_callback(png_struct *png_ptr,
                              png_bytep   outBytes,
                              png_size_t  byteCountToRead)
{
    // Retrieve our input buffer through the png_ptr
    auto reader = static_cast<IStream *>(png_get_io_ptr(png_ptr));

    if (!reader || !reader->is_ok()) return;

    reader->read(static_cast<std::uint8_t *>(outBytes), byteCountToRead);
}


bool BackendPng::IsOk() const {
    //if (this->image_path == "") {
    if (this->m_pixel_size < 1) {
        return false;
    }
    if (this->cols < 1) {
        return false;
    }
    if (this->rows < 1) {
        return false;
    }
    return true;
}

bool BackendPng::dump() const {
    std::cerr<<"[BackendImage] \"" << this->GetPath() << "\" (OK:" << (this->IsOk()?"true":"false") << ") dump:" <<std::endl;
    if (!this->IsOk()) return false;
    for (size_t y=0; y<this->GetHeight(); y++) {
        for (size_t x=0; x<this->GetWidth(); x++) {
            if (this->m_color) {
                std::cout << (x==0?"":", ")
                    << std::to_string(this->GetRed(x, y)) << ","
                    << std::to_string(this->GetGreen(x, y)) << ","
                    << std::to_string(this->GetBlue(x, y));
                ;
            }
            else {
                std::cout << (x==0?"":",") << std::to_string(this->GetLuma(x, y));
            }
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
    return true;
}

std::string BackendPng::GetPath() const {
    return this->image_path;
}

bool BackendPng::reinitialize(bool force) {
    if (this->busy) {
        if (!force) {
            std::cerr << "[BackendPng::reinitialize] cannot run while busy loading." << std::endl;
            return false;
        }
        // else do not show an error. Assume busy was set by the caller.
    }
    // Swap each buf with a new a tmp vector to reduce the buffer capacity (memory usage):
    // std::vector<uint8_t>().swap(this->png->buf);
    if (png && info) {
        png_destroy_info_struct(png, &info);
        this->png = nullptr;
        this->info = nullptr;
    }
    if (png) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        this->png = nullptr;
    }
    /*
    if (info) {
        std::cerr << "[reinitialize] Error: Unexpected png info will be deleted." << std::endl;
        delete this->info;
        this->info = nullptr;
    }
    // Don't do this. The compiler says:
    // - "invalid use of incomplete type ‘struct png_info_def’" "png.h:484:16: note: forward declaration of ‘struct png_info_def’"
    // - "neither the destructor nor the class-specific ‘operator delete’ will be called, even if they are declared when the class is defined"
    */
    this->image_path = "";
    this->m_pixel_size = 0; // cause IsOk() to return false.
    this->error_shown = false;
    this->m_color = false;
    return true;
}

void BackendPng::Destroy() {
    this->reinitialize(true);
}

bool BackendPng::load_png_file(std::string path) {
    bool was_busy = this->busy;
    if (this->image_path == path) {
        // another thread must have already loaded it.
        return true;
    }
    if (was_busy) {
        // It is already loading.
        return false;
    }
    this->busy = true; // Set to false before *every* return (or throw, which also requires first setting this->error_shown = true).
    
    if (path == "") {
        this->busy = false;
        this->error_shown = true;
        throw ConfigurationError(
            "The fuzzy_skin_displacement_map is blank but load_png_file was called."
            " This is a programming error, not a configuration error."
        );
    }
    else if ((this->GetPath() != "")) {
        // Only reinitialize if the old path isn't "".
        this->reinitialize(true);
    }
    // Private since format-specific
    // Load data using STL: See <https://stackoverflow.com/a/21802936/4541104>
    std::vector <uint8_t> vec; // since ImageGreyscale uses template class Image with uint8_t for PxT (pixel type)
    std::ifstream file(path, std::ios::binary);
    if (file.fail()) {
        this->image_path = "";
        this->busy = false;
        this->error_shown = true;
        throw ConfigurationError(
            std::string("The fuzzy_skin_displacement_map \"")
            + path + std::string("\" does not exist. Change the path and re-slice to clear the invalid state.")
        );
        // return false;
    }
    file.unsetf(std::ios::skipws); // Do not skip \n
    std::streampos fileSize;
    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    vec.reserve(fileSize);
    vec.insert(vec.begin(),
               std::istream_iterator<uint8_t>(file),
               std::istream_iterator<uint8_t>());
    // Now translate the bytes to a png structure if that is proper:
    png::ReadBuf        rb{static_cast<void*>(vec.data()), static_cast<size_t>(fileSize)}; // pre-C++11: (void*)&pixels_[0]
    // Note: There is no real problem since fileSize is positive but
    //   Having no cast results in the compiler warning: "narrowing conversion of
    //   ‘fileSize.std::fpos<__mbstate_t>::operator std::streamoff()’
    //   from ‘std::streamoff’ {aka ‘long int’} to ‘size_t’ {aka ‘long unsigned int’} [-Wnarrowing]"
    if (load_png_stream(rb, path, true)) {
        // ^ true to keep it busy (Prevent potential multithreading-related crash where busy is false but path is not set.
        this->image_path = path;
        this->busy = false;
        return true;
    }
    this->busy = false;
    return false;
}
bool BackendPng::load_png_stream(IStream &in_buf, std::string optional_stated_path, bool next_busy) {
    // optional_stated_path is only for error messages.
    this->busy = true; // also set to true by load_png, the usual caller
    this->reinitialize(next_busy);
    // based on the decode_png from this file
    static const constexpr int PNG_SIG_BYTES = 8;

    std::vector<png_byte> sig(PNG_SIG_BYTES, 0);
    in_buf.read(sig.data(), PNG_SIG_BYTES);
    std::string path_msg = "";
    if (optional_stated_path != "")
        path_msg = " \"" + optional_stated_path + "\"";
    if (!png_check_sig(sig.data(), PNG_SIG_BYTES)) {
        this->error_shown = true;
        this->busy = next_busy;
        throw ConfigurationError(
            std::string("The fuzzy_skin_displacement_map")
            + path_msg + std::string(" is not a PNG file. Change the path and re-slice to clear the invalid state.")
        );
        // return false;
    }
    this->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!this->png) {
        this->error_shown = true;
        this->busy = next_busy;
        throw ConfigurationError(
            std::string("The fuzzy_skin_displacement_map")
            + path_msg + std::string(" is not a readable PNG file. Change the path and re-slice to clear the invalid state.")
        );
        // return false;
    }
    this->info = png_create_info_struct(this->png);
    if(!this->info) {
        this->error_shown = true;
        this->busy = next_busy;
        throw ConfigurationError(
            std::string("The fuzzy_skin_displacement_map")
            + path_msg + std::string(" is not a valid PNG file. Change the path and re-slice to clear the invalid state.")
        );
        // return false;
    }

    png_set_read_fn(this->png, static_cast<void *>(&in_buf), png_read_callback);

    // Tell that we have already read the first bytes to check the signature
    png_set_sig_bytes(this->png, PNG_SIG_BYTES);

    png_read_info(this->png, this->info);

    this->cols = png_get_image_width(this->png, this->info);
    this->rows = png_get_image_height(this->png, this->info);
    png_byte color_type = png_get_color_type(this->png, this->info);
    png_byte bit_depth  = png_get_bit_depth(this->png, this->info);
    // ^ png_byte is typedef unsigned char usually, so displaying it
    //   as a string should use std::to_string.
    // ^ png_get_bit_depth gets bits *per channel*!
    //   Therefore derive m_pixel_size from color_type also:
    if (color_type == PNG_COLOR_TYPE_RGBA && bit_depth == 8) {
        this->m_pixel_size = 4;
        this->m_color = true;
    } else if (color_type == PNG_COLOR_TYPE_RGB && bit_depth == 8) {
        this->m_pixel_size = 3;
        this->m_color = true;
    } else if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth == 8) {
        this->m_pixel_size = 1;
        this->m_color = false;
    }
    /*else if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA && bit_depth == 8) {
        this->m_pixel_size = 2;
        this->m_color = false;
    }
    */
    // ^ TODO: Allow GRAY_ALPHA somehow (causes sequential buffer overflow
    //   or "libpng error: IDAT: invalid window size (libpng)" to be thrown by libpng on load.

    if (this->m_pixel_size == 0) {
        this->error_shown = true;
        std::string type_msg = get_type_message(color_type);
        
        this->busy = next_busy;
        throw ConfigurationError(
            std::string("The fuzzy_skin_displacement_map")
            + path_msg + std::string(" is not a grayscale/truecolor PNG file.")
            + std::string(" The image is ") + std::to_string(static_cast<unsigned char>(bit_depth))
            + std::string("bpc") + type_msg
            + std::string(". Change the path and re-slice to clear the invalid state.")
        );
        // return false;
    }

    this->m_stride = m_pixel_size * this->cols;

    this->buf.resize(this->rows * this->cols * this->m_pixel_size);

    auto readbuf = static_cast<png_bytep>(this->buf.data());
    for (size_t r = 0; r < this->rows; ++r)
        png_read_row(this->png, readbuf + r * this->m_stride, nullptr);
    if (this->GetWidth() * this->GetHeight() <= 16) this->dump(); // for debug only
    this->busy = false;
    return true;
}

bool BackendPng::load_png_stream(const ReadBuf &in_buf, std::string optional_stated_path, bool next_busy) {
    // based on template<class Img> bool decode_png(const ReadBuf &in_buf, Img &out_img) from PNGReadWrite.hpp
    struct ReadBufStream: public IStream {
        const ReadBuf &rbuf_ref; size_t pos = 0;

        explicit ReadBufStream(const ReadBuf &buf): rbuf_ref{buf} {}

        size_t read(std::uint8_t *outp, size_t amount) override
        {
            if (amount > rbuf_ref.sz - pos) return 0;

            auto buf = static_cast<const std::uint8_t *>(rbuf_ref.buf);
            std::copy(buf + pos, buf + (pos + amount), outp);
            pos += amount;

            return amount;
        }

        bool is_ok() const override { return pos < rbuf_ref.sz; }
    } stream{in_buf};
    return this->load_png_stream(stream, optional_stated_path, next_busy);
}

bool BackendPng::LoadFile(std::string path) {
    if (this->image_path == path) {
        // another thread must have already loaded it.
        return true;
    }
    double delay = .25;
    double total_delay = 0.0;

    double delay_timeout = 120;
    // ^ How many seconds to wait for other thread(s)...OR milliseconds depending on sleep function used??
    //   - Values below 60 cause fuzz in images >= 1024x1024 due to not loaded yet on i7-12000F on WD SSD.
    // TODO: Adjust if milliseconds not seconds (See comment above)?
    
    while (this->busy) {
        // wait for the other thread.
        if (this->error_shown) {
            return false; // Another thread already failed to load the image.
        }
        if (total_delay >= delay_timeout) {
            std::cerr << "[BackendImage::LoadFile] waiting for other thread(s) timed out."
                         " To avoid this, implement caching (for example, see config_images)"
                         " and use the main thread only (for example, see image_opt)." << std::endl;
            // FIXME: Find a way to avoid IsOK() is false after this if the image was still loading in another thread and will have succeeded.
            break;
        }
        sleep(delay);
        total_delay += delay;
    }
    if (total_delay > 0.0) {
        if (this->GetPath() == path) {
            // Another thread already loaded the correct image.
            return true;
        }
        else if (this->IsOk()) {
            // Another thread probably already loaded the correct image.
            return true;
        }
        else if (this->error_shown) {
            // Assume another thread already failed,
            //   otherwise a load_ method may throw more than once
            //   (display more than one error dialog) when the path is
            //   not blank but this->GetPath() is blank (this->IsOk() is false)
            //   and there is more than one thread that may call load on the
            //   same BackendPng.
            return false;
        }
        std::cerr << "[BackendPng::LoadFile] The thread is in an unknown state." << std::endl;
        // return true;
    }
    // TODO: Support other formats if another headless (non-wx) image loader besides libpng is in the project.
    return this->load_png_file(path);
}


size_t BackendPng::GetWidth() const {
    return this->cols;
}

size_t BackendPng::GetHeight() const {
    return this->rows;
}

bool BackendPng::clamp(size_t& x, size_t& y) const {
    bool was_in_bounds = true;
    if (x >= this->GetWidth()) {
        was_in_bounds = false;
        x %= this->GetWidth();
    }
    if (y >= this->GetHeight()) {
        was_in_bounds = false;
        y %= this->GetHeight();
    }
    return was_in_bounds;
}

std::string BackendPng::get_type_message(png_byte color_type) const {
    std::string type_msg = "";
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        type_msg = " with indexed color";
    } else if (color_type == PNG_COLOR_TYPE_GRAY) {
        type_msg = " greyscale";
    } else if (color_type == PNG_COLOR_TYPE_RGB) {
        type_msg = " RGB";
    } else if (color_type == PNG_COLOR_TYPE_RGBA) {
        type_msg = " RGBA";
    } else if (color_type == PNG_COLOR_TYPE_GA) {
        type_msg = " greyscale+alpha";
    } else {
        type_msg = "type " + std::to_string(static_cast<unsigned char>(color_type));
    }
    return type_msg;
}

uint8_t BackendPng::GetRed(size_t x, size_t y) const {
    // PNG stores each pixel in RGBA order unless png_set_bgr is called,
    //   or png_set_swap_alpha is called to move A to beginning
    //   (See <http://www.libpng.org/pub/png/libpng-1.2.5-manual.html>).
    this->clamp(x, y);
    return buf[y * this->m_stride + x * this->m_pixel_size];
}

uint8_t BackendPng::GetGreen(size_t x, size_t y) const {
    this->clamp(x, y);
    if (!this->m_color)
        return buf[y * this->m_stride + x * this->m_pixel_size];
    return buf[y * this->m_stride + x * this->m_pixel_size + 1];
}

uint8_t BackendPng::GetBlue(size_t x, size_t y) const {
    this->clamp(x, y);
    if (!this->m_color)
        return buf[y * this->m_stride + x * this->m_pixel_size];
    return buf[y * this->m_stride + x * this->m_pixel_size + 2];
}

uint8_t BackendPng::GetLuma(size_t x, size_t y) const {
    this->clamp(x, y);
    if (this->m_pixel_size < 3) { // GRAY or GRAY_ALPHA
        return buf[y * this->m_stride + x * this->m_pixel_size];
    }
    size_t start = y * this->m_stride + x * this->m_pixel_size;
    // To get something close to perceptible luminance, use the multipliers from
    //   Rec. 709 such as used in HDTV. The multipliers total 1.0, so no additional
    //   math is necessary to convert back to a byte range.
    return static_cast<uint8_t>(
        static_cast<float>(buf[start]) * .2126f // R
        + static_cast<float>(buf[start+1]) * .7152f // G
        + static_cast<float>(buf[start+2]) * .0722f // B
    );
}


bool decode_png(IStream &in_buf, ImageGreyscale &out_img)
{
    static const constexpr int PNG_SIG_BYTES = 8;

    std::vector<png_byte> sig(PNG_SIG_BYTES, 0);
    in_buf.read(sig.data(), PNG_SIG_BYTES);
    if (!png_check_sig(sig.data(), PNG_SIG_BYTES))
        return false;

    PNGDescr dsc;
    dsc.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr,
                                     nullptr);

    if(!dsc.png) return false;

    dsc.info = png_create_info_struct(dsc.png);
    if(!dsc.info) return false;

    png_set_read_fn(dsc.png, static_cast<void *>(&in_buf), png_read_callback);

    // Tell that we have already read the first bytes to check the signature
    png_set_sig_bytes(dsc.png, PNG_SIG_BYTES);

    png_read_info(dsc.png, dsc.info);

    out_img.cols = png_get_image_width(dsc.png, dsc.info);
    out_img.rows = png_get_image_height(dsc.png, dsc.info);
    size_t color_type = png_get_color_type(dsc.png, dsc.info);
    size_t bit_depth  = png_get_bit_depth(dsc.png, dsc.info);

    if (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 8)
        return false;

    out_img.buf.resize(out_img.rows * out_img.cols);

    auto readbuf = static_cast<png_bytep>(out_img.buf.data());
    for (size_t r = 0; r < out_img.rows; ++r)
        png_read_row(dsc.png, readbuf + r * out_img.cols, nullptr);

    return true;
}

// Down to earth function to store a packed RGB image to file. Mostly useful for debugging purposes.
// Based on https://www.lemoda.net/c/write-png/
// png_color_type is PNG_COLOR_TYPE_RGB or PNG_COLOR_TYPE_GRAY
//FIXME maybe better to use tdefl_write_image_to_png_file_in_memory() instead?
static bool write_rgb_or_gray_to_file(const char *file_name_utf8, size_t width, size_t height, int png_color_type, const uint8_t *data)
{
    bool         result       = false;

    // Forward declaration due to the gotos.
    png_structp  png_ptr      = nullptr;
    png_infop    info_ptr     = nullptr;
    png_byte   **row_pointers = nullptr;
 
    FILE        *fp = boost::nowide::fopen(file_name_utf8, "wb");
    if (! fp) {
        BOOST_LOG_TRIVIAL(error) << "write_png_file: File could not be opened for writing: " << file_name_utf8;
        goto fopen_failed;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (! png_ptr) {
        BOOST_LOG_TRIVIAL(error) << "write_png_file: png_create_write_struct() failed";
        goto png_create_write_struct_failed;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (! info_ptr) {
        BOOST_LOG_TRIVIAL(error) << "write_png_file: png_create_info_struct() failed";
        goto png_create_info_struct_failed;
    }

    // Set up error handling.
    if (setjmp(png_jmpbuf(png_ptr))) {
        BOOST_LOG_TRIVIAL(error) << "write_png_file: setjmp() failed";
        goto png_failure;
    }

    // Set image attributes.
    png_set_IHDR(png_ptr,
        info_ptr,
        png_uint_32(width),
        png_uint_32(height),
        8, // depth
        png_color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    // Initialize rows of PNG.
    row_pointers = reinterpret_cast<png_byte**>(::png_malloc(png_ptr, height * sizeof(png_byte*)));
    {
        int line_width = width;
        if (png_color_type == PNG_COLOR_TYPE_RGB)
            line_width *= 3;
        for (size_t y = 0; y < height; ++ y) {
            auto row = reinterpret_cast<png_byte*>(::png_malloc(png_ptr, line_width));
            row_pointers[y] = row;
            memcpy(row, data + line_width * y, line_width);
        }
    }

    // Write the image data to "fp".
    png_init_io(png_ptr, fp);
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

    for (size_t y = 0; y < height; ++ y)
        png_free(png_ptr, row_pointers[y]);
    png_free(png_ptr, row_pointers);

    result = true;

png_failure:
png_create_info_struct_failed:
    ::png_destroy_write_struct(&png_ptr, &info_ptr);
png_create_write_struct_failed:
    ::fclose(fp);
fopen_failed:
    return result;
}

bool write_rgb_to_file(const char *file_name_utf8, size_t width, size_t height, const uint8_t *data_rgb)
{
    return write_rgb_or_gray_to_file(file_name_utf8, width, height, PNG_COLOR_TYPE_RGB, data_rgb);
}

bool write_rgb_to_file(const std::string &file_name_utf8, size_t width, size_t height, const uint8_t *data_rgb)
{
    return write_rgb_to_file(file_name_utf8.c_str(), width, height, data_rgb);
}

bool write_rgb_to_file(const std::string &file_name_utf8, size_t width, size_t height, const std::vector<uint8_t> &data_rgb)
{
    assert(width * height * 3 == data_rgb.size());
    return write_rgb_to_file(file_name_utf8.c_str(), width, height, data_rgb.data());
}

bool write_gray_to_file(const char *file_name_utf8, size_t width, size_t height, const uint8_t *data_gray)
{
    return write_rgb_or_gray_to_file(file_name_utf8, width, height, PNG_COLOR_TYPE_GRAY, data_gray);
}

bool write_gray_to_file(const std::string &file_name_utf8, size_t width, size_t height, const uint8_t *data_gray)
{
    return write_gray_to_file(file_name_utf8.c_str(), width, height, data_gray);
}

bool write_gray_to_file(const std::string &file_name_utf8, size_t width, size_t height, const std::vector<uint8_t> &data_gray)
{
    assert(width * height == data_gray.size());
    return write_gray_to_file(file_name_utf8.c_str(), width, height, data_gray.data());
}

// Scaled variants are mostly useful for debugging purposes, for example to export images of low resolution distance fileds.
// Scaling is done by multiplying rows and columns without any smoothing to emphasise the original pixels.
// png_color_type is PNG_COLOR_TYPE_RGB or PNG_COLOR_TYPE_GRAY
static bool write_rgb_or_gray_to_file_scaled(const char *file_name_utf8, size_t width, size_t height, int png_color_type, const uint8_t *data, size_t scale)
{
    if (scale <= 1)
        return write_rgb_or_gray_to_file(file_name_utf8, width, height, png_color_type, data);
    else {
        size_t pixel_bytes = png_color_type == PNG_COLOR_TYPE_RGB ? 3 : 1;
        size_t line_width  = width * pixel_bytes;
        std::vector<uint8_t> scaled(line_width * height * scale * scale);
        uint8_t *dst = scaled.data();
        for (size_t r = 0; r < height; ++ r) {
            for (size_t repr = 0; repr < scale; ++ repr) {
                const uint8_t *row = data + line_width * r;
                for (size_t c = 0; c < width; ++ c) {
                    for (size_t repc = 0; repc < scale; ++ repc)
                        for (size_t b = 0; b < pixel_bytes; ++ b)
                            *dst ++ = row[b];
                    row += pixel_bytes;
                }
            }
        }
        return write_rgb_or_gray_to_file(file_name_utf8, width * scale, height * scale, png_color_type, scaled.data());
    }
}

bool write_rgb_to_file_scaled(const char *file_name_utf8, size_t width, size_t height, const uint8_t *data_rgb, size_t scale)
{
    return write_rgb_or_gray_to_file_scaled(file_name_utf8, width, height, PNG_COLOR_TYPE_RGB, data_rgb, scale);
}

bool write_rgb_to_file_scaled(const std::string &file_name_utf8, size_t width, size_t height, const uint8_t *data_rgb, size_t scale)
{
    return write_rgb_to_file_scaled(file_name_utf8.c_str(), width, height, data_rgb, scale);
}

bool write_rgb_to_file_scaled(const std::string &file_name_utf8, size_t width, size_t height, const std::vector<uint8_t> &data_rgb, size_t scale)
{
    assert(width * height * 3 == data_rgb.size());
    return write_rgb_to_file_scaled(file_name_utf8.c_str(), width, height, data_rgb.data(), scale);
}

bool write_gray_to_file_scaled(const char *file_name_utf8, size_t width, size_t height, const uint8_t *data_gray, size_t scale)
{
    return write_rgb_or_gray_to_file_scaled(file_name_utf8, width, height, PNG_COLOR_TYPE_GRAY, data_gray, scale);
}

bool write_gray_to_file_scaled(const std::string &file_name_utf8, size_t width, size_t height, const uint8_t *data_gray, size_t scale)
{
    return write_gray_to_file_scaled(file_name_utf8.c_str(), width, height, data_gray, scale);
}

bool write_gray_to_file_scaled(const std::string &file_name_utf8, size_t width, size_t height, const std::vector<uint8_t> &data_gray, size_t scale)
{
    assert(width * height == data_gray.size());
    return write_gray_to_file_scaled(file_name_utf8.c_str(), width, height, data_gray.data(), scale);
}

}} // namespace Slic3r::png
