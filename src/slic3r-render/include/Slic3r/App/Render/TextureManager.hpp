#pragma once

#include <string>
#include <unordered_map>

#include "Texture.hpp"
#include "ImageCodec.hpp"

namespace Slic3r::App::Render {

class IResourceResolver;

struct TextureKey
{
    std::string key_name;
    int width = 0;
    int height = 0;
    Domain::PixelFormat pixel_format = Domain::PixelFormat::RGBA8; // consider removing it from key
    std::unordered_map<std::string, std::string> replace_strings;

    bool operator==(const TextureKey& rhs) const;
    bool operator!=(const TextureKey& rhs) const;
};
} // namespace Slic3r::App::Render

// hash function for TextureKey
namespace std {
template<>
struct hash<Slic3r::App::Render::TextureKey>
{
    std::uint64_t operator()(const Slic3r::App::Render::TextureKey& val) const;
};
} // namespace std

namespace Slic3r::App::Render {
class Context;
class Device;

class TextureManager
{
    friend class Context;
public:
    explicit TextureManager(Device& device);

    /**
     * @return Shared pointer with the texture, that HAS to be released before context destruction
     * @warning as of now, returned TexturePtr is not const and Texture is not itself immutable
     * be aware that changing Texture state will change Texture for all users of the same file (in
     * the same resolution)
     */
    TexturePtr get_or_create_image(const std::string& filename, const ImageLoadOptions& opts = {});
    /**
     * @return Shared pointer with the texture, that HAS to be released before context destruction
     * @note use this method when you are not working with real images or files that represents an image
     */
    TexturePtr get_or_create_dynamic(const std::string& name, Domain::PixelFormat pf, size_t w, size_t h);

    /**
     * @brief Drop all cached image textures and the remembered on-disk size for the given filename.
     * @note Existing TexturePtr holders keep a valid texture; subsequent get_or_create_image calls
     *       will miss the cache and reload from disk.
     */
    void invalidate_image(const std::string& filename);

    void shutdown();

    static void set_resource_resolver(std::unique_ptr<IResourceResolver> resource_resolver);
    static IResourceResolver& resource_resolver();

private:
    // We are storing weak pointers, otherwise TextureManager would be counted as a consumer.
    // But with a weak_ptr once all consumers ceased to the Texture, we will be notified
    // with a custom deleter and we can remove the Texture from our map
    using TextureMap = std::unordered_map<TextureKey, std::weak_ptr<Texture>>;
    using DynamicTextureMap = std::unordered_map<std::string, std::weak_ptr<Texture>>;

    // stored size of filenames
    using ImageSizeMap = std::unordered_map<std::string, Domain::Size>;

    static std::unique_ptr<IResourceResolver> m_resource_resolver;

    Device& m_device;
    TextureMap m_image_textures;
    ImageSizeMap m_image_sizes;
    DynamicTextureMap m_dynamic_textures;
};

} // namespace Slic3r::App::Render
