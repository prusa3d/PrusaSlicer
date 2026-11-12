#pragma once

#include <memory>
#include <vector>

#include <boost/functional/hash.hpp>

#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

namespace Slic3r::Domain {
class ModelVolume;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater::MMPainting {

/**
 * @brief Cache key of a painted volume's geometry: the volume id plus its painting timestamp.
 */
struct MMPaintedVolumeGeometryId
{
    std::size_t volume_id{0};
    Domain::ObjectBase::Timestamp facets_timestamp{0};

    bool operator==(const MMPaintedVolumeGeometryId& rhs) const = default;
};

/**
 * @brief Builds the cache key of the volume's currently painted geometry.
 *
 * @param model_volume Volume to build the key for.
 * @return Key identifying the geometry of the volume's current painting.
 */
MMPaintedVolumeGeometryId mm_painted_volume_geometry_id(const Domain::ModelVolume& model_volume);

/**
 * @brief Creates geometry of a painted volume with the TriangleSelector state baked into every vertex.
 *
 * @param device Device the geometry buffers are created on.
 * @param model_volume Painted volume to build the geometry from.
 * @return Geometry of the volume.
 */
std::unique_ptr<Render::Geometry>
create_mm_painted_volume_geometry(Render::Device& device, const Domain::ModelVolume& model_volume);

/**
 * @brief Builds the whole paint state palette, where state 0 is unpainted and state k is slot k - 1.
 *
 * @param default_color Color of unpainted triangles, i.e. of the extruder assigned to the volume.
 * @param slot_colors Colors of the material slots indexed from 0; slot k becomes paint state k + 1.
 * @param virtual_extruders Virtual extruders of the printer group the volume belongs to.
 * @return Palette indexed by paint state, always TriangleSelector::TRIANGLE_STATE_TYPE_COUNT entries long.
 */
std::vector<Domain::ColorRGBA> create_palette_colors(
    const Domain::ColorRGBA& default_color,
    const std::vector<Domain::ColorRGBA>& slot_colors,
    const Domain::VirtualExtruders& virtual_extruders
);

/**
 * @brief Apply the palette into the material's palette texture.
 *
 * @param device Device the palette texture is created on.
 * @param palette_colors Palette indexed by paint state, see create_palette_colors().
 * @param material Material to apply the palette to.
 */
void apply_mm_palette_to_material(
    Render::Device& device,
    const std::vector<Domain::ColorRGBA>& palette_colors,
    Render::Material& material
);

/**
 * @brief Creates the base material for painted volume nodes (without the palette texture).
 *
 * @param device Device the material's shader is looked up on.
 * @return Material drawing the palette_index attribute through the mm_gouraud_light shader.
 */
Render::Material create_mm_painted_volume_material(Render::Device& device);

} // namespace Slic3r::App::Plater::MMPainting

namespace std {
template <>
struct hash<Slic3r::App::Plater::MMPainting::MMPaintedVolumeGeometryId>
{
    using value_type = Slic3r::App::Plater::MMPainting::MMPaintedVolumeGeometryId;

    std::size_t operator()(const value_type& val) const noexcept
    {
        std::size_t ret = boost::hash_value(val.volume_id);
        boost::hash_combine(ret, val.facets_timestamp);
        return ret;
    }
};

} // namespace std
