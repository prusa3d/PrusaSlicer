#pragma once

#include "Slic3r/App/Render/TextImageGenerator.hpp"

#include "Slic3r/Domain/Point.hpp"

#include <memory>

namespace Slic3r::Domain {
class Bed;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Texture;
class TextureManager;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

class BedRenderHelper
{
public:
    static size_t bed_texture_size() { return s_bed_texture_size; }
    static void set_bed_texture_size(size_t size) { s_bed_texture_size = size; }

    /**
     * @brief Load the bed texture and return it.
     *
     * @param bed The bed whose texture is required.
     * @param manager The TextureManager instance to create texture within
     *
     * @return bed texture instance.
     *
     * @note The filename of the texture is specified into the bed, see Slic3r::Domain::Bed definition.
     * @note The texture size is equal to half of the max texture size supported by the graphic card.
     */
    [[nodiscard]] static std::shared_ptr<Render::Texture> texture(const Domain::Bed& bed, Render::TextureManager& manager);

    /**
     * @brief Render the bed label texture and return it.
     *
     * @param label The label to render.
     * @param manager The TextureManager instance to create texture within
     * @param color The color to assign to the renderer text
     *
     * @return bed label texture instance.
     */
    [[nodiscard]] static std::shared_ptr<Render::Texture> label_texture(const std::string& label, Render::TextureManager& manager,
        const std::optional<Domain::ColorRGB>& color = std::nullopt);

    /**
     * @brief Return the geometry of the bed grid.
     *
     * @param bed The bed whose grid is required.
     *
     * @return the geometry of the bed grid as a std::vector of vertices, two vertices for each segment.
     *
     * @note The bed contour is specified into the bed, see Slic3r::Domain::Bed definition.
     */
    [[nodiscard]] static std::vector<Domain::Vec3f> plate_grid(const Domain::Bed& bed);

private:
    /**
     * @brief Size of the bed texture in pixels.
     */
    static size_t s_bed_texture_size;

    /**
      * @brief Renderer of text into Render::Image.
      */
    static std::unique_ptr<Render::TextImageGenerator> s_text_to_image;
};

} // namespace Slic3r::App::Scene
