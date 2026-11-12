#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Transformation.hpp"

#include <admesh/stl.h>
#include <cfloat>
#include <optional>

namespace Slic3r::Biz {

// lm_FIXME: Following class might possibly be replaced by Eigen::Hyperplane
class ClippingPlane
{
    std::array<double, 4> m_data;

public:
    ClippingPlane()
    {
        *this = ClipsNothing();
    }

    ClippingPlane(const Domain::Vec3d& direction, double offset)
    {
        set_normal(direction);
        set_offset(offset);
    }

    bool operator==(const ClippingPlane& cp) const
    {
        return m_data[0] == cp.m_data[0]
            && m_data[1] == cp.m_data[1]
            && m_data[2] == cp.m_data[2]
            && m_data[3] == cp.m_data[3];
    }

    bool operator!=(const ClippingPlane& cp) const
    {
        return !(*this == cp);
    }

    double distance(const Domain::Vec3d& pt) const
    {
        // FIXME: this fails: assert(is_approx(get_normal().norm(), 1.));
        return (-get_normal().dot(pt) + m_data[3]);
    }

    bool is_point_clipped(const Domain::Vec3d& point) const
    {
        return distance(point) < 0.;
    }

    void set_normal(const Domain::Vec3d& normal)
    {
        const Domain::Vec3d norm_dir = normal.normalized();
        m_data[0]                    = norm_dir.x();
        m_data[1]                    = norm_dir.y();
        m_data[2]                    = norm_dir.z();
    }

    void set_offset(double offset)
    {
        m_data[3] = offset;
    }

    double get_offset() const
    {
        return m_data[3];
    }

    Domain::Vec3d get_normal() const
    {
        return Domain::Vec3d(m_data[0], m_data[1], m_data[2]);
    }

    void invert_normal()
    {
        m_data[0] *= -1.0;
        m_data[1] *= -1.0;
        m_data[2] *= -1.0;
    }

    ClippingPlane inverted_normal() const
    {
        return ClippingPlane(-get_normal(), get_offset());
    }

    bool is_active() const
    {
        return m_data[3] != DBL_MAX;
    }

    static ClippingPlane ClipsNothing()
    {
        return ClippingPlane(Domain::Vec3d(0., 0., 1.), DBL_MAX);
    }

    const std::array<double, 4>& get_data() const
    {
        return m_data;
    }

    // Serialization through cereal library
    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(m_data[0], m_data[1], m_data[2], m_data[3]);
    }
};

// MeshClipper class cuts a mesh and is able to return a triangulated cut.
class MeshClipper
{
public:
    // Set whether the cut should be triangulated and whether a cut
    // contour should be calculated and shown.
    void set_behaviour(bool fill_cut, double contour_width);

    // Inform MeshClipper about which plane we want to use to cut the mesh
    // This is supposed to be in world coordinates.
    void set_plane(const ClippingPlane& plane);

    // In case the object is clipped by two planes (e.g. in case of sinking
    // objects), this will be used to clip the triagnulated cut.
    // Pass ClippingPlane::ClipsNothing to turn this off.
    void set_limiting_plane(const ClippingPlane& plane);

    // Which mesh to cut. MeshClipper remembers const * to it, caller
    // must make sure that it stays valid.
    void set_mesh(const indexed_triangle_set& mesh);

    void set_negative_mesh(const indexed_triangle_set& mesh);
    /*
        template <class It>
        void set_mesh(const Range<It>& csgrange, bool copy_meshes = false)
        {
            if (!csg::is_same(range(m_csgmesh), csgrange)) {
                m_csgmesh.clear();
                if (copy_meshes)
                    csg::copy_csgrange_deep(csgrange, std::back_inserter(m_csgmesh));
                else
                    csg::copy_csgrange_shallow(csgrange, std::back_inserter(m_csgmesh));

                result.reset();
            }
        }*/

    // Inform the MeshClipper about the transformation that transforms the mesh
    // into world coordinates.
    void set_transformation(const Domain::Transformation& trafo);

    // Returns index of the contour which was clicked, -1 otherwise.
    size_t get_contour_id_from_projection(const Domain::Vec3d& point) const;

    bool has_valid_contour() const;

    int get_number_of_contours() const
    {
        return result ? result->cut_islands.size() : 0;
    }

    std::vector<Domain::Vec3d> point_per_contour() const;

    void update_result();

    struct CutIsland
    {
        indexed_triangle_set model;
        indexed_triangle_set model_expanded;
        Domain::ExPolygon expoly;
        Domain::BoundingBox2crd expoly_bb;
        bool disabled = false;
        size_t hash;
    };

    struct ClipResult
    {
        std::vector<CutIsland> cut_islands;
        Domain::Transform3d trafo; // this rotates the cut into world coords
    };

    std::optional<ClipResult> result;

private:
    void recalculate_triangles();

    Domain::Transformation m_trafo;
    const indexed_triangle_set* m_mesh = nullptr;
    const indexed_triangle_set* m_negative_mesh = nullptr;
    // std::vector<csg::CSGPart> m_csgmesh;

    ClippingPlane m_plane;
    ClippingPlane m_limiting_plane = ClippingPlane::ClipsNothing();
    bool m_fill_cut        = true;
    double m_contour_width = 0.;
};

} // namespace Slic3r::Biz
