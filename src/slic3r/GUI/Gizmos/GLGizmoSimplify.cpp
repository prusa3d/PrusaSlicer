///|/ Copyright (c) Prusa Research 2021 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoSimplify.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

#include <GL/glew.h>

#include <thread>

namespace Slic3r::GUI {

// Extend call after only when Simplify gizmo is still alive
static void call_after_if_active(std::function<void()> fn, GUI_App* app = &wxGetApp())
{
    // check application GUI
    if (app == nullptr) return;
    app->CallAfter([fn, app]() {
        // app must exist because it call this
        // if (app == nullptr) return;
        const Plater *plater = app->plater();
        if (plater == nullptr) return;
        const GLCanvas3D *canvas = plater->canvas3D();
        if (canvas == nullptr) return;
        const GLGizmosManager &mng = canvas->get_gizmos_manager();
        // check if simplify is still activ gizmo
        if (mng.get_current_type() != GLGizmosManager::Simplify) return;
        fn();
    });
}

static std::set<ObjectID> get_selected_volume_ids(const Selection &selection)
{
    const Selection::IndicesList &volume_ids = selection.get_volume_idxs();
    const ModelObjectPtrs &model_objects     = selection.get_model()->objects;
    std::set<ObjectID> result;
    for (auto volume_id : volume_ids) {
        const GLVolume *selected_volume = selection.get_volume(volume_id);
        assert(selected_volume != nullptr);

        const GLVolume::CompositeID &cid = selected_volume->composite_id;

        assert(cid.object_id >= 0);
        assert(model_objects.size() > static_cast<size_t>(cid.object_id));

        const ModelObject *obj    = model_objects[cid.object_id];
        const ModelVolume *volume = obj->volumes[cid.volume_id];
        const ObjectID &   id     = volume->id();
        
        // prevent selection of volume without inidces
        if (volume->mesh().its.indices.empty()) continue;

        assert(result.find(id) == result.end());
        result.insert(id);
    }
    return result;
}

static std::string create_volumes_name(const std::set<ObjectID>& ids, const Selection &selection){
    assert(!ids.empty());
    std::string name;
    bool        is_first = true;
    for (const ObjectID &id : ids) {
        if (is_first)
            is_first = false;
        else
            name += " + ";

        const ModelVolume *volume = get_selected_volume(id, selection);
        assert(volume != nullptr);
        name += volume->name;
    }
    return name;
}

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D &parent)
    : GLGizmoBase(parent, M_ICON_FILENAME, -1)
    , m_show_wireframe(false)
    , m_move_to_center(false)
    , m_original_triangle_count(0)
    , m_triangle_count(0)
    // translation for GUI size
    , tr_mesh_name(_u8L("Mesh name"))
    , tr_triangles(_u8L("Triangles"))
    , tr_detail_level(_u8L("Level of detail"))
    , tr_decimate_ratio(_u8L("Decimate ratio"))
{}

GLGizmoSimplify::~GLGizmoSimplify()
{ 
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
}

bool GLGizmoSimplify::on_esc_key_down() {
    //close();
    return stop_worker_thread_request();
}

// while opening needs GLGizmoSimplify to set window position
void GLGizmoSimplify::add_simplify_suggestion_notification(
    const std::vector<size_t> &object_ids,
    const std::vector<ModelObject*>&    objects,
    NotificationManager &      manager)
{
    std::vector<size_t> big_ids;
    big_ids.reserve(object_ids.size());
    auto is_big_object = [&objects](size_t object_id) {
        const uint32_t triangles_to_suggest_simplify = 1000000;
        if (object_id >= objects.size()) return false; // out of object index
        ModelVolumePtrs &volumes = objects[object_id]->volumes;
        if (volumes.size() != 1) return false; // not only one volume
        size_t triangle_count = volumes.front()->mesh().its.indices.size();
        if (triangle_count < triangles_to_suggest_simplify)
            return false; // small volume
        return true;
    };
    std::copy_if(object_ids.begin(), object_ids.end(),
                 std::back_inserter(big_ids), is_big_object);
    if (big_ids.empty()) return;

    for (size_t object_id : big_ids) {
        std::string t = GUI::format(_L(
            "Processing model \"%1%\" with more than 1M triangles "
            "could be slow. It is highly recommended to reduce "
            "amount of triangles."), objects[object_id]->name);
        std::string hypertext = _u8L("Simplify model");

        std::function<bool(wxEvtHandler *)> open_simplify =
            [object_id](wxEvtHandler *) {
                auto plater = wxGetApp().plater();
                if (object_id >= plater->model().objects.size()) return true;

                Selection &selection = plater->canvas3D()->get_selection();
                selection.clear();
                selection.add_object((unsigned int) object_id);

                auto &manager = plater->canvas3D()->get_gizmos_manager();
                bool  close_notification = true;
                if(!manager.open_gizmo(GLGizmosManager::Simplify))
                    return close_notification;
                GLGizmoSimplify* simplify = dynamic_cast<GLGizmoSimplify*>(manager.get_current());
                if (simplify == nullptr) return close_notification;
                simplify->set_center_position();
                return close_notification;
            };
        manager.push_simplify_suggestion_notification(
            t, objects[object_id]->id(), hypertext, open_simplify);
    }
}

std::string GLGizmoSimplify::on_get_name() const
{
    return _u8L("Simplify");
}

void GLGizmoSimplify::on_render_input_window(float x, float y, float bottom_limit)
{
    create_gui_cfg();
    const Selection &selection = m_parent.get_selection();
    auto act_volume_ids = get_selected_volume_ids(selection);
    if (act_volume_ids.empty()) {
        stop_worker_thread_request();
        close();
        if (m_parent.get_selection().volumes_count() != 1) {
            MessageDialog msg((wxWindow*)wxGetApp().mainframe,
                _L("Simplification is currently only allowed when a single part is selected"),
                _L("Error"));
            msg.ShowModal();
        }
        return;
    }

    bool is_cancelling = false;
    bool is_worker_running = false;
    bool is_result_ready = false;
    int progress = 0; 
    {
        std::lock_guard lk(m_state_mutex);
        is_cancelling = m_state.status == State::cancelling;
        is_worker_running = m_state.status == State::running;
        is_result_ready = !m_state.result.empty();
        progress = m_state.progress;
    }

    // Whether to trigger calculation after rendering is done.
    bool start_process = false;
    
    // Check selection of new volume (or change)
    // Do not reselect object when processing 
    if (m_volume_ids != act_volume_ids) {
        bool change_window_position = m_volume_ids.empty();
        // select different model

        // close suggestion notification
        auto notification_manager = wxGetApp().plater()->get_notification_manager();
        for (const auto &id : act_volume_ids) 
            notification_manager->remove_simplify_suggestion_with_id(id);

        m_volume_ids = std::move(act_volume_ids);        
        init_model();

        // triangle count is calculated in init model
        m_original_triangle_count = m_triangle_count;

        // Default value of configuration
        m_configuration.decimate_ratio = 50.; // default value
        m_configuration.fix_count_by_ratio(m_original_triangle_count);
        m_configuration.use_count = false;

        // Create volumes name to describe what will be simplified
        std::string name = create_volumes_name(m_volume_ids, selection);
        if (name.length() > m_gui_cfg->max_char_in_name)
            name = name.substr(0, m_gui_cfg->max_char_in_name - 3) + "...";
        m_volumes_name = name;

        // Start processing. If we switched from another object, process will
        // stop the background thread and it will restart itself later.
        start_process = true;
        
        // set window position
        if (change_window_position) {
            ImVec2 pos;
            Size parent_size = m_parent.get_canvas_size();
            if (m_move_to_center) {
                m_move_to_center   = false;
                pos = ImVec2(parent_size.get_width() / 2 - m_gui_cfg->window_offset_x,
                             parent_size.get_height() / 2 - m_gui_cfg->window_offset_y);                
            } else {
                // keep window wisible on canvas and close to mouse click
                pos = ImGui::GetMousePos();
                pos.x -= m_gui_cfg->window_offset_x;
                pos.y -= m_gui_cfg->window_offset_y;
                // minimal top left value
                ImVec2 tl(m_gui_cfg->window_padding,
                          m_gui_cfg->window_padding + m_parent.get_main_toolbar_height());
                if (pos.x < tl.x) pos.x = tl.x;
                if (pos.y < tl.y) pos.y = tl.y;
                // maximal bottom right value
                ImVec2 br(parent_size.get_width() - (2 * m_gui_cfg->window_offset_x + m_gui_cfg->window_padding),
                          parent_size.get_height() -(2 * m_gui_cfg->window_offset_y + m_gui_cfg->window_padding));
                if (pos.x > br.x) pos.x = br.x;
                if (pos.y > br.y) pos.y = br.y;
            }
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        }
    }

    bool is_multipart = (m_volume_ids.size() > 1);
    int flag = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoCollapse;
    ImGuiPureWrap::begin(on_get_name(), flag);
    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, tr_mesh_name + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    ImGuiPureWrap::text(m_volumes_name);
    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, tr_triangles + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);

    ImGuiPureWrap::text(std::to_string(m_original_triangle_count));

    ImGui::Separator();

    if(ImGui::RadioButton("##use_error", !m_configuration.use_count) && !is_multipart) {
        m_configuration.use_count = !m_configuration.use_count;
        start_process = true;
    }
    ImGui::SameLine();
    m_imgui->disabled_begin(m_configuration.use_count);
    ImGui::Text("%s", tr_detail_level.c_str());
    std::vector<std::string> reduce_captions = {
        static_cast<std::string>(_u8L("Extra high")),
        static_cast<std::string>(_u8L("High")),
        static_cast<std::string>(_u8L("Medium")),
        static_cast<std::string>(_u8L("Low")),
        static_cast<std::string>(_u8L("Extra low"))
    };
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    static int reduction = 2;
    if(ImGui::SliderInt("##ReductionLevel", &reduction, 0, 4, reduce_captions[reduction].c_str())) {
        if (reduction < 0) reduction = 0;
        if (reduction > 4) reduction = 4;
        switch (reduction) {
        case 0: m_configuration.max_error = 1e-3f; break;
        case 1: m_configuration.max_error = 1e-2f; break;
        case 2: m_configuration.max_error = 0.1f; break;
        case 3: m_configuration.max_error = 0.5f; break;
        case 4: m_configuration.max_error = 1.f; break;
        }
        start_process = true;
    }
    m_imgui->disabled_end(); // !use_count

    if (ImGui::RadioButton("##use_count", m_configuration.use_count) && !is_multipart) {
        m_configuration.use_count = !m_configuration.use_count;
        start_process = true;
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_multipart)
        ImGui::SetTooltip("%s", _u8L("A multipart object can be simplified using only a Level of detail. "
                                     "If you want to enter a Decimate ratio, do the simplification separately.").c_str());
    ImGui::SameLine();

    // show preview result triangle count (percent)
    if (!m_configuration.use_count) {
        m_configuration.wanted_count = static_cast<uint32_t>(m_triangle_count);
        m_configuration.decimate_ratio = 
            (1.0f - (m_configuration.wanted_count / (float) m_original_triangle_count)) * 100.f;
    }

    m_imgui->disabled_begin(!m_configuration.use_count);
    ImGui::Text("%s", tr_decimate_ratio.c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    const char * format = (m_configuration.decimate_ratio > 10)? "%.0f %%": 
        ((m_configuration.decimate_ratio > 1)? "%.1f %%":"%.2f %%");

    if(m_imgui->slider_float("##decimate_ratio",  &m_configuration.decimate_ratio, 0.f, 100.f, format)){
        if (m_configuration.decimate_ratio < 0.f)
            m_configuration.decimate_ratio = 0.01f;
        if (m_configuration.decimate_ratio > 100.f)
            m_configuration.decimate_ratio = 100.f;
        m_configuration.fix_count_by_ratio(m_original_triangle_count);
        start_process = true;
    }

    ImGui::NewLine();
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::Text(_u8L("%d triangles").c_str(), m_configuration.wanted_count);
    m_imgui->disabled_end(); // use_count

    ImGui::Checkbox(_u8L("Show wireframe").c_str(), &m_show_wireframe);

    m_imgui->disabled_begin(is_cancelling);
    if (ImGuiPureWrap::button(_u8L("Close"))) {
        close();
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_cancelling)
        ImGui::SetTooltip("%s", _u8L("Operation already cancelling. Please wait few seconds.").c_str());
    m_imgui->disabled_end(); // state cancelling

    ImGui::SameLine();

    m_imgui->disabled_begin(is_worker_running || ! is_result_ready);
    if (ImGuiPureWrap::button(_u8L("Apply"))) {
        apply_simplify();
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_worker_running)
        ImGui::SetTooltip("%s", _u8L("Can't apply when proccess preview.").c_str());
    m_imgui->disabled_end(); // state !settings

    // draw progress bar
    if (is_worker_running) { // apply or preview
        ImGui::SameLine(m_gui_cfg->bottom_left_width);
        // draw progress bar
        std::string progress_text = GUI::format(_L("Process %1% / 100"), std::to_string(progress));
        ImVec2 progress_size(m_gui_cfg->input_width, 0.f);
        ImGui::ProgressBar(progress / 100., progress_size, progress_text.c_str());
    }
    ImGuiPureWrap::end();
    if (start_process)
        process();
}


void GLGizmoSimplify::close() {
    // close gizmo == open it again
    GLGizmosManager &gizmos_mgr = m_parent.get_gizmos_manager();
    gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);
}

bool GLGizmoSimplify::stop_worker_thread_request()
{
    std::lock_guard lk(m_state_mutex);
    if (m_state.status != State::running) return false;
    
    m_state.status = State::Status::cancelling;
    return true;    
}


// Following is called from a UI thread when the worker terminates
// worker calls it through a CallAfter.
void GLGizmoSimplify::worker_finished()
{
    {
        std::lock_guard lk(m_state_mutex);
        if (m_state.status == State::running) {
            // Someone started the worker again, before this callback
            // was called. Do nothing.
            return;
        }
    }
    if (m_worker.joinable())
        m_worker.join();
    if (GLGizmoBase::m_state == Off)
        return;
    const auto &result = m_state.result;
    if (!result.empty())
        update_model(result);

    if (m_state.config != m_configuration || m_state.volume_ids != m_volume_ids) {
        // Settings were changed, restart the worker immediately.
        process();
    }
    request_rerender(true);
}

void GLGizmoSimplify::process()
{
    if (m_volume_ids.empty()) return;

    // m_volume->mesh().its.indices.empty()
    bool configs_match = false;
    bool result_valid  = false;
    bool is_worker_running = false;
    {
        std::lock_guard lk(m_state_mutex);
        configs_match = (m_volume_ids == m_state.volume_ids && m_state.config == m_configuration);
        result_valid = !m_state.result.empty();
        is_worker_running = m_state.status == State::running;
    }

    if ((result_valid || is_worker_running) && configs_match) {
        // Either finished or waiting for result already. Nothing to do.
        return;
    }

    if (is_worker_running && ! configs_match) {
        // Worker is running with outdated config. Stop it. It will
        // restart itself when cancellation is done.
        stop_worker_thread_request();
        return;
    }

    if (m_worker.joinable()) {
        // This can happen when process() is called after previous worker terminated,
        // but before the worker_finished callback was called. In this case, just join the thread,
        // the callback will check this and do nothing.
        m_worker.join();
    }

    // Copy configuration that will be used.    
    m_state.config = m_configuration;
    m_state.volume_ids = m_volume_ids;
    m_state.status = State::running;

    // Create a copy of current meshes to pass to the worker thread.
    // Using unique_ptr instead of pass-by-value to avoid an extra
    // copy (which would happen when passing to std::thread).
    const Selection& selection = m_parent.get_selection();
    State::Data its;
    for (const auto &id : m_volume_ids) {
        const ModelVolume *volume = get_selected_volume(id, selection);
        its[id] = std::make_unique<indexed_triangle_set>(volume->mesh().its); // copy
    }
    
    m_worker = std::thread([this](State::Data its) {

        // Checks that the UI thread did not request cancellation, throws if so.
        std::function<void(void)> throw_on_cancel = [this]() {
            std::lock_guard lk(m_state_mutex);
            if (m_state.status == State::cancelling)
                throw SimplifyCanceledException();
        };

        // Called by worker thread, updates progress bar.
        // Using CallAfter so the rerequest function is run in UI thread.
        std::function<void(int)> statusfn = [this](int percent) {
            std::lock_guard lk(m_state_mutex);
            m_state.progress = percent;
            call_after_if_active([this]() { request_rerender(); });
        };

        // Initialize.
        uint32_t triangle_count = 0;
        float    max_error = std::numeric_limits<float>::max();
        {
            std::lock_guard lk(m_state_mutex);
            if (m_state.config.use_count)
                triangle_count = m_state.config.wanted_count;
            if (! m_state.config.use_count)
                max_error = m_state.config.max_error;
            m_state.progress = 0;
            m_state.result.clear();
            m_state.status = State::Status::running;
        }

        // Start the actual calculation.
        try {
            for (const auto& it : its) {
                float me = max_error;
                its_quadric_edge_collapse(*it.second, triangle_count, &me, throw_on_cancel, statusfn);
            }
        } catch (SimplifyCanceledException &) {
            std::lock_guard lk(m_state_mutex);
            m_state.status = State::idle;
        }

        std::lock_guard lk(m_state_mutex);
        if (m_state.status == State::Status::running) {
            // We were not cancelled, the result is valid.
            m_state.status = State::Status::idle;
            m_state.result = std::move(its);
        }

        // Update UI. Use CallAfter so the function is run on UI thread.
        call_after_if_active([this]() { worker_finished(); });
    }, std::move(its));
}

void GLGizmoSimplify::apply_simplify() {
    // worker must be stopped
    assert(m_state.status == State::Status::idle);

    // check that there is NO change of volume
    assert(m_state.volume_ids == m_volume_ids);

    const Selection& selection = m_parent.get_selection();
    auto plater = wxGetApp().plater();
    // TRN %1% = volumes name
    plater->take_snapshot(Slic3r::format(_u8L("Simplify %1%"), create_volumes_name(m_volume_ids, selection)));
    plater->clear_before_change_mesh(selection.get_object_idx(), _u8L("Custom supports, seams and multimaterial painting were "
                                                                      "removed after simplifying the mesh."));
    // After removing custom supports, seams, and multimaterial painting, we have to update info about the object to remove information about
    // custom supports, seams, and multimaterial painting in the right panel.
    wxGetApp().obj_list()->update_info_items(selection.get_object_idx());

    for (const auto &item: m_state.result) {
        const ObjectID &id = item.first;
        const indexed_triangle_set &its = *item.second;
        ModelVolume *volume = get_selected_volume(id, selection);
        assert(volume != nullptr);
        ModelObject *obj = volume->get_object();

        volume->set_mesh(std::move(its));
        volume->calculate_convex_hull();
        volume->set_new_unique_id();
        obj->invalidate_bounding_box();
        obj->ensure_on_bed(true); // allow negative z
    }
    m_state.result.clear();
    // fix hollowing, sla support points, modifiers, ...  
    int object_idx = selection.get_object_idx();
    plater->changed_mesh(object_idx);
    // Fix warning icon in object list
    wxGetApp().obj_list()->update_item_error_icon(object_idx, -1);
    close();
}

bool GLGizmoSimplify::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoSimplify::on_set_state() 
{
    // Closing gizmo. e.g. selecting another one
    if (GLGizmoBase::m_state == GLGizmoBase::Off) {
        m_parent.toggle_model_objects_visibility(true);

        stop_worker_thread_request();
        m_volume_ids.clear(); // invalidate selected model
        m_glmodels.clear(); // free gpu memory
    } else if (GLGizmoBase::m_state == GLGizmoBase::On) {
        // when open by hyperlink it needs to show up
        request_rerender();
    }
}

void GLGizmoSimplify::create_gui_cfg() { 
    if (m_gui_cfg.has_value()) return;
    int    space_size = ImGuiPureWrap::calc_text_size(std::string_view{":MM"}).x;
    GuiCfg cfg;
    cfg.top_left_width = std::max(ImGuiPureWrap::calc_text_size(tr_mesh_name).x,
                                  ImGuiPureWrap::calc_text_size(tr_triangles).x) 
        + space_size;

    const float radio_size = ImGui::GetFrameHeight();
    cfg.bottom_left_width =
        std::max(ImGuiPureWrap::calc_text_size(tr_detail_level).x,
                 ImGuiPureWrap::calc_text_size(tr_decimate_ratio).x) +
        space_size + radio_size;

    cfg.input_width   = cfg.bottom_left_width * 1.5;
    cfg.window_offset_x = (cfg.bottom_left_width + cfg.input_width)/2;
    cfg.window_offset_y = ImGui::GetTextLineHeightWithSpacing() * 5;
    
    m_gui_cfg = cfg;
}

void GLGizmoSimplify::request_rerender(bool force) {
    int64_t now = m_parent.timestamp_now();
    if (force || now > m_last_rerender_timestamp + 250) { // 250 ms
        set_dirty();
        m_parent.schedule_extra_frame(0);
        m_last_rerender_timestamp = now;
    }
}

void GLGizmoSimplify::set_center_position() {
    m_move_to_center = true; 
}

void GLGizmoSimplify::init_model()
{
    // volume ids must be set before init model
    assert(!m_volume_ids.empty());

    m_parent.toggle_model_objects_visibility(true); // selected volume may have changed
    const auto info = m_c->selection_info();

    const Selection &selection = m_parent.get_selection();
    Model &          model     = *selection.get_model();
    const Selection::IndicesList &volume_ids = selection.get_volume_idxs();
    const ModelObjectPtrs &model_objects = model.objects;

    m_glmodels.clear();
    //m_glmodels.reserve(volume_ids.size());
    m_triangle_count = 0;
    for (const ObjectID& id: m_volume_ids) {

        const GLVolume *selected_volume;
        const ModelVolume *volume = nullptr;
        for (auto volume_id : volume_ids) {
            selected_volume = selection.get_volume(volume_id);
            const GLVolume::CompositeID &cid = selected_volume->composite_id;
            ModelObject *                obj = model_objects[cid.object_id];
            ModelVolume *                act_volume = obj->volumes[cid.volume_id];
            if (id == act_volume->id()) {
                volume = act_volume;
                break;
            }
        }
        assert(volume != nullptr);

        // set actual triangle count
        m_triangle_count += volume->mesh().its.indices.size();

        assert(m_glmodels.find(id) == m_glmodels.end());
        GLModel &glmodel = m_glmodels[id]; // create new glmodel
        glmodel.init_from(volume->mesh());
        glmodel.set_color(selected_volume->color);

        m_parent.toggle_model_objects_visibility(false, info->model_object(),
                                                 info->get_active_instance(),
                                                 volume);
    }
}

void GLGizmoSimplify::update_model(const State::Data &data)
{
    // check that model exist
    if (m_glmodels.empty()) return;

    // check that result is for actual gl models
    size_t model_count = m_glmodels.size();
    if (data.size() != model_count) return;  
        
    m_triangle_count = 0;
    for (const auto &item : data) {
        const indexed_triangle_set &its = *item.second;

        auto it = m_glmodels.find(item.first);
        assert(it != m_glmodels.end());

        GLModel &glmodel = it->second;
        auto color = glmodel.get_color();
        // when not reset it keeps old shape
        glmodel.reset();
#if SLIC3R_OPENGL_ES
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3E3 };
        init_data.reserve_vertices(3 * its.indices.size());
        init_data.reserve_indices(3 * its.indices.size());

        // vertices + indices
        std::array<Vec3f, 3> barycentric_coords = { Vec3f::UnitX(), Vec3f::UnitY(), Vec3f::UnitZ() };
        unsigned int vertices_counter = 0;
        for (uint32_t i = 0; i < its.indices.size(); ++i) {
            const stl_triangle_vertex_indices face = its.indices[i];
            const stl_vertex                  vertex[3] = { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] };
            const stl_vertex                  n = face_normal_normalized(vertex);
            for (size_t j = 0; j < 3; ++j) {
                init_data.add_vertex(vertex[j], n, barycentric_coords[j]);
            }
            vertices_counter += 3;
            init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
        }

        glmodel.init_from(std::move(init_data));
#else
        glmodel.init_from(its);
#endif // SLIC3R_OPENGL_ES
        glmodel.set_color(color);

        m_triangle_count += its.indices.size();
    }
}

void GLGizmoSimplify::on_render()
{
    if (m_glmodels.empty()) return;
    
    const Selection &             selection  = m_parent.get_selection();
    
    // Check that the GLVolume still belongs to the ModelObject we work on.
    if (m_volume_ids != get_selected_volume_ids(selection)) return;

    const ModelObjectPtrs &model_objects = selection.get_model()->objects;
    const Selection::IndicesList &volume_idxs = selection.get_volume_idxs();

    // no need to render nothing
    if (volume_idxs.empty()) return;

    // Iteration over selection because of world transformation matrix of object
    for (auto volume_id : volume_idxs) {
        const GLVolume *selected_volume = selection.get_volume(volume_id);
        const GLVolume::CompositeID &cid = selected_volume->composite_id;

        ModelObject *obj    = model_objects[cid.object_id];
        ModelVolume *volume = obj->volumes[cid.volume_id];

        auto it = m_glmodels.find(volume->id());
        assert(it != m_glmodels.end());

        GLModel &glmodel = it->second;

        const Transform3d trafo_matrix = selected_volume->world_matrix();
        auto* gouraud_shader = wxGetApp().get_shader("gouraud_light");
        bool depth_test_enabled = ::glIsEnabled(GL_DEPTH_TEST);
        glsafe(::glEnable(GL_DEPTH_TEST));
        gouraud_shader->start_using();
        const Camera& camera = wxGetApp().plater()->get_camera();
        const Transform3d& view_matrix = camera.get_view_matrix();
        const Transform3d view_model_matrix = view_matrix * trafo_matrix;
        gouraud_shader->set_uniform("view_model_matrix", view_model_matrix);
        gouraud_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        gouraud_shader->set_uniform("view_normal_matrix", view_normal_matrix);
        glmodel.render();
        gouraud_shader->stop_using();

        if (m_show_wireframe) {
#if SLIC3R_OPENGL_ES
            auto* contour_shader = wxGetApp().get_shader("wireframe");
#else
            auto *contour_shader = wxGetApp().get_shader("mm_contour");
#endif // SLIC3R_OPENGL_ES
            contour_shader->start_using();
            contour_shader->set_uniform("offset", OpenGLManager::get_gl_info().is_mesa() ? 0.0005 : 0.00001);
            contour_shader->set_uniform("view_model_matrix", view_model_matrix);
            contour_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            const ColorRGBA color = glmodel.get_color();
            glmodel.set_color(ColorRGBA::WHITE());
#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile())
                glsafe(::glLineWidth(1.0f));
            glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
#endif // !SLIC3R_OPENGL_ES
            glmodel.render();
#if !SLIC3R_OPENGL_ES
            glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif // !SLIC3R_OPENGL_ES
            glmodel.set_color(color);
            contour_shader->stop_using();
        }
        if (depth_test_enabled)
            glsafe(::glEnable(GL_DEPTH_TEST));
    }
}

CommonGizmosDataID GLGizmoSimplify::on_get_requirements() const
{
    return CommonGizmosDataID(
        int(CommonGizmosDataID::SelectionInfo));
}

void GLGizmoSimplify::Configuration::fix_count_by_ratio(size_t triangle_count)
{
    if (decimate_ratio <= 0.f)
        wanted_count = static_cast<uint32_t>(triangle_count);
    else if (decimate_ratio >= 100.f)
        wanted_count = 0;
    else
        wanted_count = static_cast<uint32_t>(std::round(
            triangle_count * (100.f - decimate_ratio) / 100.f));
}

// any existing icon filename to not influence GUI
const std::string GLGizmoSimplify::M_ICON_FILENAME = "cut.svg";

} // namespace Slic3r::GUI
