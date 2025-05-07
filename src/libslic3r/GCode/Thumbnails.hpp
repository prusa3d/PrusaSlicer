///|/ Copyright (c) Prusa Research 2022 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCodeThumbnails_hpp_
#define slic3r_GCodeThumbnails_hpp_

#include <LibBGCode/binarize/binarize.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <boost/beast/core.hpp>
#include <boost/format.hpp>
#include <core/core.hpp>
#include <vector>
#include <memory>
#include <string_view>
#include <algorithm>
#include <string>
#include <utility>

#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "ThumbnailData.hpp"
#include "libslic3r/enum_bitmask.hpp"

namespace Slic3r {
class ConfigBase;

    enum class ThumbnailError : int { InvalidVal, OutOfRange, InvalidExt };
    using ThumbnailErrors = enum_bitmask<ThumbnailError>;
    ENABLE_ENUM_BITMASK_OPERATORS(ThumbnailError);
}

namespace Slic3r::GCodeThumbnails {

struct CompressedImageBuffer
{
    void       *data { nullptr };
    size_t      size { 0 };
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format);

typedef std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> GCodeThumbnailDefinitionsList;

using namespace std::literals;
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext = "PNG"sv);
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase &config);

std::string get_error_string(const ThumbnailErrors& errors);

template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback &thumbnail_cb, const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>>& thumbnails_list, WriteToOutput output, ThrowIfCanceledCallback throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb != nullptr) {
        for (const auto& [format, size] : thumbnails_list) {
            static constexpr const size_t max_row_length = 78;
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ {size}, true, true, true, true });
            for (const ThumbnailData& data : thumbnails)
                if (data.is_valid()) {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data && compressed->size) {
                        std::string encoded;
                        encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                        encoded.resize(boost::beast::detail::base64::encode((void*)encoded.data(), (const void*)compressed->data, compressed->size));

                        output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width % data.height % encoded.size()).str().c_str());

                        while (encoded.size() > max_row_length) {
                            output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                            encoded = encoded.substr(max_row_length);
                        }

                        if (encoded.size() > 0)
                            output((boost::format("; %s\n") % encoded).str().c_str());

                        output((boost::format("; %s end\n;\n") % compressed->tag()).str().c_str());
                    }
                    throw_if_canceled();
                }
        }
    }
}

template<typename ThrowIfCanceledCallback>
inline void generate_binary_thumbnails(ThumbnailsGeneratorCallback& thumbnail_cb, std::vector<bgcode::binarize::ThumbnailBlock>& out_thumbnails,
    const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> &thumbnails_list, ThrowIfCanceledCallback throw_if_canceled)
{
    using namespace bgcode::core;
    using namespace bgcode::binarize;
    out_thumbnails.clear();
    assert(thumbnail_cb != nullptr);
    if (thumbnail_cb != nullptr) {
        for (const auto& [format, size] : thumbnails_list) {
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ {size}, true, true, true, true });
            for (const ThumbnailData &data : thumbnails)
                if (data.is_valid()) {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data != nullptr && compressed->size > 0) {
                        ThumbnailBlock& block = out_thumbnails.emplace_back(ThumbnailBlock());
                        block.params.width = (uint16_t)data.width;
                        block.params.height = (uint16_t)data.height;
                        switch (format) {
                        case GCodeThumbnailsFormat::PNG: { block.params.format = (uint16_t)EThumbnailFormat::PNG; break; }
                        case GCodeThumbnailsFormat::JPG: { block.params.format = (uint16_t)EThumbnailFormat::JPG; break; }
                        case GCodeThumbnailsFormat::QOI: { block.params.format = (uint16_t)EThumbnailFormat::QOI; break; }
                        }
                        block.data.resize(compressed->size);
                        memcpy(block.data.data(), compressed->data, compressed->size);
                    }
                }
        }
    }
}

} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
