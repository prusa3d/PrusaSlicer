#include "Slic3r/App/Plater/CutPartSelection.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"

#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

namespace Slic3r::App::Plater {

using namespace Slic3r::Domain;
namespace mv = Biz::Algorithms::ModelVolume;
namespace tm = Biz::Algorithms::TriangleMesh;

CutPartSelection::CutPartSelection(
    const Domain::ModelObject* mo,
    const Domain::Transform3d& cut_matrix,
    int instance_idx_in,
    const Domain::Vec3d& center,
    const Domain::Vec3d& normal,
    Scene::ClipperPresenter* clipper_presenter
) :
    m_instance_idx(instance_idx_in),
    m_clipper_presenter(clipper_presenter)
{
    Biz::Cut cut(mo, instance_idx_in, cut_matrix);
    add_object(cut.perform_with_plane().front());

    const ModelVolumePtrs& volumes = model_object()->volumes;

    // split to parts
    for (int id = int(volumes.size()) - 1; id >= 0; id--)
        if (mv::is_splittable(*volumes[id])
            && volumes[id]->is_model_part()) // we have to split just solid volumes
            mv::split(volumes[id], 1);

    m_parts.clear();
    for (const ModelVolume* volume : volumes) {
        assert(volume != nullptr);
        Domain::Transform3d trafo =
            model_object()->instances[m_instance_idx]->get_matrix() * volume->get_matrix();
        m_parts.emplace_back(
            Part{
                volume->mesh_ptr(),
                std::make_shared<AABBMesh>(volume->mesh().its),
                trafo,
                true,
                !volume->is_model_part()
            }
        );

        // Now check whether this part is below or above the plane.
        Transform3d tr = trafo.inverse();
        Vec3f pos      = (tr * center).cast<float>();
        Vec3f norm     = (tr.linear().inverse().transpose() * normal).cast<float>();
        for (const Vec3f& v : volume->mesh().its.vertices) {
            double p = (v - pos).dot(norm);
            if (std::abs(p) > EPSILON) {
                m_parts.back().selected = p > 0.;
                break;
            }
        }
    }

    // Now go through the contours and create a map from contours to parts.
    for (const auto& [id, pt] : m_clipper_presenter->clipper().point_per_contour()) {
        const Vec3d dir        = (center - pt).dot(normal) * normal;
        const Vec3d contour_pt = dir + pt; // the result is in world coordinates.

        // Now, cast a ray from every contour point and see which volumes of the ones above
        // the plane are hit from the inside.
        for (size_t part_id = 0; part_id < m_parts.size(); ++part_id) {
            const AABBMesh& aabb = *m_parts[part_id].aabb_mesh.get();
            const Transform3d& tr =
                (translation_transform(model_object()->instances[m_instance_idx]->get_offset())
                 * translation_transform(model_object()->volumes[part_id]->get_offset()))
                    .inverse();
            for (double d : {-1., 1.}) {
                const Vec3d dir_mesh     = d * tr.linear().inverse().transpose() * normal;
                const Vec3d src          = tr * (contour_pt + d * 0.01 * normal);
                AABBMesh::hit_result hit = aabb.query_ray_hit(src, dir_mesh);

                if (hit.is_inside()) {
                    // This part belongs to this point.
                    if (d == 1.)
                        m_contours[id].first = part_id;
                    else
                        m_contours[id].second = part_id;
                }
            }
        }
    }

    m_valid = true;
}

CutPartSelection::CutPartSelection(const Domain::ModelObject* object, int instance_idx_in) :
    m_instance_idx(instance_idx_in)
{
    add_object(object);

    m_parts.clear();

    for (const ModelVolume* volume : object->volumes) {
        assert(volume != nullptr);
        Domain::Transform3d trafo =
            model_object()->instances[m_instance_idx]->get_matrix() * volume->get_matrix();
        m_parts.emplace_back(
            Part{
                volume->mesh_ptr(),
                std::make_shared<AABBMesh>(volume->mesh().its),
                trafo,
                true,
                !volume->is_model_part()
            }
        );

        // Now check whether this part is below or above the plane.
        m_parts.back().selected = volume->is_from_upper();
    }

    m_valid = true;
}

bool CutPartSelection::is_one_object() const
{
    // In theory, the implementation could be just this:
    // return m_contour_to_parts.size() == m_ignored_contours.size();
    // However, this would require that the part-contour correspondence works
    // flawlessly. Because it is currently not always so for self-intersecting
    // objects, let's better check the parts itself:
    if (m_parts.size() < 2)
        return true;
    return std::all_of(
        m_parts.begin(),
        m_parts.end(),
        [this](const Part& part)
        { return part.is_modifier || part.selected == m_parts.front().selected; }
    );
}

std::vector<Biz::Cut::Part> CutPartSelection::get_cut_parts()
{
    std::vector<Biz::Cut::Part> parts;

    for (const auto& part : m_parts)
        parts.push_back({part.selected, part.is_modifier});

    return parts;
}

void CutPartSelection::turn_over_selection()
{
    for (auto& part : m_parts) {
        part.selected = !part.selected;
    }
}

void CutPartSelection::toggle_part(size_t id)
{
    m_parts[id].selected = !m_parts[id].selected;

    // And now recalculate the contours which should be ignored.
    std::vector<Scene::MeshClipperContourId> ignored_ids;

    for (const auto& [contour_id, parts] : m_contours) {
        if (m_parts[parts.first].selected == m_parts[parts.second].selected) {
            ignored_ids.emplace_back(contour_id);
        }
    }

    m_clipper_presenter->set_ignored(ignored_ids);
}

void CutPartSelection::add_object(const Domain::ModelObject* object)
{
    m_model = Model();
    m_model.add_object(*object);

    const double sla_shift_z =
        0.; // wxGetApp().plater()->canvas3D()->get_selection().get_first_volume()->get_sla_shift_z();
    if (!is_approx(sla_shift_z, 0.)) {
        Vec3d inst_offset = model_object()->instances[m_instance_idx]->get_offset();
        inst_offset[Z] += sla_shift_z;
        model_object()->instances[m_instance_idx]->set_offset(inst_offset);
    }
}

} // namespace Slic3r::App::Plater
