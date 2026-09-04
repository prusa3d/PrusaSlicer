
#include "Slic3r/App/Plater/TextGizmo.hpp"
#include "Slic3r/App/Plater/TextDialog.hpp"
#include <Slic3r/App/Scene/SceneNodeTag.hpp>
#include <Slic3r/App/Scene/EmbossCreate.hpp>

#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Emboss/TextShapeProvider.hpp"

#include <boost/nowide/convert.hpp>

#include <Slic3r/Directories.hpp>
#include <Slic3r/Domain/ModelObject.hpp> // add volume into object
#include <Slic3r/Biz/I18N/I18N.hpp> // translations
#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>
#include <Slic3r/Biz/Emboss/Emboss.hpp> // also copy in libslic3r for SurfaceCut
#include <Slic3r/Biz/Emboss/EmbossJob.hpp> // embossing jobs

using Slic3r::Biz::_u8L;

namespace {
using namespace Slic3r;

// Constants
double MIN_DEPTH  = 1e-3; // minimal embossing depth [in mm]
double MIN_HEIGHT = 1e-3; // minimal Text height [in mm]

struct Scale
{
    std::optional<double> width;
    std::optional<double> height;
    std::optional<double> depth;
    double char_gap = 1.;
    double line_gap = 1.;
};

struct ProjectContext
{
    // Current edited multiline text for emboss
    std::string text;

    // warnings message
    std::string warning_tooltip;

    // when it has value, than lock of the up vector is set
    std::optional<double> up_limit =
        Biz::Emboss::UP_LIMIT; // default: lock up-vector for surface drag

    Scale volume_scale; // setted in function calc_scale()

    bool use_inch = false;
    bool use_deg  = true;

    Domain::ObjectID last_loaded_volume_id; // initial invalid

    // flag wheather surface point lay under the text
    // when no surface point and change 'from surface', relative move without recalculation is made
    bool exist_surface_point;
};

template <typename T>
bool set_opt(std::optional<T>& val_opt, double new_value, double scale)
{
    T scaled_new = static_cast<T>(new_value / scale);
    if (scaled_new == val_opt.value_or(T(0)))
        return false; // no change
    else if (scaled_new == 0)
        val_opt.reset();
    else
        val_opt = scaled_new;
    return true;
}
} // namespace

namespace Slic3r::App::Plater {

namespace {
void set_dialog_rotation(TextDialog& dialog, const Biz::Emboss::TextPresetManager& preset_manager);
void set_dialog_surface_distance(
    TextDialog& dialog,
    const Biz::Emboss::TextPresetManager& preset_manager,
    const std::optional<double>& scale
);

void calc_from_surface(
    std::optional<float>& from_surface,
    bool& exist_surface_point,
    const Domain::Project& project,
    const Domain::ElementRef& ref,
    PlaterScenePresenter& scene_presenter
)
{
    Scene::Node& root = scene_presenter.scene().root();
    auto distance_exp = Biz::Emboss::calc_distance(project, ref, root);
    exist_surface_point =
        distance_exp.has_value() || distance_exp.error() == Biz::Emboss::DistanceIssue::ApproxZero;
    from_surface =
        distance_exp.has_value() ? std::optional<float>{*distance_exp} : std::optional<float>{};
}

void clear_font_cache(Biz::Emboss::TextPresetManager& preset_manager)
{
    Biz::Emboss::FontFileWithCache& ff_with_cache = preset_manager.get_font_file_with_cache();
    if (ff_with_cache.has_value()) {
        ff_with_cache.cache->clear();
    }
};
} // namespace

struct TextGizmo::ProjectContext : public ::ProjectContext
{};

TextGizmo::TextGizmo(
    Render::Device& device,
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Biz::Emboss::IFontManager& font_manager,
    Scene::IGizmoController& gizmo_controller
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_font_manager(font_manager),
    m_gizmo_controller(gizmo_controller),
    m_preset_manager(
        font_manager,
        Slic3r::data_dir() + "/text_emboss_presets.cereal",
        project_interactor
    ),
    m_surface_drag(scene_presenter, project_interactor),
    m_text_lines(m_preset_manager, project_interactor, scene_presenter, device),
    m_proj_ctxs(std::make_unique<Biz::ProjectScoped<ProjectContext>>(project_interactor)),
    m_dialog(std::make_unique<TextDialog>())
{
    // Initialize font descriptor to font copied with application
    m_preset_manager.get_preset().emboss_style.descriptor = Domain::FontDescriptor{
        .name = "Prusa-slic3r font",
        .path = Slic3r::resources_dir() + "/fonts/NotoSans-Regular.ttf",
        .type = Domain::FontDescriptor::Type::file_path
    };

    // Dialog callback settings (order follow UI)
    m_dialog->callbacks().text_changed = [this](const std::string& text)
    {
        std::string& current_text = m_proj_ctxs->selected().text;
        if (m_preset_manager.get_font_prop().per_glyph) {
            // update text lines when change count of lines
            unsigned prev_count_lines = Biz::Emboss::get_count_lines(current_text);
            unsigned now_count_lines  = Biz::Emboss::get_count_lines(text);
            if (prev_count_lines != now_count_lines)
                m_text_lines.create_text_lines(now_count_lines);
        }
        m_dialog->set_enable_line_gap(Biz::Emboss::get_count_lines(text) > 1);
        current_text = text;
        update_volume();
    };
    m_dialog->callbacks().font_selection_changed =
        [this](const Domain::FontDescriptor& font_descriptor)
    {
        m_preset_manager.set_font(font_descriptor);
        update_volume();
    };
    // NOTE: style is only subcategory of font
    m_dialog->callbacks().height_changed = [this](double value)
    {
        if (value <= MIN_HEIGHT) {
            value = MIN_HEIGHT;
            if (Domain::is_approx(m_preset_manager.get_font_prop().size_in_mm, (float) value))
                return; // do not update already min value
        }

        if (const std::optional<double>& scale = m_proj_ctxs->selected().volume_scale.height;
            scale.has_value())
            value /= *scale;
        m_preset_manager.get_font_prop().size_in_mm = static_cast<float>(value);
        if (m_preset_manager.get_font_prop().per_glyph) // change size of line visualization
            m_text_lines.create_text_lines(m_text_lines.get_lines().size());
        update_volume();
    };
    m_dialog->callbacks().depth_changed = [this](double value)
    {
        if (value <= MIN_DEPTH) {
            value = MIN_DEPTH;
            if (Domain::is_approx(m_preset_manager.get_preset().projection.depth, value))
                return; // do not update already min value
        }

        const std::optional<double>& scale = m_proj_ctxs->selected().volume_scale.height;
        if (scale.has_value())
            value /= *scale;
        m_preset_manager.get_preset().projection.depth = value;
        // change from surface limits
        set_dialog_surface_distance(dialog(), m_preset_manager, scale);
        update_volume();
    };

    // Advanced settings
    m_dialog->callbacks().use_surface_checked = [this](bool check)
    {
        m_preset_manager.get_preset().projection.use_surface = check;
        if (check) {
            m_preset_manager.get_preset().distance.reset();
            m_dialog->set_enable_surface_distance(false);
        }
        update_volume();
    };
    m_dialog->callbacks().per_glyph_checked = [this](bool check)
    {
        m_preset_manager.get_font_prop().per_glyph = check;
        if (check) {
            const std::string& text = m_proj_ctxs->selected().text;
            m_text_lines.create_text_lines(Biz::Emboss::get_count_lines(text));
        } else {
            m_text_lines.reset();
        }
        update_volume();
    };
    m_dialog->callbacks().align_changed = [this](const Domain::FontProp::Align& align)
    {
        m_preset_manager.get_font_prop().align = align;
        if (m_preset_manager.get_font_prop().per_glyph) // change position of the line visualization
            m_text_lines.create_text_lines(m_text_lines.get_lines().size());
        update_volume();
    };
    auto set_optional = [this](std::optional<int>& val_opt, double new_value, double scale)
    {
        if (set_opt(val_opt, new_value, scale)) {
            clear_font_cache(m_preset_manager);
            update_volume();
        }
    };
    m_dialog->callbacks().char_gap_changed = [this, set_optional](double value)
    {
        set_optional(
            m_preset_manager.get_font_prop().char_gap,
            value,
            m_proj_ctxs->selected().volume_scale.char_gap
        );
    };
    m_dialog->callbacks().line_gap_changed = [this, set_optional](double value)
    {
        set_optional(
            m_preset_manager.get_font_prop().line_gap,
            value,
            m_proj_ctxs->selected().volume_scale.line_gap
        );
    };
    auto set_optional_f = [this](std::optional<float>& val_opt, double new_value, double scale)
    {
        if (set_opt(val_opt, new_value, scale)) {
            clear_font_cache(m_preset_manager);
            update_volume();
        }
    };
    m_dialog->callbacks().boldness_changed = [this, set_optional_f](double value)
    {
        set_optional_f(
            m_preset_manager.get_font_prop().boldness,
            value,
            m_proj_ctxs->selected().volume_scale.char_gap
        );
    };
    m_dialog->callbacks().skew_ratio_changed = [this, set_optional_f](double value)
    {
        set_optional_f(m_preset_manager.get_font_prop().skew, value, 1.); // no scale
    };
    m_dialog->callbacks().surface_distance_changed = [this](double distance_in_mm)
    {
        ProjectContext& proj_ctx  = m_proj_ctxs->selected();
        bool& exist_surface_point = proj_ctx.exist_surface_point;
        if (!exist_surface_point)
            return;

        std::optional<float>& distance = m_preset_manager.get_preset().distance;
        double diff                    = distance_in_mm - distance.value_or(0.f);
        if (Domain::is_approx(diff, 0., 1e-3))
            return; // no change

        if (const std::optional<double>& scale = proj_ctx.volume_scale.depth; scale.has_value())
            diff = diff / (*scale);

        Domain::Transform3d relative_volume_tr{Eigen::Translation3d(Domain::Vec3d(0., 0., diff))};
        Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);

        // calculate current surface distance
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ElementRef& ref =
            m_project_interactor.scene_interactor().object_selection().elements.front();
        calc_from_surface(distance, exist_surface_point, project, ref, m_scene_presenter);
        if (m_preset_manager.get_font_prop().per_glyph) {
            // Slice of object(text lines) are relative to text volume position so need recalculate
            m_text_lines.create_text_lines(m_text_lines.get_lines().size());
            update_volume(); // change position of projected letters
        }
        // ASSERT(Domain::is_approx(distance.value_or(0.f), (float)value, 1e-3f));
    };
    m_dialog->callbacks().rotation_changed = [this](double angle_in_rad) { rotate(angle_in_rad); };
    m_dialog->callbacks().unlock_rotation  = [this](bool check)
    {
        auto& up_limit = m_proj_ctxs->selected().up_limit;
        if (check) {
            up_limit.reset();
        } else { // Limit direction of the up vector on the model, between side and top surface
            up_limit = Biz::Emboss::UP_LIMIT;
        }
    };
    m_dialog->callbacks().set_on_face_camera = [this]()
    {
        const Scene::Camera& camera = m_scene_presenter.scene().camera();
        Domain::Vec3d wanted_dir    = -camera.forward();

        const Domain::Project& project                = m_project_interactor.selected_project();
        Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
        const Domain::ElementRef& ref = scene_interactor.object_selection().elements.front();

        const Domain::ModelInstance& instance =
            *project.find_instance_by_id(ref.object_id, ref.instance_id);
        const Domain::ModelVolume& volume         = (ref.volume_id != 0) ?
            *project.find_volume_by_id(ref.object_id, ref.volume_id) :
            *project.find_object_by_id(ref.object_id)->volumes.front();
        Domain::Transform3d to_world              = instance.get_matrix() * volume.get_matrix();
        const Domain::Transform3d& instance_tr    = instance.get_matrix();
        const Domain::Transform3d instance_tr_inv = instance_tr.inverse();
        const Domain::Transform3d& volume_tr_inv  = volume.get_matrix().inverse();

        Domain::Vec3d world_position                         = to_world.translation();
        const Biz::Emboss::TextPresetManager::Preset& preset = m_preset_manager.get_preset();
        const ProjectContext& proj_ctx                       = m_proj_ctxs->selected();
        const auto& up_limit                                 = proj_ctx.up_limit;
        Domain::Transform3d new_volume_tr = Biz::Emboss::get_volume_transformation(
            to_world,
            wanted_dir,
            world_position,
            instance_tr_inv,
            preset.angle,
            preset.distance,
            up_limit
        );
        Domain::Transform3d volume_relative =
            instance_tr * new_volume_tr * volume_tr_inv * instance_tr_inv;
        scene_interactor.transform_selection(volume_relative.matrix());

        if (!up_limit.has_value()) { // recalculate angle when not locked
            m_preset_manager.get_preset().angle = Biz::Emboss::calc_rotation(project, ref);
            set_dialog_rotation(dialog(), m_preset_manager);
        }

        if (m_preset_manager.get_font_prop().per_glyph) // change position of the text line
            m_text_lines.create_text_lines(m_text_lines.get_lines().size());

        // update shape when needed
        if (m_preset_manager.get_preset().projection.use_surface
            || m_preset_manager.get_font_prop().per_glyph)
            update_volume();
    };

    // Presets
    m_dialog->callbacks().preset_selection_changed = [this](int id)
    {
        m_preset_manager.load_preset(static_cast<size_t>(id));
        if (m_preset_manager.get_font_prop().per_glyph) { // new preset contain per_glyph
            const std::string& text = m_proj_ctxs->selected().text;
            m_text_lines.create_text_lines(Biz::Emboss::get_count_lines(text));
        }
        update_volume();
    };

    m_dialog->callbacks().save_preset_as = [this]()
    {
        m_preset_manager.save_preset_as();
        update_volume(); // write preset name into volume
        // resert revert buttons + add new preset into selection
    };
    m_dialog->callbacks().save_preset = [this]()
    {
        m_preset_manager.store_presets();
        // ReSet default values for input to remove revert buttons
        m_proj_ctxs->selected().last_loaded_volume_id =
            Domain::ObjectID{}; // to force reload default values
        Domain::SelectionId project_id = m_project_interactor.selected_project_id();
        const Biz::Scene::SceneInteractor& scene_interactor =
            m_project_interactor.scene_interactor();
        on_scene_selection_changed(project_id, scene_interactor.object_selection());
    };
    m_dialog->callbacks().rename_preset = [this]()
    {
        m_preset_manager.rename_preset();
        update_volume(); // write new preset name into volume
    };
    m_dialog->callbacks().delete_preset = [this]()
    {
        if (m_preset_manager.delete_preset()) {
            m_dialog->set_presets(
                m_preset_manager.get_presets_names(),
                m_preset_manager.get_preset_index()
            );
            if (m_preset_manager.get_font_prop().per_glyph) { // new preset contain per_glyph
                const std::string& text = m_proj_ctxs->selected().text;
                m_text_lines.create_text_lines(Biz::Emboss::get_count_lines(text));
            }
            update_volume();
        }
    };
    m_dialog->callbacks().operation_selection_changed = [this](Domain::ModelVolumeType type)
    { update_volume(type); };
}

TextGizmo::~TextGizmo() = default;

Scene::ToolType TextGizmo::type() const
{
    return Scene::ToolType::TextGizmo;
}

GizmoWindowPtr TextGizmo::release_ui_window()
{
    return m_dialog.release();
}

bool TextGizmo::enabled() const
{
    return Biz::Emboss::get_selected_instance(m_project_interactor) != nullptr;
};

Scene::GizmoActivationState TextGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    // using App::Platform::MouseButton;
    // using App::Platform::MouseEvent;
    // const MouseEvent& mouse_event = ctx.mouse_event();
    // if (mouse_event.type() == MouseEvent::Type::ButtonDown
    // && mouse_event.button() == MouseButton::Right)
    //{
    // Domain::ModelVolumeType type = Domain::ModelVolumeType::NEGATIVE_VOLUME;
    // if(emboss_text(type, ctx.pick_ray(), ctx.pick_results()))
    // return Scene::GizmoActivationState::Active; // create volume at pick ray
    //}
    return Scene::GizmoActivationState::Inactive;
}

bool TextGizmo::on_drag_start(const Scene::GizmoEventContext& ctx)
{
    const Biz::Emboss::TextPresetManager::Preset& preset = m_preset_manager.get_preset();
    std::optional<float> distance                        = preset.distance;
    if (const std::optional<double>& d_scale = m_proj_ctxs->selected().volume_scale.depth;
        distance.has_value() && d_scale.has_value())
        distance = static_cast<float>((*distance) / (*d_scale));
    return m_surface_drag.on_drag_start(ctx, distance);
}

namespace {
void activate_preset(
    TextDialog& dialog,
    const Biz::Emboss::TextPresetManager& preset_manager,
    const ProjectContext& proj_ctx,
    const Domain::ModelVolume& volume
);
} // namespace

bool TextGizmo::on_dragging(const Scene::GizmoEventContext& ctx)
{
    const Biz::Emboss::TextPresetManager::Preset& preset = m_preset_manager.get_preset();
    const ProjectContext& proj_ctx                       = m_proj_ctxs->selected();
    const auto& up_limit                                 = proj_ctx.up_limit;
    std::optional<float> distance                        = preset.distance;
    if (const std::optional<double> d_scale = proj_ctx.volume_scale.depth;
        distance.has_value() && d_scale.has_value())
        distance = static_cast<float>((*distance) / (*d_scale));
    if (!m_surface_drag.on_dragging(ctx, preset.angle, distance, up_limit))
        return false;

    if (!m_surface_drag.is_dragging())
        return true; // out of surface but still dragging

    if (!up_limit.has_value()) { // recalculate angle when not locked
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ElementRef& element =
            m_project_interactor.scene_interactor().object_selection().elements.front();

        m_preset_manager.get_preset().angle = Biz::Emboss::calc_rotation(project, element);
        set_dialog_rotation(dialog(), m_preset_manager);
    }

    if (m_preset_manager.get_font_prop().per_glyph) // recalculate lines
        m_text_lines.create_text_lines(m_text_lines.get_lines().size());

    if (!proj_ctx.exist_surface_point) {
        m_proj_ctxs->selected().exist_surface_point = true;
        // enable from surface distance in dialog
        const Domain::ModelVolume* volume_ptr =
            Biz::Emboss::get_selected_text_volume(
                m_project_interactor.selected_project(),
                m_project_interactor.scene_interactor().object_selection().elements
            )
                .volume;
        ASSERT(volume_ptr != nullptr);
        activate_preset(dialog(), m_preset_manager, proj_ctx, *volume_ptr);
    }
    return true;
}

void TextGizmo::on_drag_finish()
{
    m_surface_drag.on_drag_finish();
    if (m_preset_manager.get_preset().projection.use_surface
        || m_preset_manager.get_font_prop().per_glyph)
        update_volume();
}

void TextGizmo::on_drag_cancel()
{
    m_surface_drag.on_drag_cancel();
}

void TextGizmo::render_imgui()
{
    m_surface_drag.imgui_draw(); // cross hair during drag
}

void TextGizmo::on_thumbnail_render_begin()
{
    m_text_lines.set_visible(false);
}

void TextGizmo::on_thumbnail_render_end()
{
    m_text_lines.set_visible(true);
}

void TextGizmo::on_activated()
{
    m_preset_manager.init();
    ProjectContext& proj_ctx = m_proj_ctxs->selected();

    // use_inch = wxGetApp().app_config->get_bool("use_inches");
    m_dialog->update_units(proj_ctx.use_inch);

    // Re-load installed fonts
    // NOTE: Version 2.9.2 do it on dialog open, now it is on gizmo activation
    const Domain::FontList& fonts = m_font_manager.get_fonts();
    m_dialog->set_fonts(fonts);
    if (m_preset_manager.exist_stored_style())
        m_dialog->set_font(m_preset_manager.get_stored_preset()->emboss_style.descriptor, true);
    m_dialog->set_font(m_preset_manager.get_preset().emboss_style.descriptor, false);

    // Register for scene changes
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_presenter.scene().add_listener<Scene::IThumbnailRenderListener>(this);

    // when text volume is not selected, create new one
    if (Biz::Emboss::get_selected_text_volume(m_project_interactor).volume == nullptr) {
        // What shows few miliseconds till new item is created?
        m_dialog->set_enable_all_except_font(true);
        if (m_next_volume_type) {
            add_text_to_scene(*m_next_volume_type);
            m_next_volume_type = std::nullopt;
        } else {
            add_text_to_scene();
        }
        // after create it is called function on_scene_selection_changed()
        return;
    }

    // set current state of scene
    Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    on_scene_selection_changed(project_id, scene_interactor.object_selection());
}

void TextGizmo::on_deactivated()
{
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_presenter.scene().remove_listener<Scene::IThumbnailRenderListener>(this);
    m_text_lines.reset(); // remove scene node
    m_proj_ctxs->selected().last_loaded_volume_id = Domain::ObjectID{}; // invalid
}

void TextGizmo::on_project_deactivated(size_t old_project_id)
{
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_scene_presenter.scene().remove_listener<Scene::IThumbnailRenderListener>(this);
}

bool TextGizmo::allows_activation_by_double_click(const Scene::GizmoEventContext& ctx)
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.elements.size() != 1)
        return false; // allow only when text is already selected

    const Domain::ElementRef& selected = selection.elements.front();

    // is double click on selected text volume?
    const Domain::Project& project = m_project_interactor.selected_project();
    for (const App::Scene::NodePickResult& pick : ctx.pick_results()) {
        if (!pick.node->has_tag_of_type<Scene::SceneNodeTag>())
            continue; // ignore staff(node) infront of text volume
        auto* tag = pick.node->tag_of_type<Scene::SceneNodeTag>();
        if (tag == nullptr)
            continue;
        const Domain::ModelVolume* volume_ptr =
            project.find_volume_by_id(tag->object_id, tag->volume_id);
        if (volume_ptr == nullptr)
            continue; // weird situation
        const Domain::ModelVolume& volume = *volume_ptr;
        if (!volume.text_configuration.has_value())
            break; // it is not text, check only first volume

        if (selected.volume_id != tag->volume_id
            || selected.object_id != tag->object_id
            || selected.instance_id != tag->instance_id)
            break; // double click is on other volume, not selected one

        // double click is on selected text volume, allow activation
        return true;
    }
    return false;
}

namespace {
void set_dialog_rotation(TextDialog& dialog, const Biz::Emboss::TextPresetManager& preset_manager)
{
    bool exist_stored                                    = preset_manager.exist_stored_style();
    const Biz::Emboss::TextPresetManager::Preset& preset = preset_manager.get_preset();
    const Biz::Emboss::TextPresetManager::Preset& preset_ =
        exist_stored ? *preset_manager.get_stored_preset() : preset;
    dialog.set_rotation(preset.angle, preset_.angle);
}

void set_dialog_surface_distance(
    TextDialog& dialog,
    const Biz::Emboss::TextPresetManager& preset_manager,
    const std::optional<double>& scale
)
{
    bool exist_stored                                    = preset_manager.exist_stored_style();
    const Biz::Emboss::TextPresetManager::Preset& preset = preset_manager.get_preset();
    const Biz::Emboss::TextPresetManager::Preset& preset_ =
        exist_stored ? *preset_manager.get_stored_preset() : preset;
    double surface_distance  = preset.distance.value_or(0.f);
    double surface_distance_ = preset_.distance.value_or(0.f);
    double max_distance      = 2 * preset.projection.depth * scale.value_or(1.);
    dialog.set_surface_distance(max_distance, surface_distance, surface_distance_);
}

void activate_preset(
    TextDialog& dialog,
    const Biz::Emboss::TextPresetManager& preset_manager,
    const ProjectContext& proj_ctx,
    const Domain::ModelVolume& volume
)
{
    // disable callback till new values are set
    TextDialog::Callbacks temp_callbacks = std::move(dialog.callbacks());
    dialog.callbacks()                   = TextDialog::Callbacks{};
    ScopeGuard sg_callbacks([&dialog, &temp_callbacks]()
                            { dialog.callbacks() = std::move(temp_callbacks); });
    dialog.update_units(proj_ctx.use_inch);
    dialog.update_angle(!proj_ctx.use_deg);
    dialog.set_warning(proj_ctx.warning_tooltip);

    bool exist_stored                                    = preset_manager.exist_stored_style();
    const Biz::Emboss::TextPresetManager::Preset& preset = preset_manager.get_preset();
    const Biz::Emboss::TextPresetManager::Preset& preset_ =
        exist_stored ? *preset_manager.get_stored_preset() : preset;
    // NOTE: _ (suffix) means stored preset in this function

    bool use_surface = preset.projection.use_surface;
    bool is_part     = volume.get_object()->volumes.size() != 1;

    // Update dialog data
    dialog.set_editor(proj_ctx.text);

    // TODO: solve conversion from font name
    const Domain::EmbossStyle& es  = preset.emboss_style;
    const Domain::EmbossStyle& es_ = preset_.emboss_style;
    if (exist_stored)
        dialog.set_font(es_.descriptor, true);
    dialog.set_font(es.descriptor, false);

    const Domain::FontProp& prop  = es.prop;
    const Domain::FontProp& prop_ = es_.prop;
    double height                 = prop.size_in_mm * proj_ctx.volume_scale.height.value_or(1.);
    double height_default         = prop_.size_in_mm;
    dialog.set_text_height(height, height_default);

    const Domain::EmbossProjection& ep  = preset.projection;
    const Domain::EmbossProjection& ep_ = preset_.projection;
    double depth                        = ep.depth * proj_ctx.volume_scale.depth.value_or(1.);
    double depth_default                = ep_.depth;
    dialog.set_depth(depth, depth_default);

    // advanced
    dialog.set_enable_use_surface(is_part);
    dialog.set_use_surface(ep.use_surface, ep_.use_surface);

    dialog.set_enable_per_glyph(is_part);
    dialog.set_per_glyph(prop.per_glyph, prop_.per_glyph);

    dialog.set_align(prop.align, prop_.align);

    double scale_char_gap  = proj_ctx.volume_scale.char_gap;
    double char_gap_in_mm  = prop.char_gap.value_or(0) * scale_char_gap;
    double char_gap_in_mm_ = prop_.char_gap.value_or(0);
    dialog.set_char_gap(char_gap_in_mm, char_gap_in_mm_);

    bool is_multiline = Biz::Emboss::get_count_lines(proj_ctx.text) > 1;
    dialog.set_enable_line_gap(is_multiline);
    double line_gap_in_mm  = prop.line_gap.value_or(0) * proj_ctx.volume_scale.line_gap;
    double line_gap_in_mm_ = prop_.line_gap.value_or(0);
    dialog.set_line_gap(line_gap_in_mm, line_gap_in_mm_);

    double boldness_in_mm  = prop.boldness.value_or(0) * scale_char_gap;
    double boldness_in_mm_ = prop_.boldness.value_or(0);
    dialog.set_boldness(boldness_in_mm, boldness_in_mm_);

    double skew_ratio  = prop.skew.value_or(0.f);
    double skew_ratio_ = prop_.skew.value_or(0.f);
    dialog.set_skew_ratio(skew_ratio, skew_ratio_);

    dialog.set_enable_surface_distance(is_part && !use_surface && proj_ctx.exist_surface_point);
    set_dialog_surface_distance(dialog, preset_manager, proj_ctx.volume_scale.depth);

    bool rotation_lock = !proj_ctx.up_limit.has_value();
    dialog.set_rotation_lock(rotation_lock);
    set_dialog_rotation(dialog, preset_manager);

    // NOTE: not neccessary to write pressets names every time when volume loads
    dialog.set_presets(preset_manager.get_presets_names(), preset_manager.get_preset_index());

    dialog.show_part_specific_panel(is_part);
    if (is_part) {
        dialog.set_operation(volume.type());
    }
}

// True when exist change in scale otherwise false
bool calc_scales(
    Scale& volume_scale,
    const Domain::Project& project,
    const Domain::ElementRef& ref,
    Biz::Emboss::TextPresetManager& preset_manager
)
{
    Domain::Transform3d to_world = Biz::Emboss::world_tr(project, ref);
    auto to_world_linear         = to_world.linear();
    auto calc = [&to_world_linear](const Domain::Vec3d& axe, std::optional<double>& scale)
    {
        Domain::Vec3d axe_world = to_world_linear * axe;
        if (double norm_sq = axe_world.squaredNorm(); Domain::is_approx(norm_sq, 1.)) {
            if (scale.has_value())
                scale.reset();
            else
                return false;
        } else {
            scale = sqrt(norm_sq);
        }
        return true;
    };

    bool exist_change = calc(Domain::Vec3d::UnitY(), volume_scale.height);
    exist_change |= calc(Domain::Vec3d::UnitX(), volume_scale.width);
    exist_change |= calc(Domain::Vec3d::UnitZ(), volume_scale.depth);

    auto font_point_to_world = [&preset_manager](const std::optional<double>& scale) -> double
    {
        const Domain::FontFile& ff =
            *preset_manager.get_font_file_with_cache().font_file; /* not const */
        const Domain::FontProp& fp              = preset_manager.get_font_prop();
        const Domain::FontFile::Info& font_info = Biz::Emboss::get_font_info(ff, fp);
        double font_point_to_volume_mm          = fp.size_in_mm / (double) font_info.unit_per_em;
        return font_point_to_volume_mm * scale.value_or(1.); // font_point_to_world_mm
    };

    // TODO: solve first initialization and than recaluculate only when exist change
    volume_scale.char_gap = font_point_to_world(volume_scale.width);
    volume_scale.line_gap = font_point_to_world(volume_scale.height);

    return exist_change;
}
} // namespace

void TextGizmo::on_volume_mesh_changed(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& volumes
)
{
    update_from_selected_elements(project_id, volumes);
}

void TextGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    update_from_selected_elements(project_id, selection.elements);
}

void TextGizmo::update_from_selected_elements(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& selected_elements
)
{
    const Domain::Project& project     = m_project_interactor.project(project_id);
    Biz::Emboss::SelectedText selected = Biz::Emboss::get_selected_text_volume(project, selected_elements);
    if (selected.volume == nullptr)
        return close(); // unselection text volume

    const Domain::ModelVolume& volume = *selected.volume;
    ProjectContext& proj_ctx          = m_proj_ctxs->project(project_id);
    if (proj_ctx.last_loaded_volume_id == volume.id())
        return; // already loaded
    proj_ctx.last_loaded_volume_id = volume.id();

    // remove previous warnings
    proj_ctx.warning_tooltip.clear();
    ASSERT(volume.text_configuration.has_value());
    const Domain::TextConfiguration& tc = *volume.text_configuration;
    proj_ctx.text                       = tc.text;

    // load current settings
    const Domain::ElementRef& ref = selected_elements.front();
    Biz::Emboss::TextPresetManager::Preset preset{
        .emboss_style = tc.style, // copy
        .projection   = volume.emboss_shape->projection, // copy
        .angle        = Biz::Emboss::calc_rotation(project, ref)
    };

    const auto& presets = m_preset_manager.get_presets();

    // use one of the current font descriptor
    Domain::FontDescriptor& fd = preset.emboss_style.descriptor;
    auto descriptor            = m_font_manager.get_current_descriptor(fd);
    if (!descriptor.has_value()) {
        // Inform user about using unavailable font
        switch (descriptor.error()) {
        case Biz::Emboss::IFontManager::ConversionError::AnotherOS:
            proj_ctx.warning_tooltip =
                _u8L("The font was created on another platform and cannot be used here.");
            break;
        case Biz::Emboss::IFontManager::ConversionError::NotOkWxFont:
            proj_ctx.warning_tooltip = _u8L("The font could not be loaded.");
            break;
        default:
            PANIC("Unhandled ConversionError value. A new enum case may have been added.");
            break;
        }
        if (!proj_ctx.warning_tooltip.empty()) {
            proj_ctx.warning_tooltip +=
                "\n" + _u8L("The first available preset will be used instead.");
        }
        ASSERT(!presets.empty());
        preset = presets.front();
    } else if (fd.path != descriptor->path) {
        // inform user about using different font descriptor
        proj_ctx.warning_tooltip = _u8L("Autofixed font descriptor");
        fd.path                  = descriptor->path;
    }

    bool is_part = volume.get_object()->volumes.size() != 1;
    if (!is_part) { // is object
        // Appear after delete object ot the previously added text volume part
        if (volume.text_configuration->style.prop.per_glyph)
            preset.emboss_style.prop.per_glyph = false;
        if (volume.emboss_shape->projection.use_surface)
            preset.projection.use_surface = false;
    }

    auto preset_it = std::find_if(
        presets.begin(),
        presets.end(),
        [&name = fd.name](const Biz::Emboss::TextPresetManager::Preset& preset_)
        { return preset_.emboss_style.descriptor.name == name; }
    );

    if (preset_it == presets.end()) {
        // unknown preset inside volume, create temporary one
        m_preset_manager.load_preset(preset);
    } else {
        m_preset_manager.load_preset(presets.begin() - preset_it);
        m_preset_manager.get_preset() = preset;
    }

    if (m_preset_manager.get_font_prop().per_glyph) {
        unsigned count_lines = Biz::Emboss::get_count_lines(proj_ctx.text);
        m_text_lines.create_text_lines(count_lines); // create current text lines on text change
    } else {
        m_text_lines.reset(); // remove previous text lines
    }

    calc_scales(
        proj_ctx.volume_scale,
        project,
        ref,
        m_preset_manager
    ); // volume scale for each axis
    if (is_part) {
        calc_from_surface(
            m_preset_manager.get_preset().distance,
            proj_ctx.exist_surface_point,
            project,
            ref,
            m_scene_presenter
        );
    }
    activate_preset(dialog(), m_preset_manager, proj_ctx, volume);
}

void TextGizmo::set_next_volume_type(Domain::ModelVolumeType volume_type)
{
    m_next_volume_type = volume_type;
}

void TextGizmo::on_project_activated(size_t new_project_id)
{
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_scene_presenter.scene().add_listener<Scene::IThumbnailRenderListener>(this);

    // fill dialog with current data
    const Domain::Project& project = m_project_interactor.project(new_project_id);
    const Domain::ModelVolume* volume_ptr =
        Biz::Emboss::get_selected_text_volume(project, scene_interactor.object_selection().elements).volume;
    ASSERT(volume_ptr != nullptr);
    activate_preset(dialog(), m_preset_manager, m_proj_ctxs->project(new_project_id), *volume_ptr);
}

namespace {

bool is_set_volume_name(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::ModelVolume* volume_ptr =
        Biz::Emboss::get_selected_text_volume(project_interactor).volume;
    if (volume_ptr == nullptr)
        return true; // creation of the new volume
    ASSERT(volume_ptr->is_text());
    return volume_ptr->name == volume_ptr->text_configuration->text;
}

Biz::Emboss::BaseData create_base_data(
    const std::string& text,
    Domain::ModelVolumeType volume_type,
    Biz::Emboss::TextPresetManager& preset_manager,
    Biz::ProjectInteractor& project_interactor,
    const Biz::Emboss::TextLines& text_lines,
    Biz::Emboss::BaseData::IssueFn issue_fn,
    std::optional<double> depth_scale = {}
)
{
    const Biz::Emboss::TextPresetManager::Preset& preset = preset_manager.get_preset();
    Domain::TextConfiguration text_config{.style = preset.emboss_style, .text = text};
    Biz::Emboss::FontFileWithCache& font_file = preset_manager.get_font_file_with_cache();
    std::optional<float> from_surface         = preset.distance;
    if (from_surface.has_value() && depth_scale.has_value())
        from_surface = (*from_surface) / (*depth_scale);
    return Biz::Emboss::BaseData{
        .tri_mesh = {
            .shape_provider = std::make_unique<Biz::Emboss::TextShapeProvider>(
                std::move(text_config),
                preset.projection,
                text_lines,
                font_file
            ),
            .is_outside = (volume_type == Domain::ModelVolumeType::MODEL_PART),
            .per_glyph_surface_distance = from_surface,
        },
        .project_interactor = project_interactor,
        .project_id = project_interactor.selected_project_id(),
        .volume_name                = is_set_volume_name(project_interactor) ? text : std::string{},
        .issue_fn                   = std::move(issue_fn)
    };
}

bool is_text_empty(std::string_view text)
{
    return text.empty() || text.find_first_not_of(" \n\t\r") == std::string::npos;
}

Biz::Emboss::BaseData::IssueFn create_issue_fn(
    TextDialog& dialog,
    std::string& warning_tooltip,
    const Biz::ProjectInteractor& project_interactor
)
{
    auto prepend_tooltip =
        [&dialog,
         &warning_tooltip,
         &project_interactor,
         project_id = project_interactor.selected_project_id()](const std::string& message)
    {
        std::string prev_tooltip =
            (warning_tooltip.empty()) ? std::string() : ("\n" + warning_tooltip);
        warning_tooltip = message + prev_tooltip;
        if (project_id == project_interactor.selected_project_id())
            dialog.set_warning(warning_tooltip);
    };
    return [prepend_tooltip](Biz::Emboss::JobIssue issue)
    {
        using namespace Slic3r::Biz::Emboss; // JobIssue
        switch (issue) {
        case JobIssue::no_shape:
            prepend_tooltip(_u8L(
                "Current text input with selected font does not create any shape. Change font or text."
            ));
            break;
        case JobIssue::no_surface:
            prepend_tooltip(_u8L(
                "There is no surface to emboss the shape on. Move the text to a suitable surface."
            ));
            break;
        case JobIssue::default_volume:
            prepend_tooltip(
                _u8L("Default object volume was applied. Please change the font or text.")
            );
            break;
        case JobIssue::canceled:
            prepend_tooltip(_u8L("Job was canceled."));
            break;
        }
    };
}

bool is_allowed_type(Domain::ModelVolumeType volume_type)
{
    return volume_type == Domain::ModelVolumeType::MODEL_PART
        || volume_type == Domain::ModelVolumeType::NEGATIVE_VOLUME
        || volume_type == Domain::ModelVolumeType::PARAMETER_MODIFIER;
}
} // namespace

bool TextGizmo::add_text_to_scene(Domain::ModelVolumeType volume_type)
{
    if (!is_allowed_type(volume_type))
        return false;

    m_preset_manager.init();
    m_preset_manager.discard_preset_changes(); // create volume with stored settings

    const Domain::ElementRefs& els =
        m_project_interactor.scene_interactor().object_selection().elements;
    const Domain::Project& project = m_project_interactor.selected_project();
    const Scene::Scene& scene      = static_cast<Scene::ISceneProvider&>(m_scene_presenter).scene();
    auto guess                     = Scene::guess_volume_transformation(els, project, scene);

    Biz::Emboss::TextLines text_lines;
    const std::string& text = _u8L("Embossed text");
    auto issue_fn =
        create_issue_fn(dialog(), m_proj_ctxs->selected().warning_tooltip, m_project_interactor);
    auto base = create_base_data(
        text,
        volume_type,
        m_preset_manager,
        m_project_interactor,
        text_lines,
        issue_fn
    );
    if (guess.instance == nullptr) { // create object
        Biz::Emboss::CreateVolumeParams params{.base = std::move(base), .volume_type = volume_type};
        return Biz::Emboss::start_create_object_job(params, guess.bed_coor);
    } else {
        return Biz::Emboss::start_create_volume_job(
            *guess.instance,
            guess.transformation,
            base,
            volume_type
        );
    }
}

bool TextGizmo::update_volume(std::optional<Domain::ModelVolumeType> volume_type)
{
    Biz::Emboss::SelectedText selected =
        Biz::Emboss::get_selected_text_volume(m_project_interactor);
    ASSERT(selected.volume != nullptr); // no volume selected
    const Domain::ModelVolume& volume = *selected.volume;
    ProjectContext& proj_ctx          = m_proj_ctxs->selected();

    // check that selection did not change without call 'on_scene_selection_changed()'
    ASSERT(proj_ctx.last_loaded_volume_id == volume.id());

    // exist loadeable font file?
    if (!m_preset_manager.get_font_file_with_cache().has_value()) {
        proj_ctx.warning_tooltip = _u8L(
            "The text cannot be written using the selected font. Please try choosing a different font."
        );
        // full priority(remove other warnings)
        m_dialog->set_warning(proj_ctx.warning_tooltip);
        return false;
    }

    // without text there is nothing to emboss
    const std::string& text = proj_ctx.text;
    if (is_text_empty(text)) {
        proj_ctx.warning_tooltip = _u8L("Embossed text cannot contain only white spaces.");
        // full priority(remove other warnings)
        m_dialog->set_warning(proj_ctx.warning_tooltip);
        return false;
    }

    Domain::ModelVolumeType new_type = volume_type.value_or(volume.type());
    auto issue_fn = create_issue_fn(dialog(), proj_ctx.warning_tooltip, m_project_interactor);
    Biz::Emboss::UpdateVolumeParams params{
        .base = create_base_data(
            text,
            new_type,
            m_preset_manager,
            m_project_interactor,
            m_text_lines.get_lines(),
            issue_fn,
            proj_ctx.volume_scale.depth
        ),
        .volume_id   = volume.id(),
        .instance_id = selected.instance_id,
        .volume_type = volume_type
    };

    // remove_notification_not_valid_font();
    return start_update_volume(std::move(params), volume);
}

void TextGizmo::close()
{
    m_gizmo_controller.deactivate_current_tool();
}

void TextGizmo::rotate(double absolut_angle_in_rad)
{
    double current = m_preset_manager.get_preset().angle.value_or(0.f);
    if (Domain::is_approx(current, absolut_angle_in_rad, 1e-3))
        return; // approx same

    double diff_angle = absolut_angle_in_rad - current;
    Domain::Transform3d relative_volume_tr{Eigen::AngleAxisd(diff_angle, Domain::Vec3d::UnitZ())};
    Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);

    // recalculate current rotation
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ElementRef& el =
        m_project_interactor.scene_interactor().object_selection().elements.front();
    m_preset_manager.get_preset().angle = Biz::Emboss::calc_rotation(project, el);

    // Is set what was wanted?
    // assert(Domain::is_approx(m_preset_manager.get_preset().angle.value_or(0.f), (float)value, 1e-3f));

    if (m_preset_manager.get_font_prop().per_glyph) // recalculate lines
        m_text_lines.create_text_lines(m_text_lines.get_lines().size());

    // update shape when needed
    if (m_preset_manager.get_preset().projection.use_surface
        || m_preset_manager.get_font_prop().per_glyph)
        update_volume();
}
} // namespace Slic3r::App::Plater
