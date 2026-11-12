#pragma once

#include "Slic3r/App/Plater/ScopedThumbnailSceneCustomizerBase.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::App::Plater {

class ScopedGCodeThumbnailSceneCustomizer : public ScopedThumbnailSceneCustomizerBase
{
public:
    ScopedGCodeThumbnailSceneCustomizer(Scene::Scene& scene, const Domain::Project& project, Domain::SelectionId bed_instance_id,
        Scene::CameraProjectionType camera_type);
};

} // namespace Slic3r::App::Plater
