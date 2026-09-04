#pragma once

#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/App/Scene/ClipperPresenterHelper.hpp"

namespace Slic3r::App::Scene {
class ClipperPresenter;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {

class CutPartSelection
{
public:
    CutPartSelection() = default;
    CutPartSelection(
        const Domain::ModelObject* mo,
        const Domain::Transform3d& cut_matrix,
        int instance_idx,
        const Domain::Vec3d& center,
        const Domain::Vec3d& normal,
        Scene::ClipperPresenter* clipper_presenter
    );
    CutPartSelection(const Domain::ModelObject* mo, int instance_idx_in);

    ~CutPartSelection()
    {
        m_model.clear_objects();
    }

    struct Part
    {
        std::shared_ptr<const Domain::TriangleMesh> mesh;
        std::shared_ptr<AABBMesh> aabb_mesh;
        // Part transformation including tarnsformation of volume and instance
        Domain::Transform3d trafo;
        bool selected;
        bool is_modifier;
    };

    Domain::ModelObject* model_object()
    {
        return m_model.objects.front();
    }

    bool valid() const
    {
        return m_valid;
    }

    bool is_one_object() const;

    const std::vector<Part>& parts() const
    {
        return m_parts;
    }

    std::vector<Part>& parts()
    {
        return m_parts;
    }

    std::vector<Biz::Cut::Part> get_cut_parts();

    // swith selected state for each part
    void turn_over_selection();
    void toggle_part(size_t id);

private:
    void add_object(const Domain::ModelObject* object);

private:
    Domain::Model m_model;
    int m_instance_idx;
    std::vector<Part> m_parts;
    bool m_valid = false;
    std::vector<std::pair<std::vector<size_t>, std::vector<size_t>>>
        m_contour_to_parts; // for each contour, there is a vector of parts above and a vector of parts below

    std::map<Scene::MeshClipperContourId, std::pair<size_t, size_t>>
        m_contours; // for each contour, there is a pair of part above and a part below

    Scene::ClipperPresenter* m_clipper_presenter{nullptr};
};
} // namespace Slic3r::App::Plater
