#pragma once

#include "Slic3r/App/Plater/ScopedThumbnailSceneCustomizerBase.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::App::Plater {

class ScopedBedThumbnailSceneCustomizer : public ScopedThumbnailSceneCustomizerBase
{
public:
    ScopedBedThumbnailSceneCustomizer(Scene::Scene& scene, const Domain::Project& project, Domain::SelectionId bed_instance_id,
        bool bed_instance_with_error, Scene::CameraProjectionType camera_type);
};

} // namespace Slic3r::App::Plater
