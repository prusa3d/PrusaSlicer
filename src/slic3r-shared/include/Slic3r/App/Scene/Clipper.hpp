#pragma once

#include "Slic3r/Biz/Utils/MeshClipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenterHelper.hpp"
#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Domain {
class ModelVolume;
class ModelObject;
class ModelInstance;
class TriangleMesh;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {

class Camera;
struct Ray;

class Clipper
{
public:
    explicit Clipper() {};

    void set_camera(const Camera* camera);
    void set_normal(const Domain::Vec3d& dir);

    double get_position() const
    {
        return m_clp_ratio;
    }

    const Biz::ClippingPlane& get_clipping_plane(bool ignore_hide_clipped = false) const;
    void set_position_by_ratio(double pos, bool keep_normal);
    void set_range_and_pos(const Domain::Vec3d& cpl_normal, double cpl_offset, double pos);
    void set_limiting_plane(const Domain::Vec3d& plane_normal, double plane_offset);
    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);

    int get_number_of_contours() const;
    std::map<MeshClipperContourId, Domain::Vec3d> point_per_contour() const;

    MeshClipperContourId get_mesh_clipper_contour_id_from_projection(
        const Domain::Vec3d& point_in
    ) const;
    bool has_valid_contour() const;

    void update(
        const Domain::ModelObject* selected_object,
        const Domain::ModelInstance* selected_instance,
        double sla_shift                = 0.,
        bool force_clipper_regeneration = false
    );
    void release();
    void recalculate_object_clippers();

    bool unproject_on_cut_plane(
        const Ray& ray,
        Domain::Vec3d& pos_world,
        MeshClipperContourId& contour_id
    );

    const std::vector<Domain::ModelVolume*>& volumes();

    using MeshesWithTransform =
        std::vector<std::pair<std::unique_ptr<Biz::MeshClipper>, Domain::Transformation>>;
    MeshesWithTransform object_clippers;

private:
    std::vector<const Domain::TriangleMesh*> m_old_meshes;
    // std::unique_ptr<MeshClipper> m_supports_clipper;
    // std::unique_ptr<MeshClipper> m_pad_clipper;
    std::unique_ptr<Biz::ClippingPlane> m_clp;
    double m_clp_ratio             = 0.;
    double m_active_inst_bb_radius = 0.;
    bool m_hide_clipped            = true;

    const Camera* m_camera;
    const Domain::ModelObject* m_selected_object{nullptr};
    const Domain::ModelInstance* m_selected_instance{nullptr};
    double m_sla_shift{0.};
    Biz::ClippingPlane m_limiting_plane{Domain::Vec3d::UnitZ(), -Domain::SINKING_Z_THRESHOLD};
};

} // namespace Slic3r::App::Scene
