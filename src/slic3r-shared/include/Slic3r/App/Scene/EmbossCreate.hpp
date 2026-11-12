
#pragma once
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Scene/Scene.hpp" // NodePickResults
#include "Slic3r/Biz/Emboss/EmbossJob.hpp" // CreateVolumeParams

namespace Slic3r::App::Scene {
struct TrafoGuess {
    Domain::Transform3d transformation;
    const Domain::ModelInstance* instance;
    Domain::Vec2d bed_coor; // only for object creation, when instance == nullptr
};
/**
@brief Create volume transformation for just added volume by scene view
@param selection Contain instance where to add volume, when emtpy, than guess transformation for new object
@param project Project where is instance
@param scene Define current view into scene to guess where one wants to add volume
@return Transformation onto surface of the object for new added volume
*/
TrafoGuess guess_volume_transformation(
    const Domain::ElementRefs& selection, const Domain::Project& project, const Scene& scene);

} // namespace Slic3r::App::Scene
