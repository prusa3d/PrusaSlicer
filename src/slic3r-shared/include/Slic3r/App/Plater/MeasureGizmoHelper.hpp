#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/App/Plater/Measure.hpp"
#include "Slic3r/App/Scene/Ray.hpp"

#include <memory>

namespace Slic3r::Domain {
class TriangleMesh;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater::Measure {

struct VolumeCacheItem
{
    Domain::ElementRef ref;
    const Domain::TriangleMesh* mesh{nullptr};
    Domain::Transform3d world_trafo;
    int face_offset{0};
};

struct InstanceCacheItem
{
    Domain::ElementRef ref;
    std::unique_ptr<Measuring> measuring;
    Scene::Node* node{nullptr};
    bool modified{false};
};

struct SceneSelectionCache
{
    Domain::ElementRefs volume_ids;
    std::vector<VolumeCacheItem> volumes;
    std::vector<InstanceCacheItem> instances;

    void reset()
    {
        volume_ids.clear();
        volumes.clear();
        instances.clear();
    }
};

enum class MeasureGizmoElementType : int8_t
{
    Undefined,
    Features,
    CurrentFeature,
    FirstSelectedFeature,
    SecondSelectedFeature,
    AuxiliaryFeature,
    FirstCircleCenterFeature,
    SecondCircleCenterFeature,
    Dimensionings,
    DimensioningLinear,
    DimensioningLinearStem,
    DimensioningLinearArrow1,
    DimensioningLinearArrow2,
    DimensioningAngularArc,
    DimensioningAngular,
};

struct MeasureGizmoNodeTag
{
    const MeasureGizmoElementType type{MeasureGizmoElementType::Undefined};
};

struct FeatureItem
{
    Domain::ElementRef ref;
    SurfaceFeature feature;
    std::optional<SurfaceFeature> parent;

    bool is_circle_center() const
    {
        return feature.type() == SurfaceFeatureType::Point
            && parent.has_value()
            && parent->type() == SurfaceFeatureType::Circle
            && feature.point().isApprox(std::get<0>(parent->circle()));
    }
};

enum class HoverID : uint8_t
{
    None,
    FirstSelectedFeature,
    SecondSelectedFeature,
    FirstCircleCenterFeature,
    SecondCircleCenterFeature,
};

struct FeatureCache
{
    std::optional<FeatureItem> current;
    std::array<std::optional<FeatureItem>, 2> selected;
    HoverID hover_id{HoverID::None};

    std::optional<FeatureItem>& first_selected()
    {
        return selected[0];
    }

    const std::optional<FeatureItem>& first_selected() const
    {
        return selected[0];
    }

    std::optional<FeatureItem>& second_selected()
    {
        return selected[1];
    }

    const std::optional<FeatureItem>& second_selected() const
    {
        return selected[1];
    }

    void reset()
    {
        current.reset();
        selected[0].reset();
        selected[1].reset();
        hover_id = HoverID::None;
    }

    bool is_selected_circle_center(size_t id) const
    {
        DEBUG_ASSERT(id < 2);
        return selected[id].has_value() && selected[id]->is_circle_center();
    }
};

struct FeatureDetectionData
{
    const VolumeCacheItem* hovered_volume;
    const InstanceCacheItem* hovered_instance;
    const Scene::Node* node;
    Scene::Ray pick_ray;
    double hit_coord;
};

} // namespace Slic3r::App::Plater::Measure
