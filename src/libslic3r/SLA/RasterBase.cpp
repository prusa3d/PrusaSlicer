///|/ Copyright (c) Prusa Research 2020 - 2022 Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2024 Felix Reißmann @felix-rm
///|/ Copyright (c) 2022 ole00 @ole00
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLARASTER_CPP
#define SLARASTER_CPP

#include <boost/core/bit.hpp>
#include <boost/core/span.hpp>
#include <numeric>
#include <functional>

#include <libslic3r/SLA/RasterBase.hpp>
#include <libslic3r/SLA/AGGRaster.hpp>

// minz image write:
#include <miniz.h>

namespace Slic3r { namespace sla {

EncodedRaster PNGRasterEncoder::operator()(const void *ptr, size_t w, size_t h,
                                           size_t      num_components)
{
    std::vector<uint8_t> buf;
    size_t s = 0;
    
    void *rawdata = tdefl_write_image_to_png_file_in_memory(
        ptr, int(w), int(h), int(num_components), &s);
    
    // On error, data() will return an empty vector. No other info can be
    // retrieved from miniz anyway...
    if (rawdata == nullptr) return EncodedRaster({}, "png");
    
    auto pptr = static_cast<std::uint8_t*>(rawdata);
    
    buf.reserve(s);
    std::copy(pptr, pptr + s, std::back_inserter(buf));
    
    MZ_FREE(rawdata);
    return EncodedRaster(std::move(buf), "png");
}

std::ostream &operator<<(std::ostream &stream, const EncodedRaster &bytes)
{
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 std::streamsize(bytes.size()));
    
    return stream;
}

EncodedRaster PPMRasterEncoder::operator()(const void *ptr, size_t w, size_t h,
                                           size_t      num_components)
{
    std::vector<uint8_t> buf;
    
    auto header = std::string("P5 ") +
            std::to_string(w) + " " +
            std::to_string(h) + " " + "255 ";
    
    auto sz = w * h * num_components;
    size_t s = sz + header.size();
    
    buf.reserve(s);

    auto buff = reinterpret_cast<const std::uint8_t*>(ptr);
    std::copy(header.begin(), header.end(), std::back_inserter(buf));
    std::copy(buff, buff+sz, std::back_inserter(buf));
    
    return EncodedRaster(std::move(buf), "ppm");
}

namespace {

class RunLengthIterate
{
    class iterator
    {
    public:
        explicit iterator(boost::span<const uint8_t> buffer) : m_buffer(buffer) {
            calculate_run_length();
        }

        std::pair<uint8_t, size_t> operator*() const {
            if (m_buffer.empty())
                return {0, 0};
            else
                return {m_buffer[0], run_length};
        }

        void operator++() {
            m_buffer = m_buffer.subspan(run_length);
            calculate_run_length();
        }

        // NOTE: Comparison ignores address of span
        // works for comparison against end() because size will be 0 in both compared elements once
        // the end is reached
        bool operator==(iterator other) { return m_buffer.size() == other.m_buffer.size(); }
        bool operator!=(iterator other) { return m_buffer.size() != other.m_buffer.size(); }

    private:
        void calculate_run_length() {
            run_length = 1;
            while (run_length + 1 < m_buffer.size()) {
                if (m_buffer[run_length - 1] == m_buffer[run_length])
                    run_length++;
                else
                    break;
            }
        }

        boost::span<const uint8_t> m_buffer;
        size_t run_length{};
    };

    boost::span<const uint8_t> m_buffer;

public:
    explicit RunLengthIterate(boost::span<const uint8_t> buffer) : m_buffer(buffer) {}

    auto begin() const { return iterator{m_buffer}; }
    auto end() const { return iterator{boost::span<const uint8_t>{}}; }
};

}

EncodedRaster GOORLERasterEncoder::operator()(
    const void *ptr, size_t w, size_t h, size_t num_components
) {
    static constexpr uint8_t magic = 0x55;
    static constexpr uint8_t delimiter[] = {0xd, 0xa};
    static constexpr uint8_t header_size = 5;

    enum class type_tag : uint8_t {
        VALUE_00 = 0b00,
        VALUE_GRAYSCALE = 0b01,
        VALUE_DIFF = 0b10,
        VALUE_FF = 0b11
    };

    enum class length_tag : uint8_t {
        RUN_LENGTH_4BIT = 0b00,
        RUN_LENGTH_12BIT = 0b01,
        RUN_LENGTH_20BIT = 0b10,
        RUN_LENGTH_28BIT = 0b11,
    };

    struct chunk_header
    {
        // NOTE: bit field layout is reversed
        uint8_t length : 4;
        uint8_t length_tag : 2;
        uint8_t type_tag : 2;
    };
    static_assert(sizeof(chunk_header) == 1, "struct is not packed");

    if (ptr == nullptr) {
        throw std::runtime_error("GOO Encoder received nullptr as image data");
    }

    // NOTE: GOO can only represent grayscale
    if (num_components != 1) {
        throw std::runtime_error("GOO Encoder received non grayscale image data");
    }

    // NOTE: Layer definition will be written beforehand by the export process

    // Write image data header
    std::vector<uint8_t> output_buffer(header_size);
    // 0-3 reserved for 4 byte of encoded image size
    output_buffer[4] = magic;

    boost::span<const uint8_t>
        input_buffer{static_cast<const uint8_t *>(ptr), w * h * num_components};

    // Write RLE encoded image data
    for (auto [value, run_length] : RunLengthIterate(input_buffer)) {
        // NOTE: type_tag::VALUE_DIFF not currently supported
        type_tag t_tag = [](uint8_t value) {
            if (value == 0)
                return type_tag::VALUE_00;
            else if (value == 255)
                return type_tag::VALUE_FF;
            else
                return type_tag::VALUE_GRAYSCALE;
        }(value);

        length_tag l_tag = [](size_t run_length) {
            if (run_length >> 4 == 0)
                return length_tag::RUN_LENGTH_4BIT;
            else if (run_length >> 12 == 0)
                return length_tag::RUN_LENGTH_12BIT;
            else if (run_length >> 20 == 0)
                return length_tag::RUN_LENGTH_20BIT;
            else
                return length_tag::RUN_LENGTH_28BIT;
        }(run_length);

        chunk_header header{};
        header.type_tag = (uint8_t) t_tag;
        header.length_tag = (uint8_t) l_tag;
        header.length = (uint8_t) (run_length & 0x0f);

        output_buffer.push_back(boost::core::bit_cast<uint8_t>(header));

        if (t_tag == type_tag::VALUE_GRAYSCALE)
            output_buffer.push_back(value);

        if (l_tag == length_tag::RUN_LENGTH_28BIT)
            output_buffer.push_back((run_length >> 20 & 0xff));
        if (l_tag >= length_tag::RUN_LENGTH_20BIT)
            output_buffer.push_back((run_length >> 12 & 0xff));
        if (l_tag >= length_tag::RUN_LENGTH_12BIT)
            output_buffer.push_back((run_length >> 4 & 0xff));
    }

    // Write encoded image size (without length, with magic and checksum) into reserved space at the
    // start of the buffer
    uint32_t encoded_length = output_buffer.size() - header_size + 2;
    output_buffer[0] = encoded_length >> 24;
    output_buffer[1] = encoded_length >> 16 & 0xff;
    output_buffer[2] = encoded_length >> 8 & 0xff;
    output_buffer[3] = encoded_length & 0xff;

    // Write checksum
    uint8_t checksum =
        ~std::reduce(output_buffer.begin() + 5, output_buffer.end(), 0, std::plus<uint8_t>{});
    output_buffer.push_back(checksum);

    // Write delimiter
    output_buffer.push_back(delimiter[0]);
    output_buffer.push_back(delimiter[1]);

    return EncodedRaster(std::move(output_buffer), "");
}

std::unique_ptr<RasterBase> create_raster_grayscale_aa(
    const Resolution        &res,
    const PixelDim          &pxdim,
    double                   gamma,
    const RasterBase::Trafo &tr)
{
    std::unique_ptr<RasterBase> rst;
    
    if (gamma > 0)
        rst = std::make_unique<RasterGrayscaleAAGammaPower>(res, pxdim, tr, gamma);
    else if (std::abs(gamma - 1.) < 1e-6)
        rst = std::make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_none());
    else
        rst = std::make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_threshold(.5));
    
    return rst;
}

} // namespace sla
} // namespace Slic3r

#endif // SLARASTER_CPP
