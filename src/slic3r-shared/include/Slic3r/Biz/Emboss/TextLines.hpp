#ifndef slic3r_TextLines_hpp_
#define slic3r_TextLines_hpp_

#include <vector>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Emboss/Emboss.hpp" // TextLines
#include "Slic3r/Biz/Emboss/TextPresetManager.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Domain/ModelVolume.hpp" // ModelVolumePtrs
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"

namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz::Emboss {

/// <summary>
/// Creation line without model for backend only
/// </summary>
/// <param name="text_tr">Transformation of text volume inside object (aka inside of instance)</param> 
/// <param name="volumes_to_slice">Vector of volumes to be sliced</param> 
/// <param name="ff"></param> 
/// <param name="fp"></param> 
/// <param name="count_lines">Count lines of embossed text(for veritcal alignment)</param>
/// <param name="line_height_mm_ptr">[output] line height in mm</param>
/// <returns></returns>
TextLines create_text_lines(
    const Domain::Transform3d &text_tr,
    const Domain::ModelVolumePtrs &volumes_to_slice,
    const Domain::FontFile &ff,
    const Domain::FontProp &fp,
    unsigned count_lines = 1,
    double *line_height_mm_ptr = nullptr
);

/// <summary>
/// Keep text lines and creation of Scene node together for use with frontend
/// </summary>
class TextLinesModel {
    TextPresetManager& m_preset_manager;
    Biz::ProjectInteractor& m_project_interactor; // current selection
    App::Plater::PlaterScenePresenter& m_scene_presenter; // ability to append node with text lines preview
    App::Render::Device& m_device; // to create geometry from triangles
    
    App::Render::Material m_material;

    struct ProjectContext
    {
        TextLines lines;
        std::unique_ptr<App::Render::Geometry> geometry;
        App::Scene::Node* m_text_line_node = nullptr;
    };
    ProjectScoped<ProjectContext> m_proj_ctxs;
public:
    TextLinesModel(TextPresetManager& preset_manager,
        Biz::ProjectInteractor& project_interactor,
        App::Plater::PlaterScenePresenter& scene_presenter,
        App::Render::Device& device);
    const TextLines& get_lines();
    bool exist_lines() const;

    // Create lines from current selected text volume for current cached preset inside scene
    // Text transformation(inside object not world) is set when no text volume exist for selected object
    void create_text_lines(unsigned count_lines = 1, const Domain::Transform3d* text_tr = nullptr);
    void reset();
    void set_visible(bool visible);
};

struct SelectedText {
    const Domain::ModelVolume* volume = nullptr;
    Domain::SelectionId instance_id;
};

SelectedText get_selected_text_volume(
    const Domain::Project& project,
    const Domain::ElementRefs& selected_elements
);
SelectedText get_selected_text_volume(const Biz::ProjectInteractor& project_interactor);
} // namespace Slic3r::Biz::Emboss
#endif // slic3r_TextLines_hpp_