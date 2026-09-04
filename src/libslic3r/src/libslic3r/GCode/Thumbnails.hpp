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
#include <tl/expected.hpp>

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/enum_bitmask.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/ThumbnailImageResult.hpp"

#include "Slic3r/Domain/Size.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"

namespace Slic3r {
    class ConfigBase;

    enum class ThumbnailError : int { InvalidVal, OutOfRange, InvalidExt };
    using ThumbnailErrors = Domain::enum_bitmask<ThumbnailError>;
    template<> struct Domain::is_enum_bitmask_type<ThumbnailError> { static constexpr const bool enable = true; };
} // namespace Slic3r

namespace Slic3r::GCodeThumbnails {

struct CompressedImageBuffer
{
    void       *data { nullptr };
    size_t      size { 0 };
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(
    const Domain::Image& thumbnail,
    Domain::GCodeThumbnailsFormat format
);

struct RequestParsingResult {
    Domain::Sizes sizes;
    std::vector<Domain::GCodeThumbnailsFormat> formats;
};

tl::expected<RequestParsingResult, ThumbnailErrors> parse_request(
    const std::string& request,
    const std::string_view default_extension = "PNG"
);

template <typename WriteToOutput, typename ThrowIfCanceledCallback>
void export_thumbnails_to_file(
    Biz::Slicing::ThumbnailImageResult thumbnails,
    const std::vector<Domain::GCodeThumbnailsFormat>& formats,
    WriteToOutput output,
    ThrowIfCanceledCallback throw_if_canceled
)
{
    static constexpr const size_t max_row_length = 78;

    ASSERT(thumbnails.images.size() == formats.size());
    for (std::size_t i{}; i < thumbnails.images.size(); ++i) {
        const Domain::Image& thumbnail{thumbnails.images.at(i)};
        Domain::GCodeThumbnailsFormat format{formats.at(i)};

        if (!Biz::Algorithms::ImageUtils::is_valid(thumbnail)) {
            continue;
        }

        auto compressed = compress_thumbnail(thumbnail, format);
        if (compressed->data && compressed->size) {
            std::string encoded;
            encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
            encoded.resize(boost::beast::detail::base64::encode((void*)encoded.data(), (const void*)compressed->data, compressed->size));

            output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % thumbnail.width() % thumbnail.height() % encoded.size()).str().c_str());

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
} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
