///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Hejl @hejllukas
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include <LocalesUtils.hpp>
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/CustomGCode.hpp"

#include "slic3r/GUI/format.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "GUI.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "MsgDialog.hpp"

#include "libslic3r/MultipleBeds.hpp"

#if ENABLE_ACTUAL_SPEED_DEBUG
#define IMGUI_DEFINE_MATH_OPERATORS
#endif // ENABLE_ACTUAL_SPEED_DEBUG
#include <imgui/imgui_internal.h>

#include <GL/glew.h>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <wx/progdlg.h>
#include <wx/numformatter.h>

#include <array>
#include <algorithm>
#include <chrono>


namespace Slic3r {
namespace GUI {

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
void GCodeViewer::COG::render(bool fixed_screen_size)
#else
void GCodeViewer::COG::render()
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
{
    if (!m_visible)
        return;

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    fixed_screen_size = true;
    init(fixed_screen_size);
#else
    init();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    GLShaderProgram* shader = wxGetApp().get_shader("toolpaths_cog");
    if (shader == nullptr)
        return;

    shader->start_using();

    glsafe(::glDisable(GL_DEPTH_TEST));

    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d model_matrix = Geometry::translation_transform(cog()) * Geometry::scale_transform(m_scale_factor);
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    if (fixed_screen_size) {
#else
    if (m_fixed_screen_size) {
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        const double inv_zoom = camera.get_inv_zoom();
        model_matrix = model_matrix * Geometry::scale_transform(inv_zoom);
    }
    
    Transform3d view_matrix = camera.get_view_matrix();
    view_matrix.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));

    shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    m_model.render();

    shader->stop_using();

    ////Show ImGui window 
    //static float last_window_width = 0.0f;
    //static size_t last_text_length = 0;

    //ImGuiWrapper& imgui = *wxGetApp().imgui();
    //const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    //ImGuiPureWrap::set_next_window_pos(0.5f * static_cast<float>(cnv_size.get_width()), 0.0f, ImGuiCond_Always, 0.5f, 0.0f);
    //ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //ImGui::SetNextWindowBgAlpha(0.25f);
    //ImGuiPureWrap::begin(std::string("COG"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    //ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Center of mass") + ":");
    //ImGui::SameLine();
    //char buf[1024];
    //const Vec3d position = cog();
    //sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", position.x(), position.y(), position.z());
    //ImGuiPureWrap::text(std::string(buf));

    //// force extra frame to automatically update window size
    //const float width = ImGui::GetWindowWidth();
    //const size_t length = strlen(buf);
    //if (width != last_window_width || length != last_text_length) {
    //    last_window_width = width;
    //    last_text_length = length;
    //    imgui.set_requires_extra_frame();
    //}

    //imgui.end();
    //ImGui::PopStyleVar();
}

#if ENABLE_ACTUAL_SPEED_DEBUG
int GCodeViewer::SequentialView::ActualSpeedImguiWidget::plot(const char* label, const std::array<float, 2>& frame_size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return -1;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiID id = window->GetID(label);

    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 internal_frame_size(frame_size[0], frame_size[1]);
    internal_frame_size = ImGui::CalcItemSize(internal_frame_size, ImGui::CalcItemWidth(), label_size.y + style.FramePadding.y * 2.0f);

    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + internal_frame_size);
    const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, 0, &frame_bb))
        return -1;

    const bool hovered = ImGui::ItemHoverable(frame_bb, id);

    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    static const int values_count_min = 2;
    const int values_count = static_cast<int>(data.size());
    int idx_hovered = -1;

    const ImVec2 offset(10.0f, 0.0f);

    const float size_y = y_range.second - y_range.first;
    const float size_x = data.back().pos - data.front().pos;
    if (size_x > 0.0f && values_count >= values_count_min) {
        const float inv_scale_y = (size_y == 0.0f) ? 0.0f : 1.0f / size_y;
        const float inv_scale_x = 1.0f / size_x;
        const float x0 = data.front().pos;
        const float y0 = y_range.first;

        const ImU32 grid_main_color = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        const ImU32 grid_secondary_color = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.5f, 0.5f));

        // horizontal levels
        for (const auto& [level, color] : levels) {
            const float y = 1.0f - ImSaturate((level - y_range.first) * inv_scale_y);

            window->DrawList->AddLine(ImLerp(inner_bb.Min, ImVec2(inner_bb.Min.x + offset.x, inner_bb.Max.y), ImVec2(0.1f, y)),
                ImLerp(inner_bb.Min, ImVec2(inner_bb.Min.x + offset.x, inner_bb.Max.y), ImVec2(0.9f, y)), ImGuiPSWrap::to_ImU32(color), 3.0f);

            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(0.0f, y)),
                ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(1.0f, y)), grid_main_color);
        }

        // vertical positions
        for (int n = 0; n < values_count - 1; ++n) {
            const float x = ImSaturate((data[n].pos - x0) * inv_scale_x);
            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(x, 0.0f)),
                ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(x, 1.0f)), data[n].internal ? grid_secondary_color : grid_main_color);
        }
        window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(1.0f, 0.0f)),
            ImLerp(inner_bb.Min + offset, inner_bb.Max, ImVec2(1.0f, 1.0f)), grid_main_color);

        // profiile
        const ImU32 col_base = ImGui::GetColorU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        const ImU32 col_hovered = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
        for (int n = 0; n < values_count - 1; ++n) {
            const ImVec2 tp1(ImSaturate((data[n].pos - x0) * inv_scale_x), 1.0f - ImSaturate((data[n].speed - y0) * inv_scale_y));
            const ImVec2 tp2(ImSaturate((data[n + 1].pos - x0) * inv_scale_x), 1.0f - ImSaturate((data[n + 1].speed - y0) * inv_scale_y));
            // Tooltip on hover
            if (hovered && inner_bb.Contains(io.MousePos)) {
                const float t = ImClamp((io.MousePos.x - inner_bb.Min.x - offset.x) / (inner_bb.Max.x - inner_bb.Min.x - offset.x), 0.0f, 0.9999f);
                if (tp1.x < t && t < tp2.x)
                    idx_hovered = n;
            }
            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, tp1), ImLerp(inner_bb.Min + offset, inner_bb.Max, tp2),
                idx_hovered == n ? col_hovered : col_base, 2.0f);
        }
    }

    if (label_size.x > 0.0f)
        ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);

    return idx_hovered;
}
#endif // ENABLE_ACTUAL_SPEED_DEBUG

void GCodeViewer::SequentialView::Marker::init()
{
    m_model.init_from(stilized_arrow(16, 2.0f, 4.0f, 1.0f, 8.0f));
    m_model.set_color({ 1.0f, 1.0f, 1.0f, 0.5f });
}

void GCodeViewer::SequentialView::Marker::render()
{
    if (!m_visible)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    shader->start_using();
    shader->set_uniform("emission_factor", 0.0f);
    const Camera& camera = wxGetApp().plater()->get_camera();
    
    Transform3d view_matrix = camera.get_view_matrix();
    view_matrix.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));

    float scale_factor = m_scale_factor;
    if (m_fixed_screen_size)
        scale_factor *= 10.0f * camera.get_inv_zoom();
    const Transform3d model_matrix = (Geometry::translation_transform((m_world_position + m_model_z_offset * Vec3f::UnitZ()).cast<double>()) *
        Geometry::translation_transform(scale_factor * m_model.get_bounding_box().size().z() * Vec3d::UnitZ()) * Geometry::rotation_transform({ M_PI, 0.0, 0.0 })) *
        Geometry::scale_transform(scale_factor);
    shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);

    m_model.render();

    shader->stop_using();

    glsafe(::glDisable(GL_BLEND));
}

static std::string to_string(libvgcode::EMoveType type)
{
    switch (type)
    {
    // TRN: Following strings are labels in the G-code Viewer legend.
    case libvgcode::EMoveType::Noop:        { return ("Noop"); }
    case libvgcode::EMoveType::Retract:     { return _u8L("Retract"); }
    case libvgcode::EMoveType::Unretract:   { return _u8L("Unretract"); }
    case libvgcode::EMoveType::Seam:        { return _u8L("Seam"); }
    case libvgcode::EMoveType::ToolChange:  { return _u8L("Tool Change"); }
    case libvgcode::EMoveType::ColorChange: { return _u8L("Color Change"); }
    case libvgcode::EMoveType::PausePrint:  { return _u8L("Pause Print"); }
    case libvgcode::EMoveType::CustomGCode: { return _u8L("Custom G-code"); }
    case libvgcode::EMoveType::Travel:      { return _u8L("Travel"); }
    case libvgcode::EMoveType::Wipe:        { return _u8L("Wipe"); }
    case libvgcode::EMoveType::Extrude:     { return _u8L("Extrude"); }
    default:                                { return _u8L("Unknown"); }
    }
}

static std::string to_string(libvgcode::EGCodeExtrusionRole role)
{
    switch (role)
    {
    // TRN: Following strings are labels in the G-code Viewer legend.
    case libvgcode::EGCodeExtrusionRole::None:                     { return _u8L("Unknown"); }
    case libvgcode::EGCodeExtrusionRole::Perimeter:                { return _u8L("Perimeter"); }
    case libvgcode::EGCodeExtrusionRole::ExternalPerimeter:        { return _u8L("External perimeter"); }
    case libvgcode::EGCodeExtrusionRole::OverhangPerimeter:        { return _u8L("Overhang perimeter"); }
    case libvgcode::EGCodeExtrusionRole::InternalInfill:           { return _u8L("Internal infill"); }
    case libvgcode::EGCodeExtrusionRole::SolidInfill:              { return _u8L("Solid infill"); }
    case libvgcode::EGCodeExtrusionRole::TopSolidInfill:           { return _u8L("Top solid infill"); }
    case libvgcode::EGCodeExtrusionRole::Ironing:                  { return _u8L("Ironing"); }
    case libvgcode::EGCodeExtrusionRole::BridgeInfill:             { return _u8L("Bridge infill"); }
    case libvgcode::EGCodeExtrusionRole::GapFill:                  { return _u8L("Gap fill"); }
    case libvgcode::EGCodeExtrusionRole::Skirt:                    { return _u8L("Skirt/Brim"); }
    case libvgcode::EGCodeExtrusionRole::SupportMaterial:          { return _u8L("Support material"); }
    case libvgcode::EGCodeExtrusionRole::SupportMaterialInterface: { return _u8L("Support material interface"); }
    case libvgcode::EGCodeExtrusionRole::WipeTower:                { return _u8L("Wipe tower"); }
    case libvgcode::EGCodeExtrusionRole::Custom:                   { return _u8L("Custom"); }
    default:                                                       { return _u8L("Unknown"); }
    }
}

void GCodeViewer::SequentialView::Marker::render_position_window(const libvgcode::Viewer* viewer)
{
    static float last_window_width = 0.0f;
    static size_t last_text_length = 0;
    static bool properties_shown = false;

    if (viewer != nullptr) {
        ImGuiWrapper& imgui = *wxGetApp().imgui();
        const ImGuiViewport& viewport = *ImGui::GetMainViewport();

        Preview* preview = dynamic_cast<Preview*>(wxGetApp().plater()->get_current_canvas3D()->get_wxglcanvas_parent());
        assert(preview);

        ImGuiPureWrap::set_next_window_pos(viewport.GetCenter().x, viewport.Size.y - preview->get_moves_slider_height(), ImGuiCond_Always, 0.5f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::SetNextWindowBgAlpha(properties_shown ? 0.8f : 0.25f);
        ImGuiPureWrap::begin(std::string("ToolPosition"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Position") + ":");
        ImGui::SameLine();
        libvgcode::PathVertex vertex = viewer->get_current_vertex();
        size_t vertex_id = viewer->get_current_vertex_id();
        if (vertex.type == libvgcode::EMoveType::Seam) {
            vertex_id = static_cast<size_t>(viewer->get_view_visible_range()[1]) - 1;
            vertex = viewer->get_vertex_at(vertex_id);
        }

        char buf[1024];
        sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", vertex.position[0], vertex.position[1], vertex.position[2]);
        ImGuiPureWrap::text(std::string(buf));

        ImGui::SameLine();
        // TRN: Show/hide properties is a tooltip on a button which toggles an extra window in the G-code Viewer, showing properties of current G-code segment.
        if (imgui.image_button(properties_shown ? ImGui::HorizontalHide : ImGui::HorizontalShow, properties_shown ? _u8L("Hide properties") : _u8L("Show properties"))) {
            properties_shown = !properties_shown;
            imgui.requires_extra_frame();
        }

        if (properties_shown) {
            auto append_table_row = [](const std::string& label, std::function<void(void)> value_callback) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, label);
                ImGui::TableSetColumnIndex(1);
                value_callback();
            };

            ImGui::Separator();
            if (ImGui::BeginTable("Properties", 2)) {
                char buff[1024];

                append_table_row(_u8L("Type"), [&vertex]() {
                    ImGuiPureWrap::text(_u8L(to_string(vertex.type)));
                });
                append_table_row(_u8L("Feature type"), [&vertex]() {
                    std::string text;
                    if (vertex.is_extrusion())
                        text = _u8L(to_string(vertex.role));
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                });
                append_table_row(_u8L("Width") + " (" + _u8L("mm") + ")", [&vertex, &buff]() {
                    std::string text;
                    if (vertex.is_extrusion()) {
                        sprintf(buff, "%.3f", vertex.width);
                        text = std::string(buff);
                    }
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                });
                append_table_row(_u8L("Height") + " (" + _u8L("mm") + ")", [&vertex, &buff]() {
                    std::string text;
                    if (vertex.is_extrusion()) {
                        sprintf(buff, "%.3f", vertex.height);
                        text = std::string(buff);
                    }
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                });
                append_table_row(_u8L("Layer"), [&vertex, &buff]() {
                    sprintf(buff, "%d", vertex.layer_id + 1);
                    const std::string text = std::string(buff);
                    ImGuiPureWrap::text(text);
                });
                append_table_row(_u8L("Speed") + " (" + _u8L("mm/s") + ")", [&vertex, &buff]() {
                    std::string text;
                    if (vertex.is_extrusion()) {
                        sprintf(buff, "%.1f", vertex.feedrate);
                        text = std::string(buff);
                    }
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                });
                  append_table_row(_u8L("Volumetric flow rate") + " (" + _u8L("mm³/s") + ")", [&vertex, &buff]() {
                    std::string text;
                    if (vertex.is_extrusion()) {
                        sprintf(buff, "%.3f", vertex.volumetric_rate());
                        text = std::string(buff);
                    }
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                  });
                append_table_row(_u8L("Fan speed") + " (" + _u8L("%") + ")", [&vertex, &buff]() {
                    std::string text;
                    if (vertex.is_extrusion()) {
                        sprintf(buff, "%.0f", vertex.fan_speed);
                        text = std::string(buff);
                    }
                    else
                        text = _u8L("N/A");
                    ImGuiPureWrap::text(text);
                });
                append_table_row(_u8L("Temperature") + " (" + _u8L("°C") + ")", [&vertex, &buff]() {
                    sprintf(buff, "%.0f", vertex.temperature);
                    ImGuiPureWrap::text(std::string(buff));
                });
                append_table_row(_u8L("Time"), [viewer, &vertex, &buff, vertex_id]() {
                    const float estimated_time = viewer->get_estimated_time_at(vertex_id);
                    sprintf(buff, "%s (%.3fs)", get_time_dhms(estimated_time).c_str(), vertex.times[static_cast<size_t>(viewer->get_time_mode())]);
                    const std::string text = std::string(buff);
                    ImGuiPureWrap::text(text);
                });

                ImGui::EndTable();
            }

#if ENABLE_ACTUAL_SPEED_DEBUG
            if (vertex.is_extrusion() || vertex.is_travel() || vertex.is_wipe()) {
                ImGui::Spacing();
                ImGuiPureWrap::text(_u8L("Actual speed profile"));
                ImGui::SameLine();
                static bool table_shown = false;
                if (ImGuiPureWrap::button(table_shown ? _u8L("Hide table") : _u8L("Show table")))
                    table_shown = !table_shown;
                ImGui::Separator();
                const int hover_id = m_actual_speed_imgui_widget.plot("##ActualSpeedProfile", { -1.0f, 150.0f });
                if (table_shown) {
                    static float table_wnd_height = 0.0f;
                    const ImVec2 wnd_size = ImGui::GetWindowSize();
                    ImGuiPureWrap::set_next_window_pos(ImGui::GetWindowPos().x + wnd_size.x, viewport.Size.y - preview->get_moves_slider_height(), ImGuiCond_Always, 0.0f, 1.0f);
                    ImGui::SetNextWindowSizeConstraints({ 0.0f, 0.0f }, { -1.0f, wnd_size.y });
                    ImGuiPureWrap::begin(std::string("ToolPositionTableWnd"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
                    if (ImGui::BeginTable("ToolPositionTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
                        char buff[1024];
                        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                        ImGui::TableSetupColumn("Position (mm)");
                        ImGui::TableSetupColumn("Speed (mm/s)");
                        ImGui::TableHeadersRow();
                        int counter = 0;
                        for (const ActualSpeedImguiWidget::Item& item : m_actual_speed_imgui_widget.data) {
                            const bool highlight = hover_id >= 0 && (counter == hover_id || counter == hover_id + 1);
                            if (highlight && counter == hover_id)
                                ImGui::SetScrollHereY();
                            ImGui::TableNextRow();
                            const ImU32 row_bg_color = ImGui::GetColorU32(item.internal ? ImVec4(0.0f, 0.0f, 0.5f, 0.25f) : ImVec4(0.5f, 0.5f, 0.5f, 0.25f));
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_bg_color);
                            ImGui::TableSetColumnIndex(0);
                            sprintf(buff, "%.3f", item.pos);
                            ImGuiPureWrap::text_colored(highlight ? ImGuiPureWrap::COL_ORANGE_LIGHT : ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()), buff);
                            ImGui::TableSetColumnIndex(1);
                            sprintf(buff, "%.1f", item.speed);
                            ImGuiPureWrap::text_colored(highlight ? ImGuiPureWrap::COL_ORANGE_LIGHT : ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()), buff);
                            ++counter;
                        }
                        ImGui::EndTable();
                    }
                    const float curr_table_wnd_height = ImGui::GetWindowHeight();
                    if (table_wnd_height != curr_table_wnd_height) {
                        table_wnd_height = curr_table_wnd_height;
                        // require extra frame to hide the table scroll bar (bug in imgui)
                        imgui.set_requires_extra_frame();
                    }
                    ImGuiPureWrap::end();
                }
            }
#endif // ENABLE_ACTUAL_SPEED_DEBUG
        }

        // force extra frame to automatically update window size
        const float width = ImGui::GetWindowWidth();
        const size_t length = strlen(buf);
        if (width != last_window_width || length != last_text_length) {
            last_window_width = width;
            last_text_length = length;
            imgui.set_requires_extra_frame();
        }

        ImGuiPureWrap::end();
        ImGui::PopStyleVar();
    }
    else {
        ImGuiWrapper& imgui = *wxGetApp().imgui();
        const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
        ImGuiPureWrap::set_next_window_pos(0.5f * static_cast<float>(cnv_size.get_width()), static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 0.5f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::SetNextWindowBgAlpha(0.25f);
        ImGuiPureWrap::begin(std::string("ToolPosition"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Tool position") + ":");
        ImGui::SameLine();
        char buf[1024];
        const Vec3f position = m_world_position + m_world_offset + m_z_offset * Vec3f::UnitZ();
        sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", position.x(), position.y(), position.z());
        ImGuiPureWrap::text(std::string(buf));

        // force extra frame to automatically update window size
        const float width = ImGui::GetWindowWidth();
        const size_t length = strlen(buf);
        if (width != last_window_width || length != last_text_length) {
            last_window_width = width;
            last_text_length = length;
            imgui.set_requires_extra_frame();
        }

        ImGuiPureWrap::end();
        ImGui::PopStyleVar();
    }
}

void GCodeViewer::SequentialView::GCodeWindow::load_gcode(const GCodeProcessorResult& gcode_result)
{
    m_filename = gcode_result.filename;
    m_is_binary_file = gcode_result.is_binary_file;
    m_lines_ends = gcode_result.lines_ends;
}

void GCodeViewer::SequentialView::GCodeWindow::add_gcode_line_to_lines_cache(const std::string& src)
{
    std::string command;
    std::string parameters;
    std::string comment;

    // extract comment
    std::vector<std::string> tokens;
    boost::split(tokens, src, boost::is_any_of(";"), boost::token_compress_on);
    command = tokens.front();
    if (tokens.size() > 1)
        comment = ";" + tokens.back();

    // extract gcode command and parameters
    if (!command.empty()) {
        boost::split(tokens, command, boost::is_any_of(" "), boost::token_compress_on);
        command = tokens.front();
        if (tokens.size() > 1) {
            for (size_t i = 1; i < tokens.size(); ++i) {
                parameters += " " + tokens[i];
            }
        }
    }

    m_lines_cache.push_back({ command, parameters, comment });
}

void GCodeViewer::SequentialView::GCodeWindow::render(float top, float bottom, size_t curr_line_id)
{
    auto update_lines_ascii = [this]() {
        m_lines_cache.clear();
        m_lines_cache.reserve(m_cache_range.size());
        const std::vector<size_t>& lines_ends = m_lines_ends.front();
        FILE* file = boost::nowide::fopen(m_filename.c_str(), "rb");
        if (file != nullptr) {
            for (size_t id = *m_cache_range.min; id <= *m_cache_range.max; ++id) {
                assert(id > 0);
                // read line from file
                const size_t begin = id == 1 ? 0 : lines_ends[id - 2];
                const size_t len = lines_ends[id - 1] - begin;
                std::string gline(len, '\0');
                fseek(file, begin, SEEK_SET);
                const size_t rsize = fread((void*)gline.data(), 1, len, file);
                if (ferror(file) || rsize != len) {
                    m_lines_cache.clear();
                    break;
                }

                add_gcode_line_to_lines_cache(gline);
            }
            fclose(file);
        }
    };

    auto update_lines_binary = [this]() {
        m_lines_cache.clear();
        m_lines_cache.reserve(m_cache_range.size());

        size_t cumulative_lines_count = 0;
        std::vector<size_t> cumulative_lines_counts;
        cumulative_lines_counts.reserve(m_lines_ends.size());
        for (size_t i = 0; i < m_lines_ends.size(); ++i) {
            cumulative_lines_count += m_lines_ends[i].size();
            cumulative_lines_counts.emplace_back(cumulative_lines_count);
        }

        size_t first_block_id = 0;
        for (size_t i = 0; i < cumulative_lines_counts.size(); ++i) {
            if (*m_cache_range.min <= cumulative_lines_counts[i]) {
                first_block_id = i;
                break;
            }
        }
        size_t last_block_id = first_block_id;
        for (size_t i = last_block_id; i < cumulative_lines_counts.size(); ++i) {
            if (*m_cache_range.max <= cumulative_lines_counts[i]) {
                last_block_id = i;
                break;
            }
        }
        assert(last_block_id >= first_block_id);

        FilePtr file(boost::nowide::fopen(m_filename.c_str(), "rb"));
        if (file.f != nullptr) {
            fseek(file.f, 0, SEEK_END);
            const long file_size = ftell(file.f);
            rewind(file.f);

            // read file header
            using namespace bgcode::core;
            using namespace bgcode::binarize;
            FileHeader file_header;
            EResult res = read_header(*file.f, file_header, nullptr);
            if (res == EResult::Success) {
                // search first GCode block
                BlockHeader block_header;
                res = read_next_block_header(*file.f, file_header, block_header, EBlockType::GCode, nullptr, 0);
                if (res == EResult::Success) {
                    for (size_t i = 0; i < first_block_id; ++i) {
                        skip_block(*file.f, file_header, block_header);
                        res = read_next_block_header(*file.f, file_header, block_header, nullptr, 0);
                        if (res != EResult::Success || block_header.type != (uint16_t)EBlockType::GCode) {
                            m_lines_cache.clear();
                            return;
                        }
                    }

                    for (size_t i = first_block_id; i <= last_block_id; ++i) {
                        GCodeBlock block;
                        res = block.read_data(*file.f, file_header, block_header);
                        if (res != EResult::Success) {
                            m_lines_cache.clear();
                            return;
                        }

                        const size_t ref_id = (i == 0) ? 0 : i - 1;
                        const size_t first_line_id = (i == 0) ? *m_cache_range.min :
                            (*m_cache_range.min > cumulative_lines_counts[ref_id]) ? *m_cache_range.min - cumulative_lines_counts[ref_id] : 1;
                        const size_t last_line_id = (*m_cache_range.max <= cumulative_lines_counts[i]) ?
                            (i == 0) ? *m_cache_range.max : *m_cache_range.max - cumulative_lines_counts[ref_id] : m_lines_ends[i].size();
                        assert(last_line_id >= first_line_id);

                        for (size_t j = first_line_id; j <= last_line_id; ++j) {
                            const size_t begin = (j == 1) ? 0 : m_lines_ends[i][j - 2];
                            const size_t end = m_lines_ends[i][j - 1];
                            std::string gline;
                            gline.insert(gline.end(), block.raw_data.begin() + begin, block.raw_data.begin() + end);
                            add_gcode_line_to_lines_cache(gline);
                        }

                        if (ftell(file.f) == file_size)
                            break;

                        res = read_next_block_header(*file.f, file_header, block_header, nullptr, 0);
                        if (res != EResult::Success || block_header.type != (uint16_t)EBlockType::GCode) {
                            m_lines_cache.clear();
                            return;
                        }
                    }
                }
            }
        }
        assert(m_lines_cache.size() == m_cache_range.size());
    };

    static const ImVec4 LINE_NUMBER_COLOR = ImGuiPureWrap::COL_ORANGE_LIGHT;
    static const ImVec4 SELECTION_RECT_COLOR = ImGuiPureWrap::COL_ORANGE_DARK;
    static const ImVec4 COMMAND_COLOR    = { 0.8f, 0.8f, 0.0f, 1.0f };
    static const ImVec4 PARAMETERS_COLOR = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const ImVec4 COMMENT_COLOR    = { 0.7f, 0.7f, 0.7f, 1.0f };
    static const ImVec4 ELLIPSIS_COLOR   = { 0.0f, 0.7f, 0.0f, 1.0f };

    if (!m_visible || m_filename.empty() || m_lines_ends.empty() || curr_line_id == 0)
        return;

    // window height
    const float wnd_height = bottom - top;

    // number of visible lines
    const float text_height = ImGui::CalcTextSize("0").y;
    const ImGuiStyle& style = ImGui::GetStyle();
    const size_t visible_lines_count = static_cast<size_t>((wnd_height - 2.0f * style.WindowPadding.y + style.ItemSpacing.y) / (text_height + style.ItemSpacing.y));

    if (visible_lines_count == 0)
        return;

    if (m_lines_ends.empty() || m_lines_ends.front().empty())
        return;

    auto resize_range = [&](Range& range, size_t lines_count) {
        const size_t half_lines_count = lines_count / 2;
        range.min = (curr_line_id > half_lines_count) ? curr_line_id - half_lines_count : 1;
        range.max = *range.min + lines_count - 1;
        size_t lines_ends_count = 0;
        for (const auto& le : m_lines_ends) {
            lines_ends_count += le.size();
        }
        if (*range.max >= lines_ends_count) {
            range.max = lines_ends_count - 1;
            range.min = *range.max - lines_count + 1;
        }
    };

    // visible range
    Range visible_range;
    resize_range(visible_range, visible_lines_count);

    // update cache if needed
    if (m_cache_range.empty() || !m_cache_range.contains(visible_range)) {
        resize_range(m_cache_range, 4 * visible_range.size());
        if (m_is_binary_file)
            update_lines_binary();
        else
            update_lines_ascii();
    }

    if (m_lines_cache.empty())
        return;

    // line number's column width
    const float id_width = ImGui::CalcTextSize(std::to_string(*visible_range.max).c_str()).x;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto add_item_to_line = [](const std::string& txt, const ImVec4& color, float spacing, size_t& current_length) {
        static const size_t LENGTH_THRESHOLD = 60;

        if (txt.empty())
            return false;

        std::string out_text = txt;
        bool reduced = false;
        if (current_length + out_text.length() > LENGTH_THRESHOLD) {
            out_text = out_text.substr(0, LENGTH_THRESHOLD - current_length);
            reduced = true;
        }

        current_length += out_text.length();

        ImGui::SameLine(0.0f, spacing);
        ImGuiPureWrap::text_colored(color, out_text);
        if (reduced) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGuiPureWrap::text_colored(ELLIPSIS_COLOR, "...");
        }

        return reduced;
    };

    ImGuiPureWrap::set_next_window_pos(0.0f, top, ImGuiCond_Always, 0.0f, 0.0f);
    ImGuiPureWrap::set_next_window_size(0.0f, wnd_height, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGuiPureWrap::begin(std::string("G-code"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);

    // center the text in the window by pushing down the first line
    const float f_lines_count = static_cast<float>(visible_lines_count);
    ImGui::SetCursorPosY(0.5f * (wnd_height - f_lines_count * text_height - (f_lines_count - 1.0f) * style.ItemSpacing.y));

    // render text lines
    size_t max_line_length = 0;
    for (size_t id = *visible_range.min; id <= *visible_range.max; ++id) {
        const Line& line = m_lines_cache[id - *m_cache_range.min];

        // rect around the current selected line
        if (id == curr_line_id) {
            const float pos_y = ImGui::GetCursorScreenPos().y;
            const float half_ItemSpacing_y = 0.5f * style.ItemSpacing.y;
            const float half_padding_x = 0.5f * style.WindowPadding.x;
            ImGui::GetWindowDrawList()->AddRect({ half_padding_x, pos_y - half_ItemSpacing_y },
                { ImGui::GetCurrentWindow()->Size.x - half_padding_x, pos_y + text_height + half_ItemSpacing_y },
                ImGui::GetColorU32(SELECTION_RECT_COLOR));
        }

        const std::string id_str = std::to_string(id);
        // spacer to right align text
        ImGui::Dummy({ id_width - ImGui::CalcTextSize(id_str.c_str()).x, text_height });

        size_t line_length = 0;
        // render line number
        bool stop_adding = add_item_to_line(id_str, LINE_NUMBER_COLOR, 0.0f, line_length);
        if (!stop_adding && !line.command.empty())
            // render command
            stop_adding = add_item_to_line(line.command, COMMAND_COLOR, -1.0f, line_length);
        if (!stop_adding && !line.parameters.empty())
            // render parameters
            stop_adding = add_item_to_line(line.parameters, PARAMETERS_COLOR, 0.0f, line_length);
        if (!stop_adding && !line.comment.empty())
            // render comment
            stop_adding = add_item_to_line(line.comment, COMMENT_COLOR, line.command.empty() ? -1.0f : 0.0f, line_length);

        max_line_length = std::max(max_line_length, line_length);
    }

    ImGuiPureWrap::end();
    ImGui::PopStyleVar();

    // request an extra frame if window's width changed
    if (m_max_line_length != max_line_length) {
        m_max_line_length = max_line_length;
        imgui.set_requires_extra_frame();
    }
}

void GCodeViewer::SequentialView::render(float legend_height, const libvgcode::Viewer* viewer, uint32_t gcode_id)
{
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    if (viewer == nullptr)
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    marker.render();
    marker.render_position_window(viewer);
    float bottom = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height();
    if (wxGetApp().is_editor())
        bottom -= wxGetApp().plater()->get_view_toolbar().get_height();
    gcode_window.render(legend_height, bottom, gcode_id);
}

GCodeViewer::GCodeViewer()
{
    m_shells.volumes.set_use_raycasters(false);
}

void GCodeViewer::init()
{
    if (m_gl_data_initialized)
        return;

    // initializes tool marker
    m_sequential_view.marker.init();

    m_gl_data_initialized = true;

    try
    {
        m_viewer.init(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        glcheck();
    }
    catch (const std::exception& e)
    {
        MessageDialog msg_dlg(wxGetApp().plater(), e.what(), _L("Error"), wxICON_ERROR | wxOK);
        msg_dlg.ShowModal();
    }
}

void GCodeViewer::load_as_gcode(const GCodeProcessorResult& gcode_result, const Print& print, const std::vector<std::string>& str_tool_colors,
    const std::vector<std::string>& str_color_print_colors)
{
    m_loaded_as_preview = false;

    const bool current_top_layer_only = m_viewer.is_top_layer_only_view_range();
    const bool required_top_layer_only = get_app_config()->get_bool("seq_top_layer_only");
    if (current_top_layer_only != required_top_layer_only)
        m_viewer.toggle_top_layer_only_view_range();

    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id && ! s_beds_switched_since_last_gcode_load && wxGetApp().is_editor() && ! s_reload_preview_after_switching_beds) {
        // collect tool colors
        libvgcode::Palette tools_colors;
        tools_colors.reserve(str_tool_colors.size());
        for (const std::string& color : str_tool_colors) {
            tools_colors.emplace_back(libvgcode::convert(color));
        }
        m_viewer.set_tool_colors(tools_colors);

        // collect color print colors
        libvgcode::Palette color_print_colors;
        const std::vector<std::string>& str_colors = str_color_print_colors.empty() ? str_tool_colors : str_color_print_colors;
        for (const std::string& color : str_colors) {
            color_print_colors.emplace_back(libvgcode::convert(color));
        }
        m_viewer.set_color_print_colors(color_print_colors);
        return;
    }

    m_last_result_id = gcode_result.id;
    s_beds_switched_since_last_gcode_load = false;

    // release gpu memory, if used
    reset();

    // convert data from PrusaSlicer format to libvgcode format
    libvgcode::GCodeInputData data = libvgcode::convert(gcode_result, str_tool_colors, str_color_print_colors, m_viewer);

//#define ENABLE_DATA_EXPORT 1
//#if ENABLE_DATA_EXPORT
//    auto extrusion_role_to_string = [](libvgcode::EGCodeExtrusionRole role) {
//        switch (role) {
//        case libvgcode::EGCodeExtrusionRole::None:                     { return "EGCodeExtrusionRole::None"; }
//        case libvgcode::EGCodeExtrusionRole::Perimeter:                { return "EGCodeExtrusionRole::Perimeter"; }
//        case libvgcode::EGCodeExtrusionRole::ExternalPerimeter:        { return "EGCodeExtrusionRole::ExternalPerimeter"; }
//        case libvgcode::EGCodeExtrusionRole::OverhangPerimeter:        { return "EGCodeExtrusionRole::OverhangPerimeter"; }
//        case libvgcode::EGCodeExtrusionRole::InternalInfill:           { return "EGCodeExtrusionRole::InternalInfill"; }
//        case libvgcode::EGCodeExtrusionRole::SolidInfill:              { return "EGCodeExtrusionRole::SolidInfill"; }
//        case libvgcode::EGCodeExtrusionRole::TopSolidInfill:           { return "EGCodeExtrusionRole::TopSolidInfill"; }
//        case libvgcode::EGCodeExtrusionRole::Ironing:                  { return "EGCodeExtrusionRole::Ironing"; }
//        case libvgcode::EGCodeExtrusionRole::BridgeInfill:             { return "EGCodeExtrusionRole::BridgeInfill"; }
//        case libvgcode::EGCodeExtrusionRole::GapFill:                  { return "EGCodeExtrusionRole::GapFill"; }
//        case libvgcode::EGCodeExtrusionRole::Skirt:                    { return "EGCodeExtrusionRole::Skirt"; }
//        case libvgcode::EGCodeExtrusionRole::SupportMaterial:          { return "EGCodeExtrusionRole::SupportMaterial"; }
//        case libvgcode::EGCodeExtrusionRole::SupportMaterialInterface: { return "EGCodeExtrusionRole::SupportMaterialInterface"; }
//        case libvgcode::EGCodeExtrusionRole::WipeTower:                { return "EGCodeExtrusionRole::WipeTower"; }
//        case libvgcode::EGCodeExtrusionRole::Custom:                   { return "EGCodeExtrusionRole::Custom"; }
//        case libvgcode::EGCodeExtrusionRole::COUNT:                    { return "EGCodeExtrusionRole::COUNT"; }
//        }
//    };
//
//    auto move_type_to_string = [](libvgcode::EMoveType type) {
//        switch (type) {
//        case libvgcode::EMoveType::Noop:        { return "EMoveType::Noop"; }
//        case libvgcode::EMoveType::Retract:     { return "EMoveType::Retract"; }
//        case libvgcode::EMoveType::Unretract:   { return "EMoveType::Unretract"; }
//        case libvgcode::EMoveType::Seam:        { return "EMoveType::Seam"; }
//        case libvgcode::EMoveType::ToolChange:  { return "EMoveType::ToolChange"; }
//        case libvgcode::EMoveType::ColorChange: { return "EMoveType::ColorChange"; }
//        case libvgcode::EMoveType::PausePrint:  { return "EMoveType::PausePrint"; }
//        case libvgcode::EMoveType::CustomGCode: { return "EMoveType::CustomGCode"; }
//        case libvgcode::EMoveType::Travel:      { return "EMoveType::Travel"; }
//        case libvgcode::EMoveType::Wipe:        { return "EMoveType::Wipe"; }
//        case libvgcode::EMoveType::Extrude:     { return "EMoveType::Extrude"; }
//        case libvgcode::EMoveType::COUNT:       { return "EMoveType::COUNT"; }
//        }
//    };
//
//    FilePtr out{ boost::nowide::fopen("C:/prusa/slicer/test_output/spe1872/test.data", "wb") };
//    if (out.f != nullptr) {
//        const uint32_t vertices_count = static_cast<uint32_t>(data.vertices.size());
//        fwrite((void*)&vertices_count, 1, sizeof(uint32_t), out.f);
//        for (const libvgcode::PathVertex& v : data.vertices) {
//            fwrite((void*)&v.position[0], 1, sizeof(float), out.f);
//            fwrite((void*)&v.position[1], 1, sizeof(float), out.f);
//            fwrite((void*)&v.position[2], 1, sizeof(float), out.f);
//            fwrite((void*)&v.height, 1, sizeof(float), out.f);
//            fwrite((void*)&v.width, 1, sizeof(float), out.f);
//            fwrite((void*)&v.feedrate, 1, sizeof(float), out.f);
//            fwrite((void*)&v.actual_feedrate, 1, sizeof(float), out.f);
//            fwrite((void*)&v.mm3_per_mm, 1, sizeof(float), out.f);
//            fwrite((void*)&v.fan_speed, 1, sizeof(float), out.f);
//            fwrite((void*)&v.temperature, 1, sizeof(float), out.f);
//            fwrite((void*)&v.role, 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&v.type, 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&v.gcode_id, 1, sizeof(uint32_t), out.f);
//            fwrite((void*)&v.layer_id, 1, sizeof(uint32_t), out.f);
//            fwrite((void*)&v.extruder_id, 1, sizeof(uint32_t), out.f);
//            fwrite((void*)&v.color_id, 1, sizeof(uint32_t), out.f);
//            fwrite((void*)&v.times[0], 1, sizeof(float), out.f);
//            fwrite((void*)&v.times[1], 1, sizeof(float), out.f);
//#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
//            const float weight = v.weight;
//#else
//            const float weight = 0.0f;
//#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
//            fwrite((void*)&weight, 1, sizeof(float), out.f);
//        }
//
//        const uint8_t spiral_vase_mode = data.spiral_vase_mode ? 1 : 0;
//        fwrite((void*)&spiral_vase_mode, 1, sizeof(uint8_t), out.f);
//
//        const uint32_t tool_colors_count = static_cast<uint32_t>(data.tools_colors.size());
//        fwrite((void*)&tool_colors_count, 1, sizeof(uint32_t), out.f);
//        for (const libvgcode::Color& c : data.tools_colors) {
//            fwrite((void*)&c[0], 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&c[1], 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&c[2], 1, sizeof(uint8_t), out.f);
//        }
//
//        const uint32_t color_print_colors_count = static_cast<uint32_t>(data.color_print_colors.size());
//        fwrite((void*)&color_print_colors_count, 1, sizeof(uint32_t), out.f);
//        for (const libvgcode::Color& c : data.color_print_colors) {
//            fwrite((void*)&c[0], 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&c[1], 1, sizeof(uint8_t), out.f);
//            fwrite((void*)&c[2], 1, sizeof(uint8_t), out.f);
//        }
//    }
//#endif // ENABLE_DATA_EXPORT

    // send data to the viewer
    m_viewer.reset_default_extrusion_roles_colors();
    m_viewer.load(std::move(data));

#if !VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    const size_t vertices_count = m_viewer.get_vertices_count();
    m_cog.reset();
    for (size_t i = 1; i < vertices_count; ++i) {
        const libvgcode::PathVertex& curr = m_viewer.get_vertex_at(i);
        if (curr.type == libvgcode::EMoveType::Extrude &&
            curr.role != libvgcode::EGCodeExtrusionRole::Skirt &&
            curr.role != libvgcode::EGCodeExtrusionRole::SupportMaterial &&
            curr.role != libvgcode::EGCodeExtrusionRole::SupportMaterialInterface &&
            curr.role != libvgcode::EGCodeExtrusionRole::WipeTower &&
            curr.role != libvgcode::EGCodeExtrusionRole::Custom) {
            const Vec3d curr_pos = libvgcode::convert(curr.position).cast<double>();
            const Vec3d prev_pos = libvgcode::convert(m_viewer.get_vertex_at(i - 1).position).cast<double>();
            m_cog.add_segment(curr_pos, prev_pos, gcode_result.filament_densities[curr.extruder_id] * curr.mm3_per_mm * (curr_pos - prev_pos).norm());
        }
    }
#endif // !VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    const libvgcode::AABox bbox = wxGetApp().is_gcode_viewer() ?
        m_viewer.get_bounding_box() :
        m_viewer.get_extrusion_bounding_box({
            libvgcode::EGCodeExtrusionRole::Perimeter, libvgcode::EGCodeExtrusionRole::ExternalPerimeter, libvgcode::EGCodeExtrusionRole::OverhangPerimeter,
            libvgcode::EGCodeExtrusionRole::InternalInfill, libvgcode::EGCodeExtrusionRole::SolidInfill, libvgcode::EGCodeExtrusionRole::TopSolidInfill,
            libvgcode::EGCodeExtrusionRole::Ironing, libvgcode::EGCodeExtrusionRole::BridgeInfill, libvgcode::EGCodeExtrusionRole::GapFill,
            libvgcode::EGCodeExtrusionRole::Skirt, libvgcode::EGCodeExtrusionRole::SupportMaterial, libvgcode::EGCodeExtrusionRole::SupportMaterialInterface,
            libvgcode::EGCodeExtrusionRole::WipeTower
        });
    m_paths_bounding_box = BoundingBoxf3(libvgcode::convert(bbox[0]).cast<double>(), libvgcode::convert(bbox[1]).cast<double>());

    if (wxGetApp().is_editor()) {
        m_contained_in_bed = wxGetApp().plater()->build_volume().all_paths_inside(gcode_result, m_paths_bounding_box);
        if (!m_contained_in_bed) {
            s_print_statuses[s_multiple_beds.get_active_bed()] = PrintStatus::toolpath_outside;
        }
    }

    m_extruders_count = gcode_result.extruders_count;
    m_sequential_view.gcode_window.load_gcode(gcode_result);

    m_custom_gcode_per_print_z = gcode_result.custom_gcode_per_print_z;

    m_max_print_height = gcode_result.max_print_height;
    m_z_offset = gcode_result.z_offset;

    load_wipetower_shell(print);

    if (m_viewer.get_layers_count() == 0)
        return;

    m_settings_ids = gcode_result.settings_ids;
    m_filament_diameters = gcode_result.filament_diameters;
    m_filament_densities = gcode_result.filament_densities;

    if (!wxGetApp().is_editor()) {
        Pointfs bed_shape;
        std::string texture;
        std::string model;

        if (!gcode_result.bed_shape.empty()) {
            // bed shape detected in the gcode
            bed_shape = gcode_result.bed_shape;
            const auto bundle = wxGetApp().preset_bundle;
            if (bundle != nullptr && !m_settings_ids.printer.empty()) {
                const Preset* preset = bundle->printers.find_preset(m_settings_ids.printer);
                if (preset != nullptr) {
                    model = PresetUtils::system_printer_bed_model(*preset);
                    texture = PresetUtils::system_printer_bed_texture(*preset);
                }
            }
        }
        else {
            // adjust printbed size in dependence of toolpaths bbox
            const double margin = 10.0;
            const Vec2d min(m_paths_bounding_box.min.x() - margin, m_paths_bounding_box.min.y() - margin);
            const Vec2d max(m_paths_bounding_box.max.x() + margin, m_paths_bounding_box.max.y() + margin);

            const Vec2d size = max - min;
            bed_shape = {
                { min.x(), min.y() },
                { max.x(), min.y() },
                { max.x(), min.y() + 0.442265 * size.y()},
                { max.x() - 10.0, min.y() + 0.4711325 * size.y()},
                { max.x() + 10.0, min.y() + 0.5288675 * size.y()},
                { max.x(), min.y() + 0.557735 * size.y()},
                { max.x(), max.y() },
                { min.x() + 0.557735 * size.x(), max.y()},
                { min.x() + 0.5288675 * size.x(), max.y() - 10.0},
                { min.x() + 0.4711325 * size.x(), max.y() + 10.0},
                { min.x() + 0.442265 * size.x(), max.y()},
                { min.x(), max.y() } };
        }

        wxGetApp().plater()->set_bed_shape(bed_shape, gcode_result.max_print_height, texture, model, gcode_result.bed_shape.empty());
    }

    m_print_statistics = gcode_result.print_statistics;

    PrintEstimatedStatistics::ETimeMode time_mode = convert(m_viewer.get_time_mode());
    if (m_viewer.get_time_mode() != libvgcode::ETimeMode::Normal) {
        const float time = m_print_statistics.modes[static_cast<size_t>(time_mode)].time;
        if (time == 0.0f ||
            short_time(get_time_dhms(time)) == short_time(get_time_dhms(m_print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time)))
            m_viewer.set_time_mode(libvgcode::convert(PrintEstimatedStatistics::ETimeMode::Normal));
    }

    m_conflict_result = gcode_result.conflict_result;
    if (m_conflict_result.has_value())
        m_conflict_result->layer = m_viewer.get_layer_id_at(static_cast<float>(m_conflict_result->_height));
}

void GCodeViewer::load_as_preview(libvgcode::GCodeInputData&& data)
{
    m_loaded_as_preview = true;

    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::Skirt,                    { 127, 255, 127 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::ExternalPerimeter,        { 255, 255, 0 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::SupportMaterial,          { 127, 255, 127 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::SupportMaterialInterface, { 127, 255, 127 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::InternalInfill,           { 255, 127, 127 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::SolidInfill,              { 255, 127, 127 });
    m_viewer.set_extrusion_role_color(libvgcode::EGCodeExtrusionRole::WipeTower,                { 127, 255, 127 });
    m_viewer.load(std::move(data));

    const libvgcode::AABox bbox = m_viewer.get_extrusion_bounding_box();
    const BoundingBoxf3 paths_bounding_box(libvgcode::convert(bbox[0]).cast<double>(), libvgcode::convert(bbox[1]).cast<double>());
    m_contained_in_bed = wxGetApp().plater()->build_volume().all_paths_inside(GCodeProcessorResult(), paths_bounding_box);
    if (!m_contained_in_bed) {
        s_print_statuses[s_multiple_beds.get_active_bed()] = PrintStatus::toolpath_outside;
    }
}

void GCodeViewer::update_shells_color_by_extruder(const DynamicPrintConfig* config)
{
    if (config != nullptr)
        m_shells.volumes.update_colors_by_extruder(config);
}

void GCodeViewer::reset()
{
    m_viewer.reset();

    m_paths_bounding_box.reset();
    m_max_bounding_box.reset();
    m_max_print_height = 0.0f;
    m_z_offset = 0.0f;
    m_filament_diameters = std::vector<float>();
    m_filament_densities = std::vector<float>();
    m_extruders_count = 0;
    m_print_statistics.reset();
    m_custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    m_sequential_view.gcode_window.reset();
    m_contained_in_bed = true;
    m_legend_resizer.reset();
}

void GCodeViewer::render()
{
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_shells();

    if (m_viewer.get_extrusion_roles().empty())
        return;

    render_toolpaths();

    float legend_height = 0.0f;
    if (m_viewer.get_layers_count() > 0) {
        render_legend(legend_height);
        if (m_viewer.get_view_enabled_range()[1] != m_viewer.get_view_visible_range()[1]) {
            const libvgcode::PathVertex& curr_vertex = m_viewer.get_current_vertex();
            m_sequential_view.marker.set_world_position(libvgcode::convert(curr_vertex.position));
            m_sequential_view.marker.set_z_offset(m_z_offset);
            m_sequential_view.render(legend_height, &m_viewer, curr_vertex.gcode_id);
        }
    }

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    if (is_legend_shown()) {
        ImGuiWrapper& imgui = *Slic3r::GUI::wxGetApp().imgui();
        const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
        ImGuiPureWrap::set_next_window_pos(static_cast<float>(cnv_size.get_width()), static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 1.0f, 1.0f);
        ImGuiPureWrap::begin(std::string("LibVGCode Viewer Controller"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

        ImGuiPureWrap::checkbox("Cog marker fixed screen size", m_cog_marker_fixed_screen_size);
        if (ImGui::BeginTable("Cog", 2)) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Cog marker size");
            ImGui::TableSetColumnIndex(1);
            imgui.slider_float("##CogSize", &m_cog_marker_size, 1.0f, 5.0f);

            ImGui::EndTable();
        }

        ImGuiPureWrap::checkbox("Tool marker fixed screen size", m_tool_marker_fixed_screen_size);
        if (ImGui::BeginTable("Tool", 2)) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Tool marker size");
            ImGui::TableSetColumnIndex(1);
            imgui.slider_float("##ToolSize", &m_tool_marker_size, 1.0f, 5.0f);

            ImGui::EndTable();
        }

        ImGuiPureWrap::end();
    }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
}

bool GCodeViewer::can_export_toolpaths() const
{
    const libvgcode::Interval& visible_range = m_viewer.get_view_visible_range();
    for (size_t i = visible_range[0]; i <= visible_range[1]; ++i) {
        if (m_viewer.get_vertex_at(i).is_extrusion())
            return true;
    }
    return false;
}

void GCodeViewer::update_sequential_view_current(unsigned int first, unsigned int last)
{
    m_viewer.set_view_visible_range(static_cast<uint32_t>(first), static_cast<uint32_t>(last));
    const libvgcode::Interval& enabled_range = m_viewer.get_view_enabled_range();
    wxGetApp().plater()->enable_preview_moves_slider(enabled_range[1] > enabled_range[0]);

#if ENABLE_ACTUAL_SPEED_DEBUG
    if (enabled_range[1] != m_viewer.get_view_visible_range()[1]) {
        const libvgcode::PathVertex& curr_vertex = m_viewer.get_current_vertex();
        if (curr_vertex.is_extrusion() || curr_vertex.is_travel() || curr_vertex.is_wipe() ||
            curr_vertex.type == libvgcode::EMoveType::Seam) {
            const libvgcode::ColorRange& color_range = m_viewer.get_color_range(libvgcode::EViewType::ActualSpeed);
            const std::array<float, 2>& interval = color_range.get_range();
            const size_t vertices_count = m_viewer.get_vertices_count();
            std::vector<SequentialView::ActualSpeedImguiWidget::Item> actual_speed_data;
            // collect vertices sharing the same gcode_id
            const size_t curr_id = m_viewer.get_current_vertex_id();
            size_t start_id = curr_id;
            while (start_id > 0) {
                --start_id;
                if (curr_vertex.gcode_id != m_viewer.get_vertex_at(start_id).gcode_id)
                    break;
            }
            size_t end_id = curr_id;
            while (end_id < vertices_count - 1) {
                ++end_id;
                if (curr_vertex.gcode_id != m_viewer.get_vertex_at(end_id).gcode_id)
                    break;
            }

            if (m_viewer.get_vertex_at(end_id - 1).type == libvgcode::convert(EMoveType::Seam))
                --end_id;

            assert(end_id - start_id >= 2);

            float total_len = 0.0f;
            for (size_t i = start_id; i < end_id; ++i) {
                const libvgcode::PathVertex& v = m_viewer.get_vertex_at(i);
                const float len = (i > start_id) ?
                    (libvgcode::convert(v.position) - libvgcode::convert(m_viewer.get_vertex_at(i - 1).position)).norm() : 0.0f;
                total_len += len;
                if (i == start_id || len > EPSILON)
                    actual_speed_data.push_back({ total_len, v.actual_feedrate, v.times[0] == 0.0f });
            }

            std::vector<std::pair<float, ColorRGBA>> levels;
            const std::vector<float> values = color_range.get_values();
            for (float value : values) {
                levels.push_back(std::make_pair(value, libvgcode::convert(color_range.get_color_at(value))));
                levels.back().second.a(0.5f);
            }

            m_sequential_view.marker.set_actual_speed_data(actual_speed_data);
            m_sequential_view.marker.set_actual_speed_y_range(std::make_pair(interval[0], interval[1]));
            m_sequential_view.marker.set_actual_speed_levels(levels);
        }
    }
#endif // ENABLE_ACTUAL_SPEED_DEBUG
}

void GCodeViewer::set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range)
{
    m_viewer.set_layers_view_range(static_cast<uint32_t>(layers_z_range[0]), static_cast<uint32_t>(layers_z_range[1]));
    wxGetApp().plater()->update_preview_moves_slider();
}

class ToolpathsObjExporter
{
public:
    explicit ToolpathsObjExporter(const libvgcode::Viewer& viewer)
    : m_viewer(viewer) {
    }

    void export_to(const std::string& filename) {
        CNumericLocalesSetter locales_setter;

        // open geometry file
        FilePtr f_geo = boost::nowide::fopen(filename.c_str(), "w");
        if (f_geo.f == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "ToolpathsObjExporter: Couldn't open " << filename << " for writing";
            return;
        }

        boost::filesystem::path materials_filename(filename);
        materials_filename.replace_extension("mtl");

        // write header to geometry file
        fprintf(f_geo.f, "# G-Code Toolpaths\n");
        fprintf(f_geo.f, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);
        fprintf(f_geo.f, "\nmtllib ./%s\n", materials_filename.filename().string().c_str());

        // open material file
        FilePtr f_mat = boost::nowide::fopen(materials_filename.string().c_str(), "w");
        if (f_mat.f == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "ToolpathsObjExporter: Couldn't open " << materials_filename.string() << " for writing";
            return;
        }

        // write header to material file
        fprintf(f_mat.f, "# G-Code Toolpaths Materials\n");
        fprintf(f_mat.f, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);

        libvgcode::Interval visible_range = m_viewer.get_view_visible_range();
        if (m_viewer.is_top_layer_only_view_range())
            visible_range[0] = m_viewer.get_view_full_range()[0];
        for (size_t i = visible_range[0]; i <= visible_range[1]; ++i) {
            const libvgcode::PathVertex& curr = m_viewer.get_vertex_at(i);
            const libvgcode::PathVertex& next = m_viewer.get_vertex_at(i + 1);
            if (!curr.is_extrusion() || !next.is_extrusion())
                continue;
            const libvgcode::PathVertex& nextnext = m_viewer.get_vertex_at(i + 2);
            unsigned char flags = 0;
            if (curr.gcode_id == next.gcode_id)
                flags |= Flag_First;
            if (i + 1 == visible_range[1] || !nextnext.is_extrusion())
                flags |= Flag_Last;
            else
                flags |= Flag_Internal;
            export_segment(*f_geo.f, flags, i, curr, next, nextnext);
        }
        export_materials(*f_mat.f);
    }

private:
    const libvgcode::Viewer& m_viewer;
    size_t m_vertices_count{ 0 };
    std::vector<libvgcode::Color> m_colors;
    static const unsigned char Flag_First    = 0x01;
    static const unsigned char Flag_Last     = 0x02;
    static const unsigned char Flag_Internal = 0x04;
    static const float Cap_Rounding_Factor;

    struct SegmentLocalAxes
    {
        Vec3f forward;
        Vec3f right;
        Vec3f up;
    };

    SegmentLocalAxes segment_local_axes(const Vec3f& v1, const Vec3f& v2) {
        SegmentLocalAxes ret;
        ret.forward = (v2 - v1).normalized();
        ret.right   = ret.forward.cross(Vec3f::UnitZ()).normalized();
        ret.up      = ret.right.cross(ret.forward);
        return ret;
    }

    struct Vertex
    {
        Vec3f position;
        Vec3f normal;
    };

    struct CrossSection
    {
        Vertex right;
        Vertex top;
        Vertex left;
        Vertex bottom;
    };

    CrossSection cross_section(const Vec3f& v, const Vec3f& right, const Vec3f& up, float width, float height) {
        CrossSection ret;
        const Vec3f w_shift = 0.5f * width * right;
        const Vec3f h_shift = 0.5f * height * up;
        ret.right.position  = v + w_shift;
        ret.right.normal    = right;
        ret.top.position    = v + h_shift;
        ret.top.normal      = up;
        ret.left.position   = v - w_shift;
        ret.left.normal     = -right;
        ret.bottom.position = v - h_shift;
        ret.bottom.normal   = -up;
        return ret;
    }

    CrossSection normal_cross_section(const Vec3f& v, const SegmentLocalAxes& axes, float width, float height) {
        return cross_section(v, axes.right, axes.up, width, height);
    }

    enum CornerType : unsigned char
    {
        RightTurn,
        LeftTurn,
        Straight
    };

    CrossSection corner_cross_section(const Vec3f& v, const SegmentLocalAxes& axes1, const SegmentLocalAxes& axes2,
        float width, float height, CornerType& corner_type) {
        if (std::abs(std::abs(axes1.forward.dot(axes2.forward)) - 1.0f) < EPSILON)
            corner_type = CornerType::Straight;
        else if (axes1.up.dot(axes1.forward.cross(axes2.forward)) < 0.0f)
            corner_type = CornerType::RightTurn;
        else
            corner_type = CornerType::LeftTurn;
        const Vec3f right = (0.5f * (axes1.right + axes2.right)).normalized();
        return cross_section(v, right, axes1.up, width, height);
    }

    void export_segment(FILE& f, unsigned char flags, size_t v1_id, const libvgcode::PathVertex& v1, const libvgcode::PathVertex& v2, const libvgcode::PathVertex& v3) {
        const Vec3f v1_pos = libvgcode::convert(v1.position);
        const Vec3f v2_pos = libvgcode::convert(v2.position);
        const Vec3f v3_pos = libvgcode::convert(v3.position);
        const SegmentLocalAxes v1_v2 = segment_local_axes(v1_pos, v2_pos);
        const SegmentLocalAxes v2_v3 = segment_local_axes(v2_pos, v3_pos);

        // starting cap
        if ((flags & Flag_First) > 0) {
            const Vertex v0 = { v1_pos - Cap_Rounding_Factor * v1.width * v1_v2.forward, -v1_v2.forward };
            const CrossSection ncs = normal_cross_section(v1_pos, v1_v2, v1.width, v1.height);
            export_vertex(f, v0);         // 0
            export_vertex(f, ncs.right);  // 1
            export_vertex(f, ncs.top);    // 2
            export_vertex(f, ncs.left);   // 3
            export_vertex(f, ncs.bottom); // 4
            export_material(f, color_id(v1_id));
            export_triangle(f, vertex_id(0), vertex_id(1), vertex_id(2));
            export_triangle(f, vertex_id(0), vertex_id(2), vertex_id(3));
            export_triangle(f, vertex_id(0), vertex_id(3), vertex_id(4));
            export_triangle(f, vertex_id(0), vertex_id(4), vertex_id(1));
            m_vertices_count += 5;
        }
        // segment body + ending cap
        if ((flags & Flag_Last) > 0) {
            const Vertex v0 = { v2_pos + Cap_Rounding_Factor * v2.width * v1_v2.forward, v1_v2.forward };
            const CrossSection ncs = normal_cross_section(v2_pos, v1_v2, v2.width, v2.height);
            export_vertex(f, v0);         // 0
            export_vertex(f, ncs.right);  // 1
            export_vertex(f, ncs.top);    // 2
            export_vertex(f, ncs.left);   // 3
            export_vertex(f, ncs.bottom); // 4
            export_material(f, color_id(v1_id + 1));
            // segment body
            export_triangle(f, vertex_id(-4), vertex_id(1), vertex_id(2));
            export_triangle(f, vertex_id(-4), vertex_id(2), vertex_id(-3));
            export_triangle(f, vertex_id(-3), vertex_id(2), vertex_id(3));
            export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(-2));
            export_triangle(f, vertex_id(-2), vertex_id(3), vertex_id(4));
            export_triangle(f, vertex_id(-2), vertex_id(4), vertex_id(-1));
            export_triangle(f, vertex_id(-1), vertex_id(4), vertex_id(1));
            export_triangle(f, vertex_id(-1), vertex_id(1), vertex_id(-4));
            // ending cap
            export_triangle(f, vertex_id(0), vertex_id(3), vertex_id(2));
            export_triangle(f, vertex_id(0), vertex_id(2), vertex_id(1));
            export_triangle(f, vertex_id(0), vertex_id(1), vertex_id(4));
            export_triangle(f, vertex_id(0), vertex_id(4), vertex_id(3));
            m_vertices_count += 5;
        }
        else {
            CornerType corner_type;
            const CrossSection ccs   = corner_cross_section(v2_pos, v1_v2, v2_v3, v2.width, v2.height, corner_type);
            const CrossSection ncs12 = normal_cross_section(v2_pos, v1_v2, v2.width, v2.height);
            const CrossSection ncs23 = normal_cross_section(v2_pos, v2_v3, v2.width, v2.height);
            if (corner_type == CornerType::Straight) {
                export_vertex(f, ncs12.right);  // 0
                export_vertex(f, ncs12.top);    // 1
                export_vertex(f, ncs12.left);   // 2
                export_vertex(f, ncs12.bottom); // 3
                export_material(f, color_id(v1_id + 1));
                // segment body
                export_triangle(f, vertex_id(-4), vertex_id(0), vertex_id(1));
                export_triangle(f, vertex_id(-4), vertex_id(1), vertex_id(-3));
                export_triangle(f, vertex_id(-3), vertex_id(1), vertex_id(2));
                export_triangle(f, vertex_id(-3), vertex_id(2), vertex_id(-2));
                export_triangle(f, vertex_id(-2), vertex_id(2), vertex_id(3));
                export_triangle(f, vertex_id(-2), vertex_id(3), vertex_id(-1));
                export_triangle(f, vertex_id(-1), vertex_id(3), vertex_id(0));
                export_triangle(f, vertex_id(-1), vertex_id(0), vertex_id(-4));
                m_vertices_count += 4;
            }
            else if (corner_type == CornerType::RightTurn) {
                export_vertex(f, ncs12.left);   // 0
                export_vertex(f, ccs.left);     // 1
                export_vertex(f, ccs.right);    // 2
                export_vertex(f, ncs12.top);    // 3
                export_vertex(f, ncs23.left);   // 4
                export_vertex(f, ncs12.bottom); // 5
                export_material(f, color_id(v1_id + 1));
                // segment body
                export_triangle(f, vertex_id(-4), vertex_id(2), vertex_id(3));
                export_triangle(f, vertex_id(-4), vertex_id(3), vertex_id(-3));
                export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(0));
                export_triangle(f, vertex_id(-3), vertex_id(0), vertex_id(-2));
                export_triangle(f, vertex_id(-2), vertex_id(0), vertex_id(5));
                export_triangle(f, vertex_id(-2), vertex_id(5), vertex_id(-1));
                export_triangle(f, vertex_id(-1), vertex_id(5), vertex_id(2));
                export_triangle(f, vertex_id(-1), vertex_id(2), vertex_id(-4));
                // corner
                export_triangle(f, vertex_id(1), vertex_id(0), vertex_id(3));
                export_triangle(f, vertex_id(1), vertex_id(3), vertex_id(4));
                export_triangle(f, vertex_id(1), vertex_id(4), vertex_id(5));
                export_triangle(f, vertex_id(1), vertex_id(5), vertex_id(0));
                m_vertices_count += 6;
            }
            else {
                export_vertex(f, ncs12.right);  // 0
                export_vertex(f, ccs.right);    // 1
                export_vertex(f, ncs23.right);  // 2
                export_vertex(f, ncs12.top);    // 3
                export_vertex(f, ccs.left);     // 4
                export_vertex(f, ncs12.bottom); // 5
                export_material(f, color_id(v1_id + 1));
                // segment body
                export_triangle(f, vertex_id(-4), vertex_id(0), vertex_id(3));
                export_triangle(f, vertex_id(-4), vertex_id(3), vertex_id(-3));
                export_triangle(f, vertex_id(-3), vertex_id(3), vertex_id(4));
                export_triangle(f, vertex_id(-3), vertex_id(4), vertex_id(-2));
                export_triangle(f, vertex_id(-2), vertex_id(4), vertex_id(5));
                export_triangle(f, vertex_id(-2), vertex_id(5), vertex_id(-1));
                export_triangle(f, vertex_id(-1), vertex_id(5), vertex_id(0));
                export_triangle(f, vertex_id(-1), vertex_id(0), vertex_id(-4));
                // corner
                export_triangle(f, vertex_id(1), vertex_id(2), vertex_id(3));
                export_triangle(f, vertex_id(1), vertex_id(3), vertex_id(0));
                export_triangle(f, vertex_id(1), vertex_id(0), vertex_id(5));
                export_triangle(f, vertex_id(1), vertex_id(5), vertex_id(2));
                m_vertices_count += 6;
            }
        }
    }

    size_t vertex_id(int id) { return static_cast<size_t>(1 + static_cast<int>(m_vertices_count) + id); }

    void export_vertex(FILE& f, const Vertex& v) {
        fprintf(&f, "v %g %g %g\n", v.position.x(), v.position.y(), v.position.z());
        fprintf(&f, "vn %g %g %g\n", v.normal.x(), v.normal.y(), v.normal.z());
    }

    void export_material(FILE& f, size_t material_id) {
        fprintf(&f, "\nusemtl material_%zu\n", material_id + 1);
    }

    void export_triangle(FILE& f, size_t v1, size_t v2, size_t v3) {
        fprintf(&f, "f %zu//%zu %zu//%zu %zu//%zu\n", v1, v1, v2, v2, v3, v3);
    }

    void export_materials(FILE& f) {
        static const float inv_255 = 1.0f / 255.0f;
        size_t materials_counter = 0;
        for (const auto& color : m_colors) {
            fprintf(&f, "\nnewmtl material_%zu\n", ++materials_counter);
            fprintf(&f, "Ka 1 1 1\n");
            fprintf(&f, "Kd %g %g %g\n", static_cast<float>(color[0]) * inv_255,
                                         static_cast<float>(color[1]) * inv_255,
                                         static_cast<float>(color[2]) * inv_255);
            fprintf(&f, "Ks 0 0 0\n");
        }
    }

    size_t color_id(size_t vertex_id) {
        const libvgcode::PathVertex& v = m_viewer.get_vertex_at(vertex_id);
        const size_t top_layer_id = m_viewer.is_top_layer_only_view_range() ? m_viewer.get_layers_view_range()[1] : 0;
        const bool color_top_layer_only = m_viewer.get_view_full_range()[1] != m_viewer.get_view_visible_range()[1];
        const libvgcode::Color color = (color_top_layer_only && v.layer_id < top_layer_id &&
              (!m_viewer.is_spiral_vase_mode() || vertex_id != m_viewer.get_view_enabled_range()[0])) ?
              libvgcode::DUMMY_COLOR : m_viewer.get_vertex_color(v);
        auto color_it = std::find_if(m_colors.begin(), m_colors.end(), [&color](const libvgcode::Color& m) { return m == color; });
        if (color_it == m_colors.end()) {
            m_colors.emplace_back(color);
            color_it = std::prev(m_colors.end());
        }
        return std::distance(m_colors.begin(), color_it);
    }
};

const float ToolpathsObjExporter::Cap_Rounding_Factor = 0.25f;

void GCodeViewer::export_toolpaths_to_obj(const char* filename) const
{
    if (filename == nullptr)
        return;

    if (!has_data())
        return;

    wxBusyCursor busy;

    ToolpathsObjExporter exporter(m_viewer);
    exporter.export_to(filename);
}

void GCodeViewer::load_shells(const Print& print)
{
    m_shells.volumes.clear();

    if (print.objects().empty())
        // no shells, return
        return;

    // adds objects' volumes 
    for (const PrintObject* obj : print.objects()) {
        const ModelObject* model_obj = obj->model_object();
        int object_id = -1;
        const ModelObjectPtrs model_objects = wxGetApp().plater()->model().objects;
        for (int i = 0; i < static_cast<int>(model_objects.size()); ++i) {
            if (model_obj->id() == model_objects[i]->id()) {
                object_id = i;
                break;
            }
        }
        if (object_id == -1)
            continue;

        std::vector<int> instance_ids(model_obj->instances.size());
        for (int i = 0; i < (int)model_obj->instances.size(); ++i) {
            instance_ids[i] = i;
        }

        size_t current_volumes_count = m_shells.volumes.volumes.size();
        m_shells.volumes.load_object(model_obj, object_id, instance_ids);

        // adjust shells' z if raft is present
        const SlicingParameters& slicing_parameters = obj->slicing_parameters();
        if (slicing_parameters.object_print_z_min != 0.0) {
            const Vec3d z_offset = slicing_parameters.object_print_z_min * Vec3d::UnitZ();
            for (size_t i = current_volumes_count; i < m_shells.volumes.volumes.size(); ++i) {
                GLVolume* v = m_shells.volumes.volumes[i];
                v->set_volume_offset(v->get_volume_offset() + z_offset);
            }
        }
    }

    wxGetApp().plater()->get_current_canvas3D()->check_volumes_outside_state(m_shells.volumes);

    // remove modifiers, non-printable and out-of-bed volumes
    while (true) {
        GLVolumePtrs::iterator it = std::find_if(m_shells.volumes.volumes.begin(), m_shells.volumes.volumes.end(),
            [](GLVolume* volume) { return volume->is_modifier || !volume->printable || volume->is_outside; });
        if (it != m_shells.volumes.volumes.end()) {
            delete *it;
            m_shells.volumes.volumes.erase(it);
        }
        else
            break;
    }

    // removes volumes which are completely below bed
    int i = 0;
    while (i < (int)m_shells.volumes.volumes.size()) {
        GLVolume* v = m_shells.volumes.volumes[i];
        if (v->transformed_bounding_box().max.z() < SINKING_MIN_Z_THRESHOLD + EPSILON) {
            delete v;
            m_shells.volumes.volumes.erase(m_shells.volumes.volumes.begin() + i);
            --i;
        }
        ++i;
    }

    // search for sinking volumes and replace their mesh with the part of it with positive z
    for (GLVolume* v : m_shells.volumes.volumes) {
        if (v->is_sinking()) {
            TriangleMesh mesh(wxGetApp().plater()->model().objects[v->object_idx()]->volumes[v->volume_idx()]->mesh());
            mesh.transform(v->world_matrix(), true);
            indexed_triangle_set upper_its;
            cut_mesh(mesh.its, 0.0f, &upper_its, nullptr);
            v->model.reset();
            v->model.init_from(upper_its);
            v->set_instance_transformation(Transform3d::Identity());
            v->set_volume_transformation(Transform3d::Identity());
        }
    }

    for (GLVolume* volume : m_shells.volumes.volumes) {
        volume->zoom_to_volumes = false;
        volume->color.a(0.25f);
        volume->force_native_color = true;
        volume->set_render_color(true);
    }

    m_shells_bounding_box.reset();
    for (const GLVolume* volume : m_shells.volumes.volumes) {
        m_shells_bounding_box.merge(volume->transformed_bounding_box());
    }

    m_max_bounding_box.reset();
}

void GCodeViewer::load_wipetower_shell(const Print& print)
{
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF && print.is_step_done(psWipeTower)) {
        // adds wipe tower's volume
        const double max_z = print.objects()[0]->model_object()->get_model()->max_z();
        const PrintConfig& config = print.config();
        const size_t extruders_count = get_extruders_count();
        if (extruders_count > 1 && config.wipe_tower && !config.complete_objects) {
            const WipeTowerData& wipe_tower_data = print.wipe_tower_data(extruders_count);
            const float depth = wipe_tower_data.depth;
            const std::vector<std::pair<float, float>> z_and_depth_pairs = print.wipe_tower_data(extruders_count).z_and_depth_pairs;
            const float brim_width = wipe_tower_data.brim_width;
            if (depth != 0.) {
                GLVolume* volume{m_shells.volumes.load_wipe_tower_preview(wxGetApp().plater()->model().wipe_tower().position.x(), wxGetApp().plater()->model().wipe_tower().position.y(), config.wipe_tower_width, depth, z_and_depth_pairs,
                    max_z, config.wipe_tower_cone_angle, wxGetApp().plater()->model().wipe_tower().rotation, false, brim_width, 0)};
                m_shells.volumes.volumes.emplace_back(volume);
                volume->color.a(0.25f);
                volume->force_native_color = true;
                volume->set_render_color(true);
                m_shells_bounding_box.merge(volume->transformed_bounding_box());
                m_max_bounding_box.reset();
            }
        }
    }
}

void GCodeViewer::render_toolpaths()
{
    const Camera& camera = wxGetApp().plater()->get_camera();

    Transform3d tr = camera.get_view_matrix();
    tr.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
    Matrix4f m = tr.matrix().cast<float>();

    const libvgcode::Mat4x4 converted_view_matrix = libvgcode::convert(m);
    const libvgcode::Mat4x4 converted_projetion_matrix = libvgcode::convert(static_cast<Matrix4f>(camera.get_projection_matrix().matrix().cast<float>()));
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_viewer.set_cog_marker_scale_factor(m_cog_marker_fixed_screen_size ? 10.0f * m_cog_marker_size * camera.get_inv_zoom() : m_cog_marker_size);
    m_viewer.set_tool_marker_scale_factor(m_tool_marker_fixed_screen_size ? 10.0f * m_tool_marker_size * camera.get_inv_zoom() : m_tool_marker_size);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_viewer.render(converted_view_matrix, converted_projetion_matrix);

#if ENABLE_NEW_GCODE_VIEWER_DEBUG
    if (is_legend_shown()) {
        ImGuiWrapper& imgui = *wxGetApp().imgui();
        const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
        ImGuiPureWrap::set_next_window_pos(static_cast<float>(cnv_size.get_width()), 0.0f, ImGuiCond_Always, 1.0f, 0.0f);
        ImGuiPureWrap::begin(std::string("LibVGCode Viewer Debug"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

        if (ImGui::BeginTable("Data", 2)) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "# vertices");
            ImGui::TableSetColumnIndex(1);
            ImGuiPureWrap::text(std::to_string(m_viewer.get_vertices_count()));

            ImGui::Separator();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "cpu memory");
            ImGui::TableSetColumnIndex(1);
            ImGuiPureWrap::text(format_memsize(m_viewer.get_used_cpu_memory()));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "gpu memory");
            ImGui::TableSetColumnIndex(1);
            ImGuiPureWrap::text(format_memsize(m_viewer.get_used_gpu_memory()));

            ImGui::Separator();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "layers range");
            ImGui::TableSetColumnIndex(1);
            const libvgcode::Interval& layers_range = m_viewer.get_layers_view_range();
            ImGuiPureWrap::text(std::to_string(layers_range[0]) + " - " + std::to_string(layers_range[1]));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "view range (full)");
            ImGui::TableSetColumnIndex(1);
            const libvgcode::Interval& full_view_range = m_viewer.get_view_full_range();
            ImGuiPureWrap::text(std::to_string(full_view_range[0]) + " - " + std::to_string(full_view_range[1]) + " | " +
                std::to_string(m_viewer.get_vertex_at(full_view_range[0]).gcode_id) + " - " +
                std::to_string(m_viewer.get_vertex_at(full_view_range[1]).gcode_id));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "view range (enabled)");
            ImGui::TableSetColumnIndex(1);
            const libvgcode::Interval& enabled_view_range = m_viewer.get_view_enabled_range();
            ImGuiPureWrap::text(std::to_string(enabled_view_range[0]) + " - " + std::to_string(enabled_view_range[1]) + " | " +
                std::to_string(m_viewer.get_vertex_at(enabled_view_range[0]).gcode_id) + " - " +
                std::to_string(m_viewer.get_vertex_at(enabled_view_range[1]).gcode_id));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "view range (visible)");
            ImGui::TableSetColumnIndex(1);
            const libvgcode::Interval& visible_view_range = m_viewer.get_view_visible_range();
            ImGuiPureWrap::text(std::to_string(visible_view_range[0]) + " - " + std::to_string(visible_view_range[1]) + " | " +
                std::to_string(m_viewer.get_vertex_at(visible_view_range[0]).gcode_id) + " - " +
                std::to_string(m_viewer.get_vertex_at(visible_view_range[1]).gcode_id));

            auto add_range_property_row = [&imgui](const std::string& label, const std::array<float, 2>& range) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, label);
                ImGui::TableSetColumnIndex(1);
                char buf[128];
                sprintf(buf, "%.3f - %.3f", range[0], range[1]);
                ImGuiPureWrap::text(buf);
            };

            add_range_property_row("height range", m_viewer.get_color_range(libvgcode::EViewType::Height).get_range());
            add_range_property_row("width range", m_viewer.get_color_range(libvgcode::EViewType::Width).get_range());
            add_range_property_row("speed range", m_viewer.get_color_range(libvgcode::EViewType::Speed).get_range());
            add_range_property_row("fan speed range", m_viewer.get_color_range(libvgcode::EViewType::FanSpeed).get_range());
            add_range_property_row("temperature range", m_viewer.get_color_range(libvgcode::EViewType::Temperature).get_range());
            add_range_property_row("volumetric rate range", m_viewer.get_color_range(libvgcode::EViewType::VolumetricFlowRate).get_range());
            add_range_property_row("layer time linear range", m_viewer.get_color_range(libvgcode::EViewType::LayerTimeLinear).get_range());
            add_range_property_row("layer time logarithmic range", m_viewer.get_color_range(libvgcode::EViewType::LayerTimeLogarithmic).get_range());

            ImGui::EndTable();
        }

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        ImGui::Separator();

        if (ImGui::BeginTable("Cog", 2)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Cog marker scale factor");
            ImGui::TableSetColumnIndex(1);
            ImGuiPureWrap::text(std::to_string(get_cog_marker_scale_factor()));

            ImGui::EndTable();
        }

        ImGui::Separator();

        if (ImGui::BeginTable("Tool", 2)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Tool marker scale factor");
            ImGui::TableSetColumnIndex(1);
            ImGuiPureWrap::text(std::to_string(m_viewer.get_tool_marker_scale_factor()));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Tool marker z offset");
            ImGui::TableSetColumnIndex(1);
            float tool_z_offset = m_viewer.get_tool_marker_offset_z();
            if (imgui.slider_float("##ToolZOffset", &tool_z_offset, 0.0f, 1.0f))
                m_viewer.set_tool_marker_offset_z(tool_z_offset);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Tool marker color");
            ImGui::TableSetColumnIndex(1);
            const libvgcode::Color& color = m_viewer.get_tool_marker_color();
            std::array<float, 3> c = { static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f, static_cast<float>(color[2]) / 255.0f };
            if (ImGui::ColorPicker3("##ToolColor", c.data())) {
                m_viewer.set_tool_marker_color({ static_cast<uint8_t>(c[0] * 255.0f),
                                                  static_cast<uint8_t>(c[1] * 255.0f),
                                                  static_cast<uint8_t>(c[2] * 255.0f) });
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Tool marker alpha");
            ImGui::TableSetColumnIndex(1);
            float tool_alpha = m_viewer.get_tool_marker_alpha();
            if (imgui.slider_float("##ToolAlpha", &tool_alpha, 0.25f, 0.75f))
                m_viewer.set_tool_marker_alpha(tool_alpha);

            ImGui::EndTable();
        }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

        ImGui::Separator();
        if (ImGui::BeginTable("Radii", 2)) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Travels radius");
            ImGui::TableSetColumnIndex(1);
            float travels_radius = m_viewer.get_travels_radius();
            ImGui::SetNextItemWidth(200.0f);
            if (imgui.slider_float("##TravelRadius", &travels_radius, 0.05f, 0.5f))
                m_viewer.set_travels_radius(travels_radius);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Wipes radius");
            ImGui::TableSetColumnIndex(1);
            float wipes_radius = m_viewer.get_wipes_radius();
            ImGui::SetNextItemWidth(200.0f);
            if (imgui.slider_float("##WipesRadius", &wipes_radius, 0.05f, 0.5f))
                m_viewer.set_wipes_radius(wipes_radius);

            ImGui::EndTable();
        }

        imgui.end();
    }
#endif // ENABLE_NEW_GCODE_VIEWER_DEBUG
}

void GCodeViewer::render_shells()
{
    if (m_shells.volumes.empty() || (!m_shells.visible && !m_shells.force_visible))
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    const Camera& camera = wxGetApp().plater()->get_camera();

    Transform3d tr = camera.get_view_matrix();
    tr.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));

    m_shells.volumes.render(GLVolumeCollection::ERenderType::Transparent, true, tr, camera.get_projection_matrix());
    shader->set_uniform("emission_factor", 0.0f);
    shader->stop_using();
}

void GCodeViewer::render_legend(float& legend_height)
{
    if (!is_legend_shown())
        return;

    const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    ImGuiPureWrap::set_next_window_pos(0.0f, 0.0f, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    const float max_height = 0.75f * static_cast<float>(cnv_size.get_height());
    const float child_height = 0.3333f * max_height;
    ImGui::SetNextWindowSizeConstraints({ 0.0f, 0.0f }, { -1.0f, max_height });
    ImGuiPureWrap::begin(std::string("Legend"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    enum class EItemType : unsigned char
    {
        Rect,
        Circle,
        Hexagon,
        Line
    };

    const PrintEstimatedStatistics::Mode& time_mode = m_print_statistics.modes[static_cast<size_t>(m_viewer.get_time_mode())];
    const libvgcode::EViewType curr_view_type = m_viewer.get_view_type();
    const int curr_view_type_i = static_cast<int>(curr_view_type);
    bool show_estimated_time = time_mode.time > 0.0f && (curr_view_type == libvgcode::EViewType::FeatureType ||
        curr_view_type == libvgcode::EViewType::LayerTimeLinear || curr_view_type == libvgcode::EViewType::LayerTimeLogarithmic ||
        (curr_view_type == libvgcode::EViewType::ColorPrint && !time_mode.custom_gcode_times.empty()));

    const float icon_size = ImGui::GetTextLineHeight();
    const float percent_bar_size = 2.0f * ImGui::GetTextLineHeight();

    bool imperial_units = wxGetApp().app_config->get_bool("use_inches");

    auto append_item = [icon_size, percent_bar_size, &imgui, imperial_units](EItemType type, const ColorRGBA& color, const std::string& label,
        bool visible = true, const std::string& time = "", float percent = 0.0f, float max_percent = 0.0f, const std::array<float, 4>& offsets = { 0.0f, 0.0f, 0.0f, 0.0f },
        double used_filament_m = 0.0, double used_filament_g = 0.0,
        std::function<void()> callback = nullptr) {
        if (!visible)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        switch (type) {
        default:
        case EItemType::Rect: {
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiPSWrap::to_ImU32(color));
            break;
        }
        case EItemType::Circle: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGuiPSWrap::to_ImU32(color), 16);
            break;
        }
        case EItemType::Hexagon: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGuiPSWrap::to_ImU32(color), 6);
            break;
        }
        case EItemType::Line: {
            draw_list->AddLine({ pos.x + 1, pos.y + icon_size - 1 }, { pos.x + icon_size - 1, pos.y + 1 }, ImGuiPSWrap::to_ImU32(color), 3.0f);
            break;
        }
        }

        // draw text
        ImGui::Dummy({ icon_size, icon_size });
        ImGui::SameLine();

        // localize "g" and "m" units
        const std::string& grams  = _u8L("g");
        const std::string& inches = _u8L("in");
        const std::string& metres = _CTX_utf8(L_CONTEXT("m", "Metre"), "Metre");
        if (callback != nullptr) {
            if (ImGui::MenuItem(label.c_str()))
                callback();
            else {
                // show tooltip
                if (ImGui::IsItemHovered()) {
                    if (!visible)
                        ImGui::PopStyleVar();
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiPureWrap::COL_WINDOW_BACKGROUND);
                    ImGui::BeginTooltip();
                    ImGuiPureWrap::text(visible ? _u8L("Click to hide") : _u8L("Click to show"));
                    ImGui::EndTooltip();
                    ImGui::PopStyleColor();
                    if (!visible)
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);

                    // to avoid the tooltip to change size when moving the mouse
                    imgui.set_requires_extra_frame();
                }
            }

            if (!time.empty()) {
                ImGui::SameLine(offsets[0]);
                ImGuiPureWrap::text(time);
                ImGui::SameLine(offsets[1]);
                pos = ImGui::GetCursorScreenPos();
                const float width = std::max(1.0f, percent_bar_size * percent / max_percent);
                draw_list->AddRectFilled({ pos.x, pos.y + 2.0f }, { pos.x + width, pos.y + icon_size - 2.0f },
                    ImGui::GetColorU32(ImGuiPureWrap::COL_ORANGE_LIGHT));
                ImGui::Dummy({ percent_bar_size, icon_size });
                ImGui::SameLine();
                char buf[64];
                ::sprintf(buf, "%.1f%%", 100.0f * percent);
                ImGui::TextUnformatted((percent > 0.0f) ? buf : "");
                ImGui::SameLine(offsets[2]);
                ImGuiPureWrap::text(format("%1$.2f %2%", used_filament_m, (imperial_units ? inches : metres)));
                ImGui::SameLine(offsets[3]);
                ImGuiPureWrap::text(format("%1$.2f %2%", used_filament_g, grams));
            }
        }
        else {
            ImGuiPureWrap::text(label);
            if (!time.empty()) {
                ImGui::SameLine(offsets[0]);
                ImGuiPureWrap::text(time);
                ImGui::SameLine(offsets[1]);
                pos = ImGui::GetCursorScreenPos();
                const float width = std::max(1.0f, percent_bar_size * percent / max_percent);
                draw_list->AddRectFilled({ pos.x, pos.y + 2.0f }, { pos.x + width, pos.y + icon_size - 2.0f },
                    ImGui::GetColorU32(ImGuiPureWrap::COL_ORANGE_LIGHT));
                ImGui::Dummy({ percent_bar_size, icon_size });
                ImGui::SameLine();
                char buf[64];
                ::sprintf(buf, "%.1f%%", 100.0f * percent);
                ImGui::TextUnformatted((percent > 0.0f) ? buf : "");
            }
            else if (used_filament_m > 0.0) {
                ImGui::SameLine(offsets[0]);
                ImGuiPureWrap::text(format("%1$.2f %2%", used_filament_m, (imperial_units ? inches : metres)));
                ImGui::SameLine(offsets[1]);
                ImGuiPureWrap::text(format("%1$.2f %2%", used_filament_g, grams));
            }
        }

        if (!visible)
            ImGui::PopStyleVar();
    };

    auto append_range = [append_item](const libvgcode::ColorRange& range, unsigned int decimals) {
        auto append_range_item = [append_item, &range](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
            append_item(EItemType::Rect, libvgcode::convert(range.get_palette()[i]), buf);
        };

        std::vector<float> values = range.get_values();
        if (values.size() == 1)
            // single item use case
            append_range_item(0, values.front(), decimals);
        else if (values.size() == 2) {
            // two items use case
            append_range_item(static_cast<int>(range.get_palette().size()) - 1, values.back(), decimals);
            append_range_item(0, values.front(), decimals);
        }
        else {
            for (int i = static_cast<int>(range.get_palette().size()) - 1; i >= 0; --i) {
                append_range_item(i, values[i], decimals);
            }
        }
    };

    auto append_time_range = [append_item](const libvgcode::ColorRange& range) {
        auto append_range_item = [append_item, &range](int i, float value) {
            std::string str_value = get_time_dhms(value);
            if (str_value == "0s")
                str_value = "< 1s";
            append_item(EItemType::Rect, libvgcode::convert(range.get_palette()[i]), str_value);
        };

        std::vector<float> values = range.get_values();
        if (values.size() == 1)
            // single item use case
            append_range_item(0, values.front());
        else if (values.size() == 2) {
            // two items use case
            append_range_item(static_cast<int>(range.get_palette().size()) - 1, values.back());
            append_range_item(0, values.front());
        }
        else {
            for (int i = static_cast<int>(range.get_palette().size()) - 1; i >= 0; --i) {
                append_range_item(i, values[i]);
            }
        }
    };

    auto append_headers = [](const std::array<std::string, 5>& texts, const std::array<float, 4>& offsets) {
        size_t i = 0;
        for (; i < offsets.size(); i++) {
            ImGuiPureWrap::text(texts[i]);
            ImGui::SameLine(offsets[i]);
        }
        ImGuiPureWrap::text(texts[i]);
        ImGui::Separator();
    };

    auto max_width = [](const std::vector<std::string>& items, const std::string& title, float extra_size = 0.0f) {
        float ret = ImGui::CalcTextSize(title.c_str()).x;
        for (const std::string& item : items) {
            ret = std::max(ret, extra_size + ImGui::CalcTextSize(item.c_str()).x);
        }
        return ret;
    };

    auto calculate_offsets = [max_width](const std::vector<std::string>& labels, const std::vector<std::string>& times,
        const std::array<std::string, 4>& titles, float extra_size = 0.0f) {
            const ImGuiStyle& style = ImGui::GetStyle();
            std::array<float, 4> ret = { 0.0f, 0.0f, 0.0f, 0.0f };
            ret[0] = max_width(labels, titles[0], extra_size) + 3.0f * style.ItemSpacing.x;
            for (size_t i = 1; i < titles.size(); i++)
                ret[i] = ret[i-1] + max_width(times, titles[i]) + style.ItemSpacing.x;
            return ret;
    };

    auto color_print_ranges = [this](unsigned char extruder_id, const std::vector<CustomGCode::Item>& custom_gcode_per_print_z) {
        std::vector<std::pair<ColorRGBA, std::pair<double, double>>> ret;
        ret.reserve(custom_gcode_per_print_z.size());

        for (const auto& item : custom_gcode_per_print_z) {
            if (extruder_id + 1 != static_cast<unsigned char>(item.extruder))
                continue;

            if (item.type != CustomGCode::ColorChange)
                continue;

            const std::vector<float> zs = m_viewer.get_layers_zs();
            auto lower_b = std::lower_bound(zs.begin(), zs.end(),
                static_cast<float>(item.print_z - Slic3r::CustomGCode::epsilon()));
            if (lower_b == zs.end())
                continue;

            const double current_z = *lower_b;
            const double previous_z = (lower_b == zs.begin()) ? 0.0 : *(--lower_b);

            // to avoid duplicate values, check adding values
            if (ret.empty() || !(ret.back().second.first == previous_z && ret.back().second.second == current_z)) {
                ColorRGBA color;
                decode_color(item.color, color);
                ret.push_back({ color, { previous_z, current_z } });
            }
        }

        return ret;
    };

    auto upto_label = [](double z) {
        char buf[64];
        ::sprintf(buf, "%.2f", z);
        return _u8L("up to") + " " + std::string(buf) + " " + _u8L("mm");
    };

    auto above_label = [](double z) {
        char buf[64];
        ::sprintf(buf, "%.2f", z);
        return _u8L("above") + " " + std::string(buf) + " " + _u8L("mm");
    };

    auto fromto_label = [](double z1, double z2) {
        char buf1[64];
        ::sprintf(buf1, "%.2f", z1);
        char buf2[64];
        ::sprintf(buf2, "%.2f", z2);
        return _u8L("from") + " " + std::string(buf1) + " " + _u8L("to") + " " + std::string(buf2) + " " + _u8L("mm");
    };

    auto role_time_and_percent = [this, time_mode](libvgcode::EGCodeExtrusionRole role) {
        const float time = m_viewer.get_extrusion_role_estimated_time(role);
        return std::make_pair(time, time / time_mode.time);
    };

    auto used_filament_per_role = [this, imperial_units](GCodeExtrusionRole role) {
        auto it = m_print_statistics.used_filaments_per_role.find(role);
        if (it == m_print_statistics.used_filaments_per_role.end())
            return std::make_pair(0.0, 0.0);

        double koef = imperial_units ? 1000.0 / ObjectManipulation::in_to_mm : 1.0;
        return std::make_pair(it->second.first * koef, it->second.second);
    };

    auto toggle_extrusion_role_visibility = [this](libvgcode::EGCodeExtrusionRole role) {
        const libvgcode::Interval view_visible_range = m_viewer.get_view_visible_range();
        const libvgcode::Interval view_enabled_range = m_viewer.get_view_enabled_range();
        m_viewer.toggle_extrusion_role_visibility(role);
        std::optional<int> view_visible_range_min;
        std::optional<int> view_visible_range_max;
        if (view_visible_range != view_enabled_range) {
            view_visible_range_min = static_cast<int>(view_visible_range[0]);
            view_visible_range_max = static_cast<int>(view_visible_range[1]);
        }
        wxGetApp().plater()->update_preview_moves_slider(view_visible_range_min, view_visible_range_max);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
    };

    // data used to properly align items in columns when showing time
    std::array<float, 4> offsets = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<std::string> labels;
    std::vector<std::string> times;
    std::vector<float> percents;
    std::vector<double> used_filaments_m;
    std::vector<double> used_filaments_g;
    float max_time_percent = 0.0f;

    if (curr_view_type == libvgcode::EViewType::FeatureType) {
        // calculate offsets to align time/percentage data
        const std::vector<libvgcode::EGCodeExtrusionRole>& roles = m_viewer.get_extrusion_roles();
        for (libvgcode::EGCodeExtrusionRole role : roles) {
            assert(static_cast<size_t>(role) < libvgcode::GCODE_EXTRUSION_ROLES_COUNT);
            if (static_cast<size_t>(role) < libvgcode::GCODE_EXTRUSION_ROLES_COUNT) {
                labels.push_back(_u8L(gcode_extrusion_role_to_string(convert(role))));
                auto [time, percent] = role_time_and_percent(role);
                times.push_back((time > 0.0f) ? short_time_ui(get_time_dhms(time)) : "");
                percents.push_back(percent);
                max_time_percent = std::max(max_time_percent, percent);
                auto [used_filament_m, used_filament_g] = used_filament_per_role(convert(role));
                used_filaments_m.push_back(used_filament_m);
                used_filaments_g.push_back(used_filament_g);
            }
        }

        std::string longest_percentage_string;
        for (double item : percents) {
            char buffer[64];
            ::sprintf(buffer, "%.2f %%", item);
            if (::strlen(buffer) > longest_percentage_string.length())
                longest_percentage_string = buffer;
        }
        longest_percentage_string += "            ";
        if (_u8L("Percentage").length() > longest_percentage_string.length())
            longest_percentage_string = _u8L("Percentage");

        std::string longest_used_filament_string;
        for (double item : used_filaments_m) {
            char buffer[64];
            ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", item);
            if (::strlen(buffer) > longest_used_filament_string.length())
                longest_used_filament_string = buffer;
        }

        offsets = calculate_offsets(labels, times, { _u8L("Feature type"), _u8L("Time"), longest_percentage_string, longest_used_filament_string }, icon_size);
    }

    // get used filament (meters and grams) from used volume in respect to the active extruder
    auto get_used_filament_from_volume = [this, imperial_units](double volume, int extruder_id) {
        double koef = imperial_units ? 1.0 / ObjectManipulation::in_to_mm : 0.001;
        std::pair<double, double> ret = { koef * volume / (PI * sqr(0.5 * m_filament_diameters[extruder_id])),
                                          volume * m_filament_densities[extruder_id] * 0.001 };
        return ret;
    };

    if (curr_view_type == libvgcode::EViewType::Tool) {
        // calculate used filaments data
        const size_t extruders_count = get_extruders_count();
        used_filaments_m = std::vector<double>(extruders_count, 0.0);
        used_filaments_g = std::vector<double>(extruders_count, 0.0);
        const std::vector<uint8_t>& used_extruders_ids = m_viewer.get_used_extruders_ids();
        for (uint8_t extruder_id : used_extruders_ids) {
            if (m_print_statistics.volumes_per_extruder.find(extruder_id) == m_print_statistics.volumes_per_extruder.end())
                continue;
            double volume = m_print_statistics.volumes_per_extruder.at(extruder_id);

            auto [used_filament_m, used_filament_g] = get_used_filament_from_volume(volume, extruder_id);
            used_filaments_m[extruder_id] = used_filament_m;
            used_filaments_g[extruder_id] = used_filament_g;
        }

        std::string longest_used_filament_string;
        for (double item : used_filaments_m) {
            char buffer[64];
            ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", item);
            if (::strlen(buffer) > longest_used_filament_string.length())
                longest_used_filament_string = buffer;
        }

        offsets = calculate_offsets(labels, times, { "Extruder NNN", longest_used_filament_string }, icon_size);
    }

    // selection section
    bool view_type_changed = false;
    int new_view_type_i = curr_view_type_i;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.1f, 0.1f, 0.8f });
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, { 0.2f, 0.2f, 0.2f, 0.8f });
    std::vector<std::string> view_options;
    std::vector<int> view_options_id;
    const std::vector<float> layers_times = get_layers_times();
    if (!layers_times.empty() && layers_times.size() == m_viewer.get_layers_count()) {
        view_options = { _u8L("Feature type"), _u8L("Height (mm)"), _u8L("Width (mm)"), _u8L("Speed (mm/s)"), _u8L("Actual speed (mm/s)"),
                         _u8L("Fan speed (%)"), _u8L("Temperature (°C)"), _u8L("Volumetric flow rate (mm³/s)"), _u8L("Actual volumetric flow rate (mm³/s)"),
                         _u8L("Layer time (linear)"), _u8L("Layer time (logarithmic)"), _u8L("Tool"), _u8L("Color Print") };
        view_options_id = { static_cast<int>(libvgcode::EViewType::FeatureType),
                            static_cast<int>(libvgcode::EViewType::Height),
                            static_cast<int>(libvgcode::EViewType::Width),
                            static_cast<int>(libvgcode::EViewType::Speed),
                            static_cast<int>(libvgcode::EViewType::ActualSpeed),
                            static_cast<int>(libvgcode::EViewType::FanSpeed),
                            static_cast<int>(libvgcode::EViewType::Temperature),
                            static_cast<int>(libvgcode::EViewType::VolumetricFlowRate),
                            static_cast<int>(libvgcode::EViewType::ActualVolumetricFlowRate),
                            static_cast<int>(libvgcode::EViewType::LayerTimeLinear),
                            static_cast<int>(libvgcode::EViewType::LayerTimeLogarithmic),
                            static_cast<int>(libvgcode::EViewType::Tool),
                            static_cast<int>(libvgcode::EViewType::ColorPrint) };
    }
    else {
        view_options = { _u8L("Feature type"), _u8L("Height (mm)"), _u8L("Width (mm)"), _u8L("Speed (mm/s)"), _u8L("Actual speed (mm/s)"),
                         _u8L("Fan speed (%)"), _u8L("Temperature (°C)"), _u8L("Volumetric flow rate (mm³/s)"), _u8L("Actual volumetric flow rate (mm³/s)"),
                         _u8L("Tool"), _u8L("Color Print") };
        view_options_id = { static_cast<int>(libvgcode::EViewType::FeatureType),
                            static_cast<int>(libvgcode::EViewType::Height),
                            static_cast<int>(libvgcode::EViewType::Width),
                            static_cast<int>(libvgcode::EViewType::Speed),
                            static_cast<int>(libvgcode::EViewType::ActualSpeed),
                            static_cast<int>(libvgcode::EViewType::FanSpeed),
                            static_cast<int>(libvgcode::EViewType::Temperature),
                            static_cast<int>(libvgcode::EViewType::VolumetricFlowRate),
                            static_cast<int>(libvgcode::EViewType::ActualVolumetricFlowRate),
                            static_cast<int>(libvgcode::EViewType::Tool),
                            static_cast<int>(libvgcode::EViewType::ColorPrint) };
        if (new_view_type_i == static_cast<int>(libvgcode::EViewType::LayerTimeLinear) ||
            new_view_type_i == static_cast<int>(libvgcode::EViewType::LayerTimeLogarithmic))
            new_view_type_i = 0;
    }
    auto new_view_type_it = std::find(view_options_id.begin(), view_options_id.end(), new_view_type_i);
    int new_view_type_id = (new_view_type_it == view_options_id.end()) ? 0 : std::distance(view_options_id.begin(), new_view_type_it);
    if (ImGuiPureWrap::combo(std::string(), view_options, new_view_type_id, ImGuiComboFlags_HeightLargest, 0.0f, -1.0f))
        new_view_type_i = view_options_id[new_view_type_id];
    ImGui::PopStyleColor(2);
   
    if (curr_view_type_i != new_view_type_i) {
        enable_view_type_cache_load(false);
        set_view_type(static_cast<libvgcode::EViewType>(new_view_type_i));
        enable_view_type_cache_load(true);
        wxGetApp().plater()->set_keep_current_preview_type(true);
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
        view_type_changed = true;
    }

    const libvgcode::EViewType new_view_type = m_viewer.get_view_type();

    // extrusion paths section -> title
    if (new_view_type == libvgcode::EViewType::FeatureType)
        append_headers({ "", _u8L("Time"), _u8L("Percentage"), _u8L("Used filament") }, offsets);
    else if (new_view_type == libvgcode::EViewType::Tool)
        append_headers({ "", _u8L("Used filament"), "", "" }, offsets);
    else
        ImGui::Separator();

    if (!view_type_changed) {
        // extrusion paths section -> items
        switch (new_view_type)
        {
        case libvgcode::EViewType::FeatureType:
        {
            const float travels_time = m_viewer.get_travels_estimated_time();
            max_time_percent = std::max(max_time_percent, travels_time / time_mode.time);
            const std::vector<libvgcode::EGCodeExtrusionRole>& roles = m_viewer.get_extrusion_roles();
            for (size_t i = 0; i < roles.size(); ++i) {
                libvgcode::EGCodeExtrusionRole role = roles[i];
                if (static_cast<size_t>(role) >= libvgcode::GCODE_EXTRUSION_ROLES_COUNT)
                    continue;

                const bool visible = m_viewer.is_extrusion_role_visible(role);

                append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_extrusion_role_color(role)), labels[i],
                    visible, times[i], percents[i], max_time_percent, offsets, used_filaments_m[i], used_filaments_g[i],
                    [role, toggle_extrusion_role_visibility]() { toggle_extrusion_role_visibility(role); }
                );
            }

            if (m_viewer.is_option_visible(libvgcode::EOptionType::Travels))
                append_item(EItemType::Line, libvgcode::convert(m_viewer.get_option_color(libvgcode::EOptionType::Travels)), _u8L("Travel"), true, short_time_ui(get_time_dhms(travels_time)),
                    travels_time / time_mode.time, max_time_percent, offsets, 0.0f, 0.0f);

            break;
        }
        case libvgcode::EViewType::Height:                   { append_range(m_viewer.get_color_range(libvgcode::EViewType::Height), 3); break; }
        case libvgcode::EViewType::Width:                    { append_range(m_viewer.get_color_range(libvgcode::EViewType::Width), 3); break; }
        case libvgcode::EViewType::Speed:                    { append_range(m_viewer.get_color_range(libvgcode::EViewType::Speed), 1); break; }
        case libvgcode::EViewType::ActualSpeed:              { append_range(m_viewer.get_color_range(libvgcode::EViewType::ActualSpeed), 1); break; }
        case libvgcode::EViewType::FanSpeed:                 { append_range(m_viewer.get_color_range(libvgcode::EViewType::FanSpeed), 0); break; }
        case libvgcode::EViewType::Temperature:              { append_range(m_viewer.get_color_range(libvgcode::EViewType::Temperature), 0); break; }
        case libvgcode::EViewType::VolumetricFlowRate:       { append_range(m_viewer.get_color_range(libvgcode::EViewType::VolumetricFlowRate), 3); break; }
        case libvgcode::EViewType::ActualVolumetricFlowRate: { append_range(m_viewer.get_color_range(libvgcode::EViewType::ActualVolumetricFlowRate), 3); break; }
        case libvgcode::EViewType::LayerTimeLinear:          { append_time_range(m_viewer.get_color_range(libvgcode::EViewType::LayerTimeLinear)); break; }
        case libvgcode::EViewType::LayerTimeLogarithmic:     { append_time_range(m_viewer.get_color_range(libvgcode::EViewType::LayerTimeLogarithmic)); break; }
        case libvgcode::EViewType::Tool: {
            // shows only extruders actually used
            const std::vector<uint8_t>& used_extruders_ids = m_viewer.get_used_extruders_ids();
            for (uint8_t extruder_id : used_extruders_ids) {
                if (used_filaments_m[extruder_id] > 0.0 && used_filaments_g[extruder_id] > 0.0)
                    append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_tool_colors()[extruder_id]), _u8L("Extruder") + " " + std::to_string(extruder_id + 1),
                    true, "", 0.0f, 0.0f, offsets, used_filaments_m[extruder_id], used_filaments_g[extruder_id]);
            }
            break;
        }
        case libvgcode::EViewType::ColorPrint: {
            size_t total_items = 1;
            const std::vector<uint8_t>& used_extruders_ids = m_viewer.get_used_extruders_ids();
            for (uint8_t extruder_id : used_extruders_ids) {
                total_items += color_print_ranges(extruder_id, m_custom_gcode_per_print_z).size();
            }

            const bool need_scrollable = static_cast<float>(total_items) * icon_size + (static_cast<float>(total_items) - 1.0f) * ImGui::GetStyle().ItemSpacing.y > child_height;

            // add scrollable region, if needed
            if (need_scrollable)
                ImGui::BeginChild("color_prints", { -1.0f, child_height }, false);
            if (get_extruders_count() == 1) { // single extruder use case
                const std::vector<std::pair<ColorRGBA, std::pair<double, double>>> cp_values = color_print_ranges(0, m_custom_gcode_per_print_z);
                const int items_cnt = static_cast<int>(cp_values.size());
                if (items_cnt == 0)  // There are no color changes, but there are some pause print or custom Gcode
                    append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_tool_colors().front()), _u8L("Default color"));
                else {
                    for (int i = items_cnt; i >= 0; --i) {
                        // create label for color change item
                        if (i == 0) {
                            append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_tool_colors().front()), upto_label(cp_values.front().second.first));
                            break;
                        }
                        else if (i == items_cnt) {
                            append_item(EItemType::Rect, cp_values[i - 1].first, above_label(cp_values[i - 1].second.second));
                            continue;
                        }
                        append_item(EItemType::Rect, cp_values[i - 1].first, fromto_label(cp_values[i - 1].second.second, cp_values[i].second.first));
                    }
                }
            }
            else { // multi extruder use case
                // shows only extruders actually used
                const std::vector<uint8_t>& used_extruders_ids = m_viewer.get_used_extruders_ids();
                for (uint8_t extruder_id : used_extruders_ids) {
                    const std::vector<std::pair<ColorRGBA, std::pair<double, double>>> cp_values = color_print_ranges(extruder_id, m_custom_gcode_per_print_z);
                    const int items_cnt = static_cast<int>(cp_values.size());
                    if (items_cnt == 0)
                        // There are no color changes, but there are some pause print or custom Gcode
                        append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_tool_colors()[extruder_id]), _u8L("Extruder") + " " +
                            std::to_string(extruder_id + 1) + " " + _u8L("default color"));
                    else {
                        for (int j = items_cnt; j >= 0; --j) {
                            // create label for color change item
                            std::string label = _u8L("Extruder") + " " + std::to_string(extruder_id + 1);
                            if (j == 0) {
                                label += " " + upto_label(cp_values.front().second.first);
                                append_item(EItemType::Rect, libvgcode::convert(m_viewer.get_tool_colors()[extruder_id]), label);
                                break;
                            }
                            else if (j == items_cnt) {
                                label += " " + above_label(cp_values[j - 1].second.second);
                                append_item(EItemType::Rect, cp_values[j - 1].first, label);
                                continue;
                            }

                            label += " " + fromto_label(cp_values[j - 1].second.second, cp_values[j].second.first);
                            append_item(EItemType::Rect, cp_values[j - 1].first, label);
                        }
                    }
                }
            }
            if (need_scrollable)
                ImGui::EndChild();

            break;
        }
        default: { break; }
        }
    }

    // partial estimated printing time section
    if (new_view_type == libvgcode::EViewType::ColorPrint) {
        using Times = std::pair<float, float>;
        using TimesList = std::vector<std::pair<CustomGCode::Type, Times>>;

        // helper structure containig the data needed to render the time items
        struct PartialTime
        {
            enum class EType : unsigned char
            {
                Print,
                ColorChange,
                Pause
            };
            EType type;
            int extruder_id;
            ColorRGBA color1;
            ColorRGBA color2;
            Times times;
            std::pair<double, double> used_filament{ 0.0f, 0.0f };
        };
        using PartialTimes = std::vector<PartialTime>;

        auto generate_partial_times = [this, get_used_filament_from_volume](const TimesList& times, const std::vector<double>& used_filaments) {
            PartialTimes items;

            std::vector<CustomGCode::Item> custom_gcode_per_print_z = m_custom_gcode_per_print_z;
            const size_t extruders_count = get_extruders_count();
            std::vector<ColorRGBA> last_color(extruders_count);
            for (size_t i = 0; i < extruders_count; ++i) {
                last_color[i] = libvgcode::convert(m_viewer.get_tool_colors()[i]);
            }
            int last_extruder_id = 1;
            int color_change_idx = 0;
            for (const auto& time_rec : times) {
                switch (time_rec.first)
                {
                case CustomGCode::PausePrint: {
                    auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                    if (it != custom_gcode_per_print_z.end()) {
                        items.push_back({ PartialTime::EType::Print, it->extruder, last_color[it->extruder - 1], ColorRGBA::BLACK(), time_rec.second });
                        items.push_back({ PartialTime::EType::Pause, it->extruder, ColorRGBA::BLACK(), ColorRGBA::BLACK(), time_rec.second });
                        custom_gcode_per_print_z.erase(it);
                    }
                    break;
                }
                case CustomGCode::ColorChange: {
                    auto it = std::find_if(custom_gcode_per_print_z.begin(), custom_gcode_per_print_z.end(), [time_rec](const CustomGCode::Item& item) { return item.type == time_rec.first; });
                    if (it != custom_gcode_per_print_z.end()) {
                        items.push_back({ PartialTime::EType::Print, it->extruder, last_color[it->extruder - 1], ColorRGBA::BLACK(), time_rec.second, get_used_filament_from_volume(used_filaments[color_change_idx++], it->extruder - 1) });
                        ColorRGBA color;
                        decode_color(it->color, color);
                        items.push_back({ PartialTime::EType::ColorChange, it->extruder, last_color[it->extruder - 1], color, time_rec.second });
                        last_color[it->extruder - 1] = color;
                        last_extruder_id = it->extruder;
                        custom_gcode_per_print_z.erase(it);
                    }
                    else
                        items.push_back({ PartialTime::EType::Print, last_extruder_id, last_color[last_extruder_id - 1], ColorRGBA::BLACK(), time_rec.second, get_used_filament_from_volume(used_filaments[color_change_idx++], last_extruder_id - 1) });

                    break;
                }
                default: { break; }
                }
            }

            return items;
        };

        auto append_color_change = [](const ColorRGBA& color1, const ColorRGBA& color2, const std::array<float, 4>& offsets, const Times& times) {
            ImGuiPureWrap::text(_u8L("Color change"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiPSWrap::to_ImU32(color1));
            pos.x += icon_size;
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiPSWrap::to_ImU32(color2));

            ImGui::SameLine(offsets[0]);
            ImGuiPureWrap::text(short_time_ui(get_time_dhms(times.second - times.first)));
        };

        auto append_print = [imperial_units](const ColorRGBA& color, const std::array<float, 4>& offsets, const Times& times, std::pair<double, double> used_filament) {
            ImGuiPureWrap::text(_u8L("Print"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiPSWrap::to_ImU32(color));

            ImGui::SameLine(offsets[0]);
            ImGuiPureWrap::text(short_time_ui(get_time_dhms(times.second)));
            ImGui::SameLine(offsets[1]);
            ImGuiPureWrap::text(short_time_ui(get_time_dhms(times.first)));
            if (used_filament.first > 0.0f) {
                char buffer[64];
                ImGui::SameLine(offsets[2]);
                ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", used_filament.first);
                ImGuiPureWrap::text(buffer);

                ImGui::SameLine(offsets[3]);
                ::sprintf(buffer, "%.2f g", used_filament.second);
                ImGuiPureWrap::text(buffer);
            }
        };

        PartialTimes partial_times = generate_partial_times(time_mode.custom_gcode_times, m_print_statistics.volumes_per_color_change);
        if (!partial_times.empty()) {
            labels.clear();
            times.clear();

            for (const PartialTime& item : partial_times) {
                switch (item.type)
                {
                case PartialTime::EType::Print:       { labels.push_back(_u8L("Print")); break; }
                case PartialTime::EType::Pause:       { labels.push_back(_u8L("Pause")); break; }
                case PartialTime::EType::ColorChange: { labels.push_back(_u8L("Color change")); break; }
                }
                times.push_back(short_time_ui(get_time_dhms(item.times.second)));
            }


            std::string longest_used_filament_string;
            for (const PartialTime& item : partial_times) {
                if (item.used_filament.first > 0.0f) {
                    char buffer[64];
                    ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", item.used_filament.first);
                    if (::strlen(buffer) > longest_used_filament_string.length())
                        longest_used_filament_string = buffer;
                }
            }

            offsets = calculate_offsets(labels, times, { _u8L("Event"), _u8L("Remaining time"), _u8L("Duration"), longest_used_filament_string }, 2.0f * icon_size);

            ImGui::Spacing();
            append_headers({ _u8L("Event"), _u8L("Remaining time"), _u8L("Duration"), _u8L("Used filament") }, offsets);
            const bool need_scrollable = static_cast<float>(partial_times.size()) * icon_size + (static_cast<float>(partial_times.size()) - 1.0f) * ImGui::GetStyle().ItemSpacing.y > child_height;
            if (need_scrollable)
                // add scrollable region
                ImGui::BeginChild("events", { -1.0f, child_height }, false);

            for (const PartialTime& item : partial_times) {
                switch (item.type)
                {
                case PartialTime::EType::Print: {
                    append_print(item.color1, offsets, item.times, item.used_filament);
                    break;
                }
                case PartialTime::EType::Pause: {
                    ImGuiPureWrap::text(_u8L("Pause"));
                    ImGui::SameLine(offsets[0]);
                    ImGuiPureWrap::text(short_time_ui(get_time_dhms(item.times.second - item.times.first)));
                    break;
                }
                case PartialTime::EType::ColorChange: {
                    append_color_change(item.color1, item.color2, offsets, item.times);
                    break;
                }
                }
            }

            if (need_scrollable)
                ImGui::EndChild();
        }
    }

    auto add_strings_row_to_table = [](const std::string& col_1, const ImVec4& col_1_color, const std::string& col_2, const ImVec4& col_2_color) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(col_1_color, col_1.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGuiPureWrap::text_colored(col_2_color, col_2.c_str());
    };

    // settings section
    bool has_settings = false;
    has_settings |= !m_settings_ids.print.empty();
    has_settings |= !m_settings_ids.printer.empty();
    bool has_filament_settings = true;
    has_filament_settings &= !m_settings_ids.filament.empty();
    for (const std::string& fs : m_settings_ids.filament) {
        has_filament_settings &= !fs.empty();
    }
    has_settings |= has_filament_settings;
    bool show_settings = wxGetApp().is_gcode_viewer();
    show_settings &= (new_view_type == libvgcode::EViewType::FeatureType || new_view_type == libvgcode::EViewType::Tool);
    show_settings &= has_settings;
    if (show_settings) {
        ImGui::Spacing();
        ImGuiPureWrap::title(_u8L("Settings"));

        auto trim_text_if_needed = [](const std::string& txt) {
            const float max_length = 250.0f;
            const float length = ImGui::CalcTextSize(txt.c_str()).x;
            if (length > max_length) {
                const size_t new_len = txt.length() * max_length / length;
                return txt.substr(0, new_len) + "...";
            }
            return txt;
        };

        if (ImGui::BeginTable("Settings", 2)) {
            if (!m_settings_ids.printer.empty())
                add_strings_row_to_table(_u8L("Printer") + ":", ImGuiPureWrap::COL_ORANGE_LIGHT,
                    trim_text_if_needed(m_settings_ids.printer), ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()));
            if (!m_settings_ids.print.empty())
                add_strings_row_to_table(_u8L("Print settings") + ":", ImGuiPureWrap::COL_ORANGE_LIGHT,
                    trim_text_if_needed(m_settings_ids.print), ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()));
            if (!m_settings_ids.filament.empty()) {
                const std::vector<uint8_t>& used_extruders_ids = m_viewer.get_used_extruders_ids();
                for (uint8_t extruder_id : used_extruders_ids) {
                    if (extruder_id < static_cast<unsigned char>(m_settings_ids.filament.size()) && !m_settings_ids.filament[extruder_id].empty()) {
                        std::string txt = _u8L("Filament");
                        txt += (m_viewer.get_used_extruders_count() == 1) ? ":" : " " + std::to_string(extruder_id + 1);
                        add_strings_row_to_table(txt, ImGuiPureWrap::COL_ORANGE_LIGHT,
                            trim_text_if_needed(m_settings_ids.filament[extruder_id]), ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()));
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    if (new_view_type == libvgcode::EViewType::Width || new_view_type == libvgcode::EViewType::VolumetricFlowRate ||
        new_view_type == libvgcode::EViewType::ActualVolumetricFlowRate) {
        const std::vector<libvgcode::EGCodeExtrusionRole>& roles = m_viewer.get_extrusion_roles();
        const auto custom_it = std::find(roles.begin(), roles.end(), libvgcode::EGCodeExtrusionRole::Custom);
        if (custom_it != roles.end()) {
            const bool custom_visible = m_viewer.is_extrusion_role_visible((libvgcode::EGCodeExtrusionRole)GCodeExtrusionRole::Custom);
            const std::string btn_text = custom_visible ? _u8L("Hide Custom G-code") : _u8L("Show Custom G-code");
            ImGui::Separator();
            if (imgui.button(btn_text, ImVec2(-1.0f, 0.0f), true))
                toggle_extrusion_role_visibility(libvgcode::EGCodeExtrusionRole::Custom);
        }
    }

    // total estimated printing time section
    if (show_estimated_time) {
        ImGui::Spacing();
        std::string time_title = _u8L("Estimated printing times");
        auto can_show_mode_button = [this](libvgcode::ETimeMode mode) {
            std::vector<std::string> time_strs;
            for (size_t i = 0; i < m_print_statistics.modes.size(); ++i) {
                if (m_print_statistics.modes[i].time > 0.0f) {
                    const std::string time_str = short_time(get_time_dhms(m_print_statistics.modes[i].time));
                    const auto it = std::find(time_strs.begin(), time_strs.end(), time_str);
                    if (it == time_strs.end())
                        time_strs.emplace_back(time_str);
                }
            }
            return time_strs.size() > 1;
        };

        const libvgcode::ETimeMode time_mode_id = m_viewer.get_time_mode();
        if (can_show_mode_button(time_mode_id)) {
            switch (time_mode_id)
            {
            case libvgcode::ETimeMode::Normal:  { time_title += " [" + _u8L("Normal mode") + "]"; break; }
            case libvgcode::ETimeMode::Stealth: { time_title += " [" + _u8L("Stealth mode") + "]"; break; }
            default: { assert(false); break; }
            }
        }

        ImGuiPureWrap::title(time_title + ":");

        if (ImGui::BeginTable("Times", 2)) {
            const std::vector<float> layers_times = get_layers_times();
            if (!layers_times.empty())
                add_strings_row_to_table(_u8L("First layer") + ":", ImGuiPureWrap::COL_ORANGE_LIGHT,
                    short_time_ui(get_time_dhms(layers_times.front())), ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()));

            add_strings_row_to_table(_u8L("Total") + ":", ImGuiPureWrap::COL_ORANGE_LIGHT,
                short_time_ui(get_time_dhms(time_mode.time)), ImGuiPSWrap::to_ImVec4(ColorRGBA::WHITE()));

            ImGui::EndTable();
        }

        auto show_mode_button = [this, &imgui, can_show_mode_button](const std::string& label, libvgcode::ETimeMode mode) {
            if (can_show_mode_button(mode)) {
                if (ImGuiPureWrap::button(label)) {
                    m_viewer.set_time_mode(mode);
                    imgui.set_requires_extra_frame();
                }
            }
        };

        switch (time_mode_id) {
        case libvgcode::ETimeMode::Normal: {
            show_mode_button(_u8L("Show stealth mode"), libvgcode::ETimeMode::Stealth);
            break;
        }
        case libvgcode::ETimeMode::Stealth: {
            show_mode_button(_u8L("Show normal mode"), libvgcode::ETimeMode::Normal);
            break;
        }
        default : { assert(false); break; }
        }
    }

    // toolbar section
    auto toggle_button = [this, icon_size](Preview::OptionType type, const std::string& name,
        std::function<void(ImGuiWindow& window, const ImVec2& pos, float size)> draw_callback) {
        bool active = false;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        active = (type == Preview::OptionType::Shells) ? m_shells.visible : m_viewer.is_option_visible(libvgcode::convert(type));
#else
        switch (type)
        {
        case Preview::OptionType::CenterOfGravity: { active = m_cog.is_visible(); break; }
        case Preview::OptionType::ToolMarker:      { active = m_sequential_view.marker.is_visible(); break; }
        case Preview::OptionType::Shells:          { active = m_shells.visible; break; }
        default:                                   { active = m_viewer.is_option_visible(libvgcode::convert(type)); break; }
        }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

        if (ImGuiPureWrap::draw_radio_button(name, 1.5f * icon_size, active, draw_callback)) {
            // check whether we need to keep the current visible range
            libvgcode::Interval view_visible_range = m_viewer.get_view_visible_range();
            const libvgcode::Interval view_enabled_range = m_viewer.get_view_enabled_range();
            // update visible range to take in account for skipped moves
            const uint32_t view_first_visible_gcode_id = m_viewer.get_vertex_at(view_visible_range[0]).gcode_id;
            while (view_visible_range[0] > view_enabled_range[0] && view_first_visible_gcode_id == m_viewer.get_vertex_at(view_visible_range[0] - 1).gcode_id) {
                --view_visible_range[0];
            }
            const bool keep_visible_range = view_visible_range != view_enabled_range;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            if (type == Preview::OptionType::Shells)
                m_shells.visible = !active;
            else
                m_viewer.toggle_option_visibility(libvgcode::convert(type));
#else
            switch (type)
            {
            case Preview::OptionType::CenterOfGravity: { m_cog.set_visible(!active); break; }
            case Preview::OptionType::ToolMarker:      { m_sequential_view.marker.set_visible(!active); break; }
            case Preview::OptionType::Shells:          { m_shells.visible = !active; break; }
            default:                                   {
                m_viewer.toggle_option_visibility(libvgcode::convert(type));
                break;
            }
            }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            std::optional<int> view_visible_range_min = keep_visible_range ? std::optional<int>{ static_cast<int>(view_visible_range[0]) } : std::nullopt;
            std::optional<int> view_visible_range_max = keep_visible_range ? std::optional<int>{ static_cast<int>(view_visible_range[1]) } : std::nullopt;
            wxGetApp().plater()->update_preview_moves_slider(view_visible_range_min, view_visible_range_max);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiPureWrap::COL_WINDOW_BACKGROUND);
            ImGui::BeginTooltip();
            ImGuiPureWrap::text(name);
            ImGui::EndTooltip();
            ImGui::PopStyleColor();
        }
    };

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
    toggle_button(Preview::OptionType::Travel, _u8L("Travel"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendTravel);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::Wipe, _u8L("Wipe"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendWipe);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::Retractions, _u8L("Retractions"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendRetract);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::Unretractions, _u8L("Deretractions"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendDeretract);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::Seams, _u8L("Seams"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendSeams);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::ToolChanges, _u8L("Tool changes"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendToolChanges);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::ColorChanges, _u8L("Color changes"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendColorChanges);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::PausePrints, _u8L("Print pauses"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendPausePrints);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::CustomGCodes, _u8L("Custom G-codes"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendCustomGCodes);
    });
    ImGui::SameLine();
    toggle_button(Preview::OptionType::CenterOfGravity, _u8L("Center of gravity"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendCOG);
    });
    ImGui::SameLine();
    if (!wxGetApp().is_gcode_viewer()) {
        toggle_button(Preview::OptionType::Shells, _u8L("Shells"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
            imgui.draw_icon(window, pos, size, ImGui::LegendShells);
        });
        ImGui::SameLine();
    }
    toggle_button(Preview::OptionType::ToolMarker, _u8L("Tool marker"), [&imgui](ImGuiWindow& window, const ImVec2& pos, float size) {
        imgui.draw_icon(window, pos, size, ImGui::LegendToolMarker);
    });

    bool size_dirty = !ImGui::GetCurrentWindow()->ScrollbarY && ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x != ImGui::GetWindowWidth();
    if (m_legend_resizer.dirty || size_dirty != m_legend_resizer.dirty) {
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
    }
    m_legend_resizer.dirty = size_dirty;

    legend_height = ImGui::GetWindowHeight();

    ImGuiPureWrap::end();
    ImGui::PopStyleVar();
}

} // namespace GUI
} // namespace Slic3r
