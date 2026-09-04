#pragma once
#include <boost/functional/hash.hpp>

namespace Slic3r::App::Scene {

/// Identifier for a cut island within a clipped object.
/**
 * This struct is used to uniquely identify each cut island
 * produced by the mesh clipper. The combination of `volume_id`
 * and `island_id` forms a unique key within the clipped mesh.
 */
struct MeshClipperContourId
{
    size_t volume_id{size_t(-1)}; ///< ID of the source volume.
    size_t island_id{size_t(-1)}; ///< ID of the island inside that volume.

    bool operator==(const MeshClipperContourId& rhs) const
    {
        return volume_id == rhs.volume_id && island_id == rhs.island_id;
    }

    bool operator<(const MeshClipperContourId& rhs) const
    {
        return volume_id < rhs.volume_id
            || (volume_id == rhs.volume_id && island_id < rhs.island_id);
    }

    bool is_valid() const {
        return volume_id != size_t(-1) && island_id != size_t(-1);
    }
};

enum class ClipperElementType : uint8_t
{
    Undef = 0,
    Mesh,
    Plane,
    Contour,
};

/**
 * @brief Struct used for tag of nodes for for Clipper visual elements.
 *
 * It identifies nodes that belong to the clipping plane visualization, including
 * the plane surface, clipping region, and contour outline.
 */
struct ClipperElement
{
    ClipperElementType type;
    size_t id;
    size_t island_id{size_t(-1)};

    /**
     *
     * @param rhs
     * @return
     */
    bool operator==(const ClipperElement& rhs) const
    {
        return type == rhs.type && id == rhs.id && island_id == rhs.island_id;
    }

    bool operator<(const ClipperElement& rhs) const
    {
        return type < rhs.type
            || (type == rhs.type && id < rhs.id)
            || (type == rhs.type && id == rhs.id && island_id < rhs.island_id);
    }
};
} // namespace Slic3r::App::Scene

namespace std {
template <>
struct hash<Slic3r::App::Scene::ClipperElement>
{
    using value_type = Slic3r::App::Scene::ClipperElement;

    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.type);
        boost::hash_combine(ret, val.id);
        boost::hash_combine(ret, val.island_id);
        return ret;
    }
};

} // namespace std
