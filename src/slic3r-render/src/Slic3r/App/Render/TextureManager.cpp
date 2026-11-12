#include "Slic3r/App/Render/TextureManager.hpp"

#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/IResourceResolver.hpp"

#include <Slic3r/Log.hpp>

#include <boost/nowide/fstream.hpp>
#include <boost/functional/hash.hpp>

namespace Slic3r::App::Render {

using Domain::Size;
using Domain::PixelFormat;
using Domain::Image;

bool TextureKey::operator==(const TextureKey& rhs) const
{
    return key_name == rhs.key_name
        && width == rhs.width
        && height == rhs.height
        && pixel_format == rhs.pixel_format
        && replace_strings == rhs.replace_strings;
}

bool TextureKey::operator!=(const TextureKey& rhs) const { return !(*this == rhs); }

std::unique_ptr<IResourceResolver> TextureManager::m_resource_resolver = nullptr;

TextureManager::TextureManager(Device& device) : m_device(device) {}

TexturePtr TextureManager::get_or_create_image(
    const std::string& filename, const ImageLoadOptions& opts
)
{
    ASSERT(!filename.empty(), "name cannot be empty");

    ImageSizeMap::const_iterator size_it = m_image_sizes.find(filename);
    if (size_it != m_image_sizes.cend()) {
        // we have loaded this file before
        // compute what loaded scale we would use
        Size scaled_size = opts.resolve_to_size(size_it->second);
        const TextureKey key{
            filename,
            scaled_size.width,
            scaled_size.height,
            Domain::PixelFormat::RGBA8,
            opts.replace_strings
        };
        TextureMap::const_iterator it = m_image_textures.find(key);
        if (it != m_image_textures.end()) {
            // we have same texture in the same size, return it
            return it->second.lock();
        }
    }

    // Texture was not yet loaded, or scaled size doesn't match
    // Create a new one

    // Load bitmap
    auto* codec = ImageCodecManager::instance().find_loader(filename);
    if (codec == nullptr) {
        SPDLOG_ERROR("Cannot find Image Reader Codec for file {}", filename);
        return nullptr;
    }

    Size size;
    std::vector<Image> images;
    {
        const std::string path = m_resource_resolver->resolve(filename);

        boost::nowide::ifstream is(path, std::ios::binary | std::ios::in);
        if (!is.good()) {
            SPDLOG_ERROR("Cannot open file {}", filename);
            return nullptr;
        }

        images = codec->load(is, opts, &size);
    }

    if (images.empty()) {
        SPDLOG_ERROR("Cannot load image {}", filename);
        return nullptr;
    }

    std::unique_ptr<Texture> tex = m_device.create_texture();
    for (size_t level = 0; level < images.size(); level++) {
        const Image& img = images[level];
        tex->set_data(img.format(), level, img.width(), img.height(), img.pixels.data(), img.pixels.size());
    }

    tex->set_filtering(
        images.size() > 1 ? TextureMinFilter::MipMapLinearLinear : TextureMinFilter::Linear,
        TextureMagFilter::Linear
    );

    tex->set_wrap_s(TextureWrap::ClampToBorder);
    tex->set_wrap_r(TextureWrap::ClampToBorder);
    tex->set_wrap_t(TextureWrap::ClampToBorder);

    m_image_sizes[filename] = size;

    Size scaled_size = opts.resolve_to_size(size);
    TextureKey key{filename, scaled_size.width, scaled_size.height, PixelFormat::RGBA8, opts.replace_strings};
    tex->set_object_name(key.key_name);
    TexturePtr shared_tex(tex.release(), [this, key](Texture* texture) {
        delete texture;
        m_image_textures.erase(key);
    });
    m_image_textures[key] = shared_tex;
    return shared_tex;
}

TexturePtr TextureManager::get_or_create_dynamic(
    const std::string& name, PixelFormat pf, size_t w, size_t h
)
{
    ASSERT(!name.empty(), "TextureManager: name cannot be empty");

    DynamicTextureMap::const_iterator it = m_dynamic_textures.find(name);
    if (it != m_dynamic_textures.end()) {
        return it->second.lock();
    }

    std::unique_ptr<Texture> tex = m_device.create_texture();
    {
        std::vector<uint8_t> data;
        data.resize(w * h * pixel_format_bytes_per_pixel(pf), 0x7f);
        tex->set_data(pf, 0, w, h, data.data(), data.size());
    }

    tex->set_object_name(name);

    TexturePtr shared_tex(tex.release(), [this, name](Texture* texture) {
        delete texture;
        m_dynamic_textures.erase(name);
    });
    m_dynamic_textures[name] = shared_tex;
    return shared_tex;
}

void TextureManager::set_resource_resolver(std::unique_ptr<IResourceResolver> resource_resolver)
{
    m_resource_resolver = std::move(resource_resolver);
}

IResourceResolver& TextureManager::resource_resolver()
{
    ASSERT(m_resource_resolver != nullptr);
    return *m_resource_resolver;
}

void TextureManager::invalidate_image(const std::string& filename)
{
    std::erase_if(m_image_textures, [&filename](const auto& pair) {
        return pair.first.key_name == filename;
    });
    m_image_sizes.erase(filename);
}

void TextureManager::shutdown()
{
    m_image_textures.clear();
    m_dynamic_textures.clear();
}

} // namespace Slic3r::App::Render

uint64_t std::hash<Slic3r::App::Render::TextureKey>::operator()(
    const Slic3r::App::Render::TextureKey& val
) const
{
    size_t ret = boost::hash_value(val.key_name);
    boost::hash_combine(ret, val.width);
    boost::hash_combine(ret, val.height);
    boost::hash_combine(ret, val.pixel_format);
    return ret;
}
