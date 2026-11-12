#pragma once

#include "Slic3r/App/Plater/ScopedThumbnailSceneCustomizerBase.hpp"

namespace Slic3r::App::Plater {

class Scoped3mfThumbnailSceneCustomizer : public ScopedThumbnailSceneCustomizerBase
{
public:
    Scoped3mfThumbnailSceneCustomizer(Scene::Scene& scene, const Domain::Project& project, Scene::CameraProjectionType camera_type);
};

} // namespace Slic3r::App::Plater
