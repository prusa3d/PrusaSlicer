///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoTextureSkin.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Feature/TextureSkin/MeshDisplace.hpp"
#include "libslic3r/Feature/TextureSkin/TexturePatterns.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>
#include <algorithm>

namespace Slic3r::GUI {

void GLGizmoTextureSkin::on_shutdown()
{
    join_bake_thread();
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoTextureSkin::on_get_name() const
{
    return _u8L("Paint-on texture skin");
}

bool GLGizmoTextureSkin::on_init()
{
    m_shortcut_key = WXK_CONTROL_T;

    m_desc["clipping_of_view"]             = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"]              = _u8L("Reset direction");
    m_desc["cursor_size"]                  = _u8L("Brush size") + ": ";
    m_desc["cursor_type"]                  = _u8L("Brush shape") + ": ";
    m_desc["add_texture_skin_caption"]     = _u8L("Left mouse button") + ": ";
    m_desc["add_texture_skin"]             = _u8L("Add texture skin");
    m_desc["remove_texture_skin_caption"]  = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove_texture_skin"]          = _u8L("Remove texture skin");
    m_desc["remove_all"]                   = _u8L("Remove all selection");
    m_desc["circle"]                       = _u8L("Circle");
    m_desc["sphere"]                       = _u8L("Sphere");
    m_desc["pointer"]                      = _u8L("Triangles");
    m_desc["tool_type"]                    = _u8L("Tool type") + ": ";
    m_desc["tool_brush"]                   = _u8L("Brush");
    m_desc["tool_smart_fill"]              = _u8L("Smart fill");
    m_desc["smart_fill_angle"]             = _u8L("Smart fill angle");
    m_desc["split_triangles"]              = _u8L("Split triangles");

    return true;
}

void GLGizmoTextureSkin::render_painter_gizmo()
{
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoTextureSkin::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(22.f);

    y = std::min(y, bottom_limit - approx_height);
    ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always);

    ImGuiPureWrap::begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    const float clipping_slider_left   = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                                  ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left     = ImGuiPureWrap::calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float smart_fill_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_circle  = ImGuiPureWrap::calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_sphere  = ImGuiPureWrap::calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_pointer = ImGuiPureWrap::calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(2.5f);

    const float button_width         = ImGuiPureWrap::calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width        = m_imgui->scaled(0.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    const float tool_type_radio_left       = ImGuiPureWrap::calc_text_size(m_desc["tool_type"]).x + m_imgui->scaled(1.f);
    const float tool_type_radio_brush      = ImGuiPureWrap::calc_text_size(m_desc["tool_brush"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_smart_fill = ImGuiPureWrap::calc_text_size(m_desc["tool_smart_fill"]).x + m_imgui->scaled(2.5f);

    const float split_triangles_checkbox_width = ImGuiPureWrap::calc_text_size(m_desc["split_triangles"]).x + m_imgui->scaled(2.5f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const std::string t : {"add_texture_skin", "remove_texture_skin"}) {
        caption_max    = std::max(caption_max, ImGuiPureWrap::calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, ImGuiPureWrap::calc_text_size(m_desc[t]).x);
    }

    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(smart_fill_slider_left, std::max(cursor_slider_left, clipping_slider_left));
    const float slider_icon_width  = ImGuiPureWrap::get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
    window_width                   = std::max(window_width, total_text_max);
    window_width                   = std::max(window_width, button_width);
    window_width                   = std::max(window_width, split_triangles_checkbox_width);
    window_width                   = std::max(window_width, cursor_type_radio_circle + cursor_type_radio_sphere + cursor_type_radio_pointer);
    window_width                   = std::max(window_width, tool_type_radio_left + tool_type_radio_brush + tool_type_radio_smart_fill);
    window_width                   = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [&caption_max](const std::string &caption, const std::string &text) {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        ImGuiPureWrap::text(text);
    };

    for (const std::string t : {"add_texture_skin", "remove_texture_skin"}) {
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));
    }

    ImGui::Separator();

    std::string format_str = std::string("%.f") + I18N::translate_utf8("°",
        "Degree sign to use in the respective slider in texture skin gizmo,"
        "placed after the number with no whitespace in between.");

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc["tool_type"]);

    float tool_type_offset = tool_type_radio_left + (window_width - tool_type_radio_left - tool_type_radio_brush - tool_type_radio_smart_fill + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(tool_type_offset);
    ImGui::PushItemWidth(tool_type_radio_brush);
    if (ImGuiPureWrap::radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH))
        m_tool_type = ToolType::BRUSH;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Paints facets according to the chosen painting brush."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush);
    ImGui::PushItemWidth(tool_type_radio_smart_fill);
    if (ImGuiPureWrap::radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL))
        m_tool_type = ToolType::SMART_FILL;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Paints neighboring facets whose relative angle is less or equal to set angle."), max_tooltip_width);

    ImGui::Separator();

    if (m_tool_type == ToolType::BRUSH) {
        ImGuiPureWrap::text(m_desc.at("cursor_type"));
        ImGui::NewLine();

        float cursor_type_offset = (window_width - cursor_type_radio_sphere - cursor_type_radio_circle - cursor_type_radio_pointer + m_imgui->scaled(1.5f)) / 2.f;
        ImGui::SameLine(cursor_type_offset);
        ImGui::PushItemWidth(cursor_type_radio_sphere);
        if (ImGuiPureWrap::radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
            m_cursor_type = TriangleSelector::CursorType::SPHERE;

        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
        ImGui::PushItemWidth(cursor_type_radio_circle);

        if (ImGuiPureWrap::radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
            m_cursor_type = TriangleSelector::CursorType::CIRCLE;

        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Ignores facets facing away from the camera."), max_tooltip_width);

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere + cursor_type_radio_circle);
        ImGui::PushItemWidth(cursor_type_radio_pointer);

        if (ImGuiPureWrap::radio_button(m_desc["pointer"], m_cursor_type == TriangleSelector::CursorType::POINTER))
            m_cursor_type = TriangleSelector::CursorType::POINTER;

        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Paints only one facet."), max_tooltip_width);

        m_imgui->disabled_begin(m_cursor_type != TriangleSelector::CursorType::SPHERE && m_cursor_type != TriangleSelector::CursorType::CIRCLE);

        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));

        ImGuiPureWrap::checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Splits bigger facets into smaller ones while the object is painted."), max_tooltip_width);

        m_imgui->disabled_end();
    } else {
        assert(m_tool_type == ToolType::SMART_FILL);
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc["smart_fill_angle"] + ":");

        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true, _L("Alt + Mouse wheel")))
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
    }

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("clipping_of_view"));
    } else {
        if (ImGuiPureWrap::button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, from_u8(GUI::shortkey_ctrl_prefix()) + _L("Mouse wheel")))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    ImGui::Separator();
    if (ImGuiPureWrap::button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
        ModelObject         *mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data();
            }

        update_model_object();
        m_parent.set_as_dirty();
    }

    // --- Bake displacement section --------------------------------------
    ImGui::Separator();
    ImGuiPureWrap::text(_u8L("Bake displacement"));

    BakeState::Status status;
    int progress = 0;
    {
        std::lock_guard<std::mutex> lk(m_bake_mutex);
        status = m_bake_state.status;
        progress = m_bake_state.progress;
    }
    const bool running = status != BakeState::idle;

    m_imgui->disabled_begin(running);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Amplitude (mm)") + ":");
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    m_imgui->slider_float("##bake_amp", &m_bake_amplitude_mm, 0.05f, 5.0f, "%.2f", 1.0f, true);

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Edge length (mm)") + ":");
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    m_imgui->slider_float("##bake_edge", &m_bake_edge_length_mm, 0.1f, 2.0f, "%.2f", 1.0f, true);

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Target triangles") + ":");
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    {
        int min_t = 1000, max_t = 2000000;
        ImGui::SliderInt("##bake_tri", &m_bake_target_triangles, min_t, max_t, "%d", ImGuiSliderFlags_Logarithmic);
    }
    ImGuiPureWrap::checkbox(_u8L("Skip bottom face (build-plate side)"), m_bake_skip_bottom);
    m_imgui->disabled_end();

    if (!running) {
        if (ImGuiPureWrap::button(_u8L("Bake mesh"))) {
            start_bake();
        }
    } else {
        ImGui::ProgressBar(float(progress) / 100.0f, ImVec2(-1.0f, 0.0f));
        if (ImGuiPureWrap::button(_u8L("Cancel"))) {
            cancel_bake();
        }
    }
    ImGuiPureWrap::text_wrapped(_u8L("Baking replaces the mesh with displaced geometry and clears paint. "
                                      "Afterwards, set Texture Skin to None in Print Settings to avoid double-texturing."),
                                 window_width);

    ImGuiPureWrap::end();
}

void GLGizmoTextureSkin::update_model_object() const
{
    bool         updated = false;
    ModelObject *mo      = m_c->selection_info()->model_object();
    int          idx     = -1;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++idx;
        updated |= mv->texture_skin_facets.set(*m_triangle_selectors[idx]);
    }

    if (updated) {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoTextureSkin::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++volume_id;

        const TriangleMesh *mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        m_triangle_selectors.back()->deserialize(mv->texture_skin_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

PainterGizmoType GLGizmoTextureSkin::get_painter_type() const
{
    return PainterGizmoType::TEXTURE_SKIN;
}

wxString GLGizmoTextureSkin::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    return shift_down ? _L("Remove texture skin") : _L("Add texture skin");
}

void GLGizmoTextureSkin::join_bake_thread()
{
    if (m_bake_thread.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_bake_mutex);
            if (m_bake_state.status == BakeState::running)
                m_bake_state.status = BakeState::cancelling;
        }
        m_bake_thread.join();
    }
}

void GLGizmoTextureSkin::cancel_bake()
{
    std::lock_guard<std::mutex> lk(m_bake_mutex);
    if (m_bake_state.status == BakeState::running)
        m_bake_state.status = BakeState::cancelling;
}

void GLGizmoTextureSkin::start_bake()
{
    namespace TS = Slic3r::Feature::TextureSkin;

    // Already running? Ignore.
    {
        std::lock_guard<std::mutex> lk(m_bake_mutex);
        if (m_bake_state.status != BakeState::idle) return;
    }

    // Make sure any previous (finished) thread is joined before starting.
    if (m_bake_thread.joinable()) m_bake_thread.join();

    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo) return;
    ModelVolume *volume = nullptr;
    for (ModelVolume *mv : mo->volumes)
        if (mv->is_model_part()) { volume = mv; break; }
    if (!volume) return;

    // Snapshot the mesh we'll operate on.
    auto input = std::make_shared<indexed_triangle_set>(volume->mesh().its);

    // Read pattern + UV settings from the currently-edited Print preset.
    const DynamicPrintConfig &cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const TextureSkinPattern  pattern_enum  = cfg.opt_enum<TextureSkinPattern>("texture_skin_pattern");
    const TextureSkinUVMode   uv_mode_enum  = cfg.opt_enum<TextureSkinUVMode>("texture_skin_uv_mode");
    const double uv_scale      = cfg.opt_float("texture_skin_uv_scale");
    const double uv_off_u      = cfg.opt_float("texture_skin_uv_offset_u");
    const double uv_off_v      = cfg.opt_float("texture_skin_uv_offset_v");
    const double uv_rotation   = cfg.opt_float("texture_skin_uv_rotation");
    const double mapping_blend = cfg.opt_float("texture_skin_mapping_blend");
    const std::string custom_img_path = cfg.opt_string("texture_skin_custom_image");

    const auto pattern = static_cast<TS::Pattern>(static_cast<int>(pattern_enum));
    const TS::GrayImage *img = nullptr;
    if (pattern == TS::Pattern::Custom) {
        if (!custom_img_path.empty()) img = &TS::get_custom_image(custom_img_path);
    } else {
        img = &TS::get_pattern_image(pattern, Slic3r::resources_dir());
    }
    if (!img || img->empty()) {
        MessageDialog(wxGetApp().plater(),
                      _L("Could not load the texture image. Pick a texture first."),
                      _L("Texture Skin"), wxICON_WARNING | wxOK).ShowModal();
        return;
    }

    // Fine-grained mask: painted sub-triangles in original mesh coordinates.
    auto painted_region = std::make_shared<indexed_triangle_set>();
    if (volume->is_texture_skin_painted()) {
        *painted_region = volume->texture_skin_facets.get_facets(*volume, TriangleStateType::TEXTURE_SKIN);
    }

    // Object-local bounds in mm (matches apply_texture_skin caller-side in LayerRegion.cpp).
    TS::MeshDisplaceParams params;
    params.image                 = img;
    params.uv_mode               = static_cast<TS::UVMode>(static_cast<int>(uv_mode_enum));
    params.uv_settings.scale_u   = std::max(uv_scale, 1e-4);
    params.uv_settings.scale_v   = params.uv_settings.scale_u;
    params.uv_settings.offset_u  = uv_off_u;
    params.uv_settings.offset_v  = uv_off_v;
    params.uv_settings.rotation_deg = uv_rotation;
    params.uv_settings.mapping_blend = mapping_blend;
    params.amplitude_mm          = m_bake_amplitude_mm;
    params.edge_length_mm        = m_bake_edge_length_mm;
    params.target_triangle_count = static_cast<uint32_t>(m_bake_target_triangles);
    params.bounds = BoundingBoxf3(input->vertices.begin(), input->vertices.end());
    params.painted_region_its    = painted_region->indices.empty() ? nullptr : painted_region.get();
    params.skip_bottom_face      = m_bake_skip_bottom;

    // Snapshot selection info.
    const int obj_idx = m_c->selection_info()->get_active_instance();
    (void)obj_idx; // unused; object index derived from selection at apply-time.
    {
        std::lock_guard<std::mutex> lk(m_bake_mutex);
        m_bake_state.status = BakeState::running;
        m_bake_state.progress = 0;
        m_bake_state.object_idx = m_parent.get_selection().get_object_idx();
        m_bake_state.volume_id = volume->id();
        m_bake_state.result = {};
    }

    m_bake_thread = std::thread([this, input, painted_region, params]() {
        auto throw_on_cancel = [this]() {
            std::lock_guard<std::mutex> lk(m_bake_mutex);
            if (m_bake_state.status == BakeState::cancelling)
                throw std::runtime_error("cancelled");
        };
        auto statusfn = [this](int percent) {
            std::lock_guard<std::mutex> lk(m_bake_mutex);
            m_bake_state.progress = percent;
        };

        indexed_triangle_set out;
        bool cancelled = false;
        try {
            out = Slic3r::Feature::TextureSkin::mesh_displace(*input, params, throw_on_cancel, statusfn);
        } catch (...) {
            cancelled = true;
        }

        {
            std::lock_guard<std::mutex> lk(m_bake_mutex);
            if (!cancelled && !out.indices.empty()) m_bake_state.result = std::move(out);
            m_bake_state.status = BakeState::idle;
        }

        wxGetApp().CallAfter([this]() { apply_bake(); });
    });
}

void GLGizmoTextureSkin::apply_bake()
{
    // Called on UI thread after worker finishes. Must handle both success and
    // cancellation (empty result).
    indexed_triangle_set result;
    int obj_idx = -1;
    ObjectID volume_id;
    {
        std::lock_guard<std::mutex> lk(m_bake_mutex);
        if (m_bake_state.status != BakeState::idle) return;
        result = std::move(m_bake_state.result);
        obj_idx = m_bake_state.object_idx;
        volume_id = m_bake_state.volume_id;
        m_bake_state.result = {};
    }
    if (result.indices.empty() || obj_idx < 0) return;

    if (m_bake_thread.joinable()) m_bake_thread.join();

    auto *plater = wxGetApp().plater();
    plater->take_snapshot(_u8L("Bake texture displacement"));
    plater->clear_before_change_mesh(obj_idx, _u8L("Painted annotations (supports, seams, multimaterial, "
                                                   "fuzzy/texture skin) were cleared by mesh displacement."));
    wxGetApp().obj_list()->update_info_items(obj_idx);

    ModelObject *mo = wxGetApp().model().objects[obj_idx];
    ModelVolume *volume = nullptr;
    for (ModelVolume *mv : mo->volumes)
        if (mv->id() == volume_id) { volume = mv; break; }
    if (!volume) return;

    volume->set_mesh(std::move(result));
    volume->calculate_convex_hull();
    volume->set_new_unique_id();
    mo->invalidate_bounding_box();
    mo->ensure_on_bed(true);

    plater->changed_mesh(obj_idx);
    wxGetApp().obj_list()->update_item_error_icon(obj_idx, -1);

    // Refresh the gizmo's triangle selectors so the (now-cleared) paint state
    // matches the new mesh.
    update_from_model_object();
    m_parent.set_as_dirty();
}

} // namespace Slic3r::GUI
