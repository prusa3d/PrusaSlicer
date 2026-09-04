#pragma once

#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"

namespace Slic3r::App::Scene {

struct ModelGeometryProvider
{
    ModelGeometryProvider(std::string name) :
        geometry_manager{name + "_geometry"},
        triangle_mesh_manager{name + "_mesh"}
    {}

    using GeometryManager     = Render::GeometryManager<AuxiliaryElementId>;
    using TriangleMeshManager = App::Scene::TriangleMeshManager<AuxiliaryElementId>;

    GeometryManager geometry_manager;
    TriangleMeshManager triangle_mesh_manager;
};

struct ISharedModelGeometryProvider
{
    virtual ~ISharedModelGeometryProvider() = default;
    virtual std::shared_ptr<ModelGeometryProvider> shared_model_geometry_provider() = 0;
};

} // namespace Slic3r::App::Scene
