#pragma once

#include <istream>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/Size.hpp"

namespace Slic3r::App::Render {

struct ImageLoadOptions
{
    int max_size_px{1'024};
    bool force_power_of_two{false};
    bool gen_mipmaps{false};
    bool flip_y{false};
    bool compressed{false};

    std::unordered_map<std::string, std::string> replace_strings;

    Domain::Size resolve_to_size(const Domain::Size& source_size) const;
};

/**.
 * Interface for image loading codec.
 */
class IImageLoadCodec
{
public:
    virtual ~IImageLoadCodec() = default;

    /**
     * Check if filename can be read by this codec.
     * @param filename A name of file to detect if codec can be used.
     * @return True if codec supports loading given filename, otherwise false.
     */
    virtual bool matches(const std::string& filename) = 0;

    /**
     * Load image(s) from given input stream with optional loading options.
     * @param is Input stream with image data to read.
     * @param opts
     * @param image_size optional pointer to Size that will contain the size of the image
     * @return List of loaded image mipmaps (if its generation is enabled in loading options),
     * or a single image (if mipmaps disabled) or empty vector if error happened.
     */
    virtual std::vector<Domain::Image> load(
        std::istream& is,
        const ImageLoadOptions& opts,
        Domain::Size* image_size = nullptr
    ) = 0;
};

/**
 * Image Codec Registry holding all known image codecs.
 */
class ImageCodecManager
{
public:
    static ImageCodecManager& instance();

    IImageLoadCodec* find_loader(const std::string& filename);

private:
    ImageCodecManager();

private:
    using LoaderList = std::vector<std::unique_ptr<IImageLoadCodec>>;
    LoaderList m_loaders;
};

} // namespace Slic3r::App::Render
