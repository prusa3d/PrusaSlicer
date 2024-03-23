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
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/format.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "GUI.hpp"
#include "DoubleSlider.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "GUI_ObjectManipulation.hpp"

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

static unsigned char buffer_id(EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(EMoveType::Retract);
}

static EMoveType buffer_type(unsigned char id) {
    return static_cast<EMoveType>(static_cast<unsigned char>(EMoveType::Retract) + id);
}

// Round to a bin with minimum two digits resolution.
// Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
static float round_to_bin(const float value)
{
//    assert(value >= 0);
    constexpr float const scale    [5] = { 100.f,  1000.f,  10000.f,  100000.f,  1000000.f };
    constexpr float const invscale [5] = { 0.01f,  0.001f,  0.0001f,  0.00001f,  0.000001f };
    constexpr float const threshold[5] = { 0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f };
    // Scaling factor, pointer to the tables above.
    int                   i            = 0;
    // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
    for (; value < threshold[i] && i < 4; ++ i) ;
    // At least on MSVC std::round() calls a complex function, which is pretty expensive.
    // our fast_round_up is much cheaper and it could be inlined.
//    return std::round(value * scale[i]) * invscale[i];
    double a = value * scale[i];
    assert(std::abs(a) < double(std::numeric_limits<int64_t>::max()));
    return fast_round_up<int64_t>(a) * invscale[i];
}

void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (!vbos.empty()) {
        glsafe(::glDeleteBuffers(static_cast<GLsizei>(vbos.size()), static_cast<const GLuint*>(vbos.data())));
        vbos.clear();
    }

#if ENABLE_GL_CORE_PROFILE
    if (!vaos.empty()) {
        glsafe(::glDeleteVertexArrays(static_cast<GLsizei>(vaos.size()), static_cast<const GLuint*>(vaos.data())));
        vaos.clear();
    }
#endif // ENABLE_GL_CORE_PROFILE

    sizes.clear();
    count = 0;
}

void GCodeViewer::InstanceVBuffer::Ranges::reset()
{
    for (Range& range : ranges) {
        // release gpu memory
        if (range.vbo > 0)
            glsafe(::glDeleteBuffers(1, &range.vbo));
    }

    ranges.clear();
}

void GCodeViewer::InstanceVBuffer::reset()
{
    s_ids.clear();
    buffer.clear();
    render_ranges.reset();
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (ibo > 0) {
        glsafe(::glDeleteBuffers(1, &ibo));
        ibo = 0;
    }

    vbo = 0;
    count = 0;
}

bool GCodeViewer::Path::matches(const GCodeProcessorResult::MoveVertex& move, bool account_for_volumetric_rate) const
{
    auto matches_percent = [](float value1, float value2, float max_percent) {
        return std::abs(value2 - value1) / value1 <= max_percent;
    };

    switch (move.type)
    {
    case EMoveType::Tool_change:
    case EMoveType::Color_change:
    case EMoveType::Pause_Print:
    case EMoveType::Custom_GCode:
    case EMoveType::Retract:
    case EMoveType::Unretract:
    case EMoveType::Seam:
    case EMoveType::Extrude: {
        // use rounding to reduce the number of generated paths
        if (account_for_volumetric_rate)
            return type == move.type && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id && role == move.extrusion_role &&
                move.position.z() <= sub_paths.front().first.position.z() && feedrate == move.feedrate && fan_speed == move.fan_speed &&
                height == round_to_bin(move.height) && width == round_to_bin(move.width) &&
                matches_percent(volumetric_rate, move.volumetric_rate(), 0.001f);
        else
            return type == move.type && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id && role == move.extrusion_role &&
                move.position.z() <= sub_paths.front().first.position.z() && feedrate == move.feedrate && fan_speed == move.fan_speed &&
                height == round_to_bin(move.height) && width == round_to_bin(move.width);
    }
    case EMoveType::Travel: {
        return type == move.type && feedrate == move.feedrate && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
    }
    default: { return false; }
    }
}

void GCodeViewer::TBuffer::Model::reset()
{
    instances.reset();
}

void GCodeViewer::TBuffer::reset()
{
    vertices.reset();
    for (IBuffer& buffer : indices) {
        buffer.reset();
    }

    indices.clear();
    paths.clear();
    render_paths.clear();
    model.reset();
}

void GCodeViewer::TBuffer::add_path(const GCodeProcessorResult::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id)
{
    Path::Endpoint endpoint = { b_id, i_id, s_id, move.position };
    // use rounding to reduce the number of generated paths
    paths.push_back({ move.type, move.extrusion_role, move.delta_extruder,
        round_to_bin(move.height), round_to_bin(move.width),
        move.feedrate, move.fan_speed, move.temperature,
        move.volumetric_rate(), move.extruder_id, move.cp_color_id, { { endpoint, endpoint } } });
}

void GCodeViewer::COG::render()
{
    if (!m_visible)
        return;

    init();

    GLShaderProgram* shader = wxGetApp().get_shader("toolpaths_cog");
    if (shader == nullptr)
        return;

    shader->start_using();

    glsafe(::glDisable(GL_DEPTH_TEST));

    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d model_matrix = Geometry::translation_transform(cog());
    if (m_fixed_size) {
        const double inv_zoom = camera.get_inv_zoom();
        model_matrix = model_matrix * Geometry::scale_transform(inv_zoom);
    }
    const Transform3d& view_matrix = camera.get_view_matrix();
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
    //imgui.set_next_window_pos(0.5f * static_cast<float>(cnv_size.get_width()), 0.0f, ImGuiCond_Always, 0.5f, 0.0f);
    //ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //ImGui::SetNextWindowBgAlpha(0.25f);
    //imgui.begin(std::string("COG"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    //imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _u8L("Center of mass") + ":");
    //ImGui::SameLine();
    //char buf[1024];
    //const Vec3d position = cog();
    //sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", position.x(), position.y(), position.z());
    //imgui.text(std::string(buf));

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

float GCodeViewer::Extrusions::Range::step_size(EType type) const
{
    switch (type)
    {
    default:
    case EType::Linear:      { return (max > min) ? (max - min) / (static_cast<float>(Range_Colors.size()) - 1.0f) : 0.0f; }
    case EType::Logarithmic: { return (max > min && min > 0.0f) ? ::log(max / min) / (static_cast<float>(Range_Colors.size()) - 1.0f) : 0.0f; }
    }
}

ColorRGBA GCodeViewer::Extrusions::Range::get_color_at(float value, EType type) const
{
    // Input value scaled to the colors range
    float global_t = 0.0f;
    const float step = step_size(type);
    if (step > 0.0f) {
        switch (type)
        {
        default:
        case EType::Linear:      { global_t = (value > min) ? (value - min) / step : 0.0f; break; }
        case EType::Logarithmic: { global_t = (value > min && min > 0.0f) ? ::log(value / min) / step : 0.0f; break; }
        }
    }

    const size_t color_max_idx = Range_Colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx  = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    return lerp(Range_Colors[color_low_idx], Range_Colors[color_high_idx], global_t - static_cast<float>(color_low_idx));
}

GCodeViewer::SequentialRangeCap::~SequentialRangeCap() {
    if (ibo > 0)
        glsafe(::glDeleteBuffers(1, &ibo));
}

void GCodeViewer::SequentialRangeCap::reset() {
    if (ibo > 0)
        glsafe(::glDeleteBuffers(1, &ibo));

    buffer = nullptr;
    ibo = 0;
#if ENABLE_GL_CORE_PROFILE
    vao = 0;
#endif // ENABLE_GL_CORE_PROFILE
    vbo = 0;
    color = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void GCodeViewer::SequentialView::Marker::init()
{
    m_model.init_from(stilized_arrow(16, 2.0f, 4.0f, 1.0f, 8.0f));
    m_model.set_color({ 1.0f, 1.0f, 1.0f, 0.5f });
}

void GCodeViewer::SequentialView::Marker::set_world_position(const Vec3f& position)
{    
    m_world_position = position;
    m_world_transform = (Geometry::translation_transform((position + m_model_z_offset * Vec3f::UnitZ()).cast<double>()) *
        Geometry::translation_transform(m_model.get_bounding_box().size().z() * Vec3d::UnitZ()) * Geometry::rotation_transform({ M_PI, 0.0, 0.0 })).cast<float>();
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
    const Transform3d& view_matrix = camera.get_view_matrix();
    const Transform3d model_matrix = m_world_transform.cast<double>();
    shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);

    m_model.render();

    shader->stop_using();

    glsafe(::glDisable(GL_BLEND));

    static float last_window_width = 0.0f;
    static size_t last_text_length = 0;

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(0.5f * static_cast<float>(cnv_size.get_width()), static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 0.5f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.25f);
    imgui.begin(std::string("ToolPosition"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _u8L("Tool position") + ":");
    ImGui::SameLine();
    char buf[1024];
    const Vec3f position = m_world_position + m_world_offset + m_z_offset * Vec3f::UnitZ();
    sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", position.x(), position.y(), position.z());
    imgui.text(std::string(buf));

    // force extra frame to automatically update window size
    const float width = ImGui::GetWindowWidth();
    const size_t length = strlen(buf);
    if (width != last_window_width || length != last_text_length) {
        last_window_width = width;
        last_text_length = length;
        imgui.set_requires_extra_frame();
    }

    imgui.end();
    ImGui::PopStyleVar();
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
        size_t last_block_id = 0;
        for (size_t i = 0; i < cumulative_lines_counts.size(); ++i) {
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
    };

    static const ImVec4 LINE_NUMBER_COLOR = ImGuiWrapper::COL_ORANGE_LIGHT;
    static const ImVec4 SELECTION_RECT_COLOR = ImGuiWrapper::COL_ORANGE_DARK;
    static const ImVec4 COMMAND_COLOR = { 0.8f, 0.8f, 0.0f, 1.0f };
    static const ImVec4 PARAMETERS_COLOR = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const ImVec4 COMMENT_COLOR = { 0.7f, 0.7f, 0.7f, 1.0f };
    static const ImVec4 ELLIPSIS_COLOR = { 0.0f, 0.7f, 0.0f, 1.0f };

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

    auto add_item_to_line = [&imgui](const std::string& txt, const ImVec4& color, float spacing, size_t& current_length) {
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
        imgui.text_colored(color, out_text);
        if (reduced) {
            ImGui::SameLine(0.0f, 0.0f);
            imgui.text_colored(ELLIPSIS_COLOR, "...");
        }

        return reduced;
    };

    imgui.set_next_window_pos(0.0f, top, ImGuiCond_Always, 0.0f, 0.0f);
    imgui.set_next_window_size(0.0f, wnd_height, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    imgui.begin(std::string("G-code"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

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

    imgui.end();
    ImGui::PopStyleVar();

    // request an extra frame if window's width changed
    if (m_max_line_length != max_line_length) {
        m_max_line_length = max_line_length;
        imgui.set_requires_extra_frame();
    }
}

void GCodeViewer::SequentialView::render(float legend_height)
{
    marker.render();
    float bottom = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height();
    if (wxGetApp().is_editor())
        bottom -= wxGetApp().plater()->get_view_toolbar().get_height();
    gcode_window.render(legend_height, bottom, static_cast<uint64_t>(gcode_ids[current.last]));
}

const std::array<ColorRGBA, static_cast<size_t>(GCodeExtrusionRole::Count)> GCodeViewer::Extrusion_Role_Colors{ {
    { 0.90f, 0.70f, 0.70f, 1.0f },   // GCodeExtrusionRole::None
    { 1.00f, 0.90f, 0.30f, 1.0f },   // GCodeExtrusionRole::Perimeter
    { 1.00f, 0.49f, 0.22f, 1.0f },   // GCodeExtrusionRole::ExternalPerimeter
    { 0.12f, 0.12f, 1.00f, 1.0f },   // GCodeExtrusionRole::OverhangPerimeter
    { 0.69f, 0.19f, 0.16f, 1.0f },   // GCodeExtrusionRole::InternalInfill
    { 0.59f, 0.33f, 0.80f, 1.0f },   // GCodeExtrusionRole::SolidInfill
    { 0.94f, 0.25f, 0.25f, 1.0f },   // GCodeExtrusionRole::TopSolidInfill
    { 1.00f, 0.55f, 0.41f, 1.0f },   // GCodeExtrusionRole::Ironing
    { 0.30f, 0.50f, 0.73f, 1.0f },   // GCodeExtrusionRole::BridgeInfill
    { 1.00f, 1.00f, 1.00f, 1.0f },   // GCodeExtrusionRole::GapFill
    { 0.00f, 0.53f, 0.43f, 1.0f },   // GCodeExtrusionRole::Skirt
    { 0.00f, 1.00f, 0.00f, 1.0f },   // GCodeExtrusionRole::SupportMaterial
    { 0.00f, 0.50f, 0.00f, 1.0f },   // GCodeExtrusionRole::SupportMaterialInterface
    { 0.70f, 0.89f, 0.67f, 1.0f },   // GCodeExtrusionRole::WipeTower
    { 0.37f, 0.82f, 0.58f, 1.0f },   // GCodeExtrusionRole::Custom
}};

const std::vector<ColorRGBA> GCodeViewer::Options_Colors{ {
    { 0.803f, 0.135f, 0.839f, 1.0f },   // Retractions
    { 0.287f, 0.679f, 0.810f, 1.0f },   // Unretractions
    { 0.900f, 0.900f, 0.900f, 1.0f },   // Seams
    { 0.758f, 0.744f, 0.389f, 1.0f },   // ToolChanges
    { 0.856f, 0.582f, 0.546f, 1.0f },   // ColorChanges
    { 0.322f, 0.942f, 0.512f, 1.0f },   // PausePrints
    { 0.886f, 0.825f, 0.262f, 1.0f }    // CustomGCodes
}};

const std::vector<ColorRGBA> GCodeViewer::Travel_Colors{ {
    { 0.219f, 0.282f, 0.609f, 1.0f }, // Move
    { 0.112f, 0.422f, 0.103f, 1.0f }, // Extrude
    { 0.505f, 0.064f, 0.028f, 1.0f }  // Retract
}};

#if 1
// Normal ranges
const std::vector<ColorRGBA> GCodeViewer::Range_Colors{ {
    { 0.043f, 0.173f, 0.478f, 1.0f }, // bluish
    { 0.075f, 0.349f, 0.522f, 1.0f },
    { 0.110f, 0.533f, 0.569f, 1.0f },
    { 0.016f, 0.839f, 0.059f, 1.0f },
    { 0.667f, 0.949f, 0.000f, 1.0f },
    { 0.988f, 0.975f, 0.012f, 1.0f },
    { 0.961f, 0.808f, 0.039f, 1.0f },
    { 0.890f, 0.533f, 0.125f, 1.0f },
    { 0.820f, 0.408f, 0.188f, 1.0f },
    { 0.761f, 0.322f, 0.235f, 1.0f },
    { 0.581f, 0.149f, 0.087f, 1.0f }  // reddish
}};
#else
// Detailed ranges
const std::vector<ColorRGBA> GCodeViewer::Range_Colors{ {
    { 0.043f, 0.173f, 0.478f, 1.0f }, // bluish
    { 0.5f * (0.043f + 0.075f), 0.5f * (0.173f + 0.349f), 0.5f * (0.478f + 0.522f), 1.0f },
    { 0.075f, 0.349f, 0.522f, 1.0f },
    { 0.5f * (0.075f + 0.110f), 0.5f * (0.349f + 0.533f), 0.5f * (0.522f + 0.569f), 1.0f },
    { 0.110f, 0.533f, 0.569f, 1.0f },
    { 0.5f * (0.110f + 0.016f), 0.5f * (0.533f + 0.839f), 0.5f * (0.569f + 0.059f), 1.0f },
    { 0.016f, 0.839f, 0.059f, 1.0f },
    { 0.5f * (0.016f + 0.667f), 0.5f * (0.839f + 0.949f), 0.5f * (0.059f + 0.000f), 1.0f },
    { 0.667f, 0.949f, 0.000f, 1.0f },
    { 0.5f * (0.667f + 0.988f), 0.5f * (0.949f + 0.975f), 0.5f * (0.000f + 0.012f), 1.0f },
    { 0.988f, 0.975f, 0.012f, 1.0f },
    { 0.5f * (0.988f + 0.961f), 0.5f * (0.975f + 0.808f), 0.5f * (0.012f + 0.039f), 1.0f },
    { 0.961f, 0.808f, 0.039f, 1.0f },
    { 0.5f * (0.961f + 0.890f), 0.5f * (0.808f + 0.533f), 0.5f * (0.039f + 0.125f), 1.0f },
    { 0.890f, 0.533f, 0.125f, 1.0f },
    { 0.5f * (0.890f + 0.820f), 0.5f * (0.533f + 0.408f), 0.5f * (0.125f + 0.188f), 1.0f },
    { 0.820f, 0.408f, 0.188f, 1.0f },
    { 0.5f * (0.820f + 0.761f), 0.5f * (0.408f + 0.322f), 0.5f * (0.188f + 0.235f), 1.0f },
    { 0.761f, 0.322f, 0.235f, 1.0f },
    { 0.5f * (0.761f + 0.581f), 0.5f * (0.322f + 0.149f), 0.5f * (0.235f + 0.087f), 1.0f },
    { 0.581f, 0.149f, 0.087f, 1.0f }  // reddishgit 
} };
#endif

const ColorRGBA GCodeViewer::Wipe_Color    = ColorRGBA::YELLOW();
const ColorRGBA GCodeViewer::Neutral_Color = ColorRGBA::DARK_GRAY();

GCodeViewer::GCodeViewer()
{
    m_extrusions.reset_role_visibility_flags();
    m_shells.volumes.set_use_raycasters(false);
//    m_sequential_view.skip_invisible_moves = true;
}

void GCodeViewer::init()
{
    if (m_gl_data_initialized)
        return;

    // initializes opengl data of TBuffers
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& buffer = m_buffers[i];
        EMoveType type = buffer_type(i);
        switch (type)
        {
        default: { break; }
        case EMoveType::Tool_change:
        case EMoveType::Color_change:
        case EMoveType::Pause_Print:
        case EMoveType::Custom_GCode:
        case EMoveType::Retract:
        case EMoveType::Unretract:
        case EMoveType::Seam: {
#if !DISABLE_GCODEVIEWER_INSTANCED_MODELS
            if (wxGetApp().is_gl_version_greater_or_equal_to(3, 3)) {
                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::InstancedModel;
                buffer.shader = "gouraud_light_instanced";
                buffer.model.model.init_from(diamond(16));
                buffer.model.color = option_color(type);
                buffer.model.instances.format = InstanceVBuffer::EFormat::InstancedModel;
            }
            else {
#endif // !DISABLE_GCODEVIEWER_INSTANCED_MODELS
                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::BatchedModel;
                buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
                buffer.shader = "gouraud_light";
                buffer.model.data = diamond(16);
                buffer.model.color = option_color(type);
                buffer.model.instances.format = InstanceVBuffer::EFormat::BatchedModel;
#if !DISABLE_GCODEVIEWER_INSTANCED_MODELS
            }
#endif // !DISABLE_GCODEVIEWER_INSTANCED_MODELS
                break;
        }
        case EMoveType::Wipe:
        case EMoveType::Extrude: {
            buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Triangle;
            buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
            buffer.shader = "gouraud_light";
            break;
        }
        case EMoveType::Travel: {
            buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Line;
            buffer.vertices.format = VBuffer::EFormat::Position;
#if ENABLE_GL_CORE_PROFILE
            // on MAC using the geometry shader of dashed_thick_lines is too slow
            buffer.shader = "flat";
//            buffer.shader = OpenGLManager::get_gl_info().is_core_profile() ? "dashed_thick_lines" : "flat";
#else
            buffer.shader = "flat";
#endif // ENABLE_GL_CORE_PROFILE
            break;
        }
        }

        set_toolpath_move_type_visible(EMoveType::Seam, true);
        set_toolpath_move_type_visible(EMoveType::Extrude, true);
    }

    // initializes tool marker
    m_sequential_view.marker.init();

    m_gl_data_initialized = true;
}

void GCodeViewer::load(const GCodeProcessorResult& gcode_result, const Print& print)
{
    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id &&
        (m_last_view_type == m_view_type || (m_last_view_type != EViewType::VolumetricRate && m_view_type != EViewType::VolumetricRate)))
        return;

    m_last_result_id = gcode_result.id;
    m_last_view_type = m_view_type;

    // release gpu memory, if used
    reset(); 

    m_sequential_view.gcode_window.load_gcode(gcode_result);

    if (wxGetApp().is_gcode_viewer())
        m_custom_gcode_per_print_z = gcode_result.custom_gcode_per_print_z;

    m_max_print_height = gcode_result.max_print_height;
    m_z_offset = gcode_result.z_offset;

    load_toolpaths(gcode_result);
    load_wipetower_shell(print);

    if (m_layers.empty())
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

    if (m_time_estimate_mode != PrintEstimatedStatistics::ETimeMode::Normal) {
        const float time = m_print_statistics.modes[static_cast<size_t>(m_time_estimate_mode)].time;
        if (time == 0.0f ||
            short_time(get_time_dhms(time)) == short_time(get_time_dhms(m_print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time)))
            m_time_estimate_mode = PrintEstimatedStatistics::ETimeMode::Normal;
    }

    m_conflict_result = gcode_result.conflict_result;
    if (m_conflict_result.has_value()) { m_conflict_result->layer = m_layers.get_l_at(m_conflict_result->_height); }
}

void GCodeViewer::refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (m_moves_count == 0)
        return;

    wxBusyCursor busy;

    if (m_view_type == EViewType::Tool && !gcode_result.extruder_colors.empty())
        // update tool colors from config stored in the gcode
        decode_colors(gcode_result.extruder_colors, m_tool_colors);
    else
        // update tool colors
        decode_colors(str_tool_colors, m_tool_colors);

    ColorRGBA default_color;
    decode_color("#FF8000", default_color);

    // ensure there are enough colors defined
    while (m_tool_colors.size() < std::max(size_t(1), gcode_result.extruders_count))
        m_tool_colors.push_back(default_color);

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_moves_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];

        switch (curr.type)
        {
        case EMoveType::Extrude:
        {
            m_extrusions.ranges.height.update_from(round_to_bin(curr.height));
            if (curr.extrusion_role != GCodeExtrusionRole::Custom || is_visible(GCodeExtrusionRole::Custom))
                m_extrusions.ranges.width.update_from(round_to_bin(curr.width));
            m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            m_extrusions.ranges.temperature.update_from(curr.temperature);
            if (curr.extrusion_role != GCodeExtrusionRole::Custom || is_visible(GCodeExtrusionRole::Custom))
                m_extrusions.ranges.volumetric_rate.update_from(round_to_bin(curr.volumetric_rate()));
            [[fallthrough]];
        }
        case EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);

            break;
        }
        default: { break; }
        }
    }

    for (size_t i = 0; i < gcode_result.print_statistics.modes.size(); ++i) {
        m_layers_times[i] = gcode_result.print_statistics.modes[i].layers_times;
    }

    for (size_t i = 0; i < m_layers_times.size(); ++i) {
        for (float time : m_layers_times[i]) {
            m_extrusions.ranges.layer_time[i].update_from(time);
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.refresh_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // update buffers' render paths
    refresh_render_paths(false, false);
    log_memory_used("Refreshed G-code extrusion paths, ");
}

void GCodeViewer::update_shells_color_by_extruder(const DynamicPrintConfig* config)
{
    if (config != nullptr)
        m_shells.volumes.update_colors_by_extruder(config);
}

void GCodeViewer::reset()
{
    m_moves_count = 0;
    for (TBuffer& buffer : m_buffers) {
        buffer.reset();
    }

    m_paths_bounding_box.reset();
    m_max_bounding_box.reset();
    m_max_print_height = 0.0f;
    m_z_offset = 0.0f;
    m_tool_colors = std::vector<ColorRGBA>();
    m_extruders_count = 0;
    m_extruder_ids = std::vector<unsigned char>();
    m_filament_diameters = std::vector<float>();
    m_filament_densities = std::vector<float>();
    m_extrusions.reset_ranges();
    m_layers.reset();
    m_layers_z_range = { 0, 0 };
    m_roles = std::vector<GCodeExtrusionRole>();
    m_print_statistics.reset();
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        m_layers_times[i] = std::vector<float>();
    }
    m_custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    m_sequential_view.gcode_window.reset();
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_all();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    m_contained_in_bed = true;
    m_legend_resizer.reset();
}

void GCodeViewer::render()
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_opengl();
    m_statistics.total_instances_gpu_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    glsafe(::glEnable(GL_DEPTH_TEST));
    render_shells();

    if (m_roles.empty())
        return;

    render_toolpaths();
    float legend_height = 0.0f;
    if (!m_layers.empty()) {
        render_legend(legend_height);
        if (m_sequential_view.current.last != m_sequential_view.endpoints.last) {
            m_sequential_view.marker.set_world_position(m_sequential_view.current_position);
            m_sequential_view.marker.set_world_offset(m_sequential_view.current_offset);
            m_sequential_view.marker.set_z_offset(m_z_offset);
            m_sequential_view.render(legend_height);
        }
    }
#if ENABLE_GCODE_VIEWER_STATISTICS
    render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

bool GCodeViewer::can_export_toolpaths() const
{
    return has_data() && m_buffers[buffer_id(EMoveType::Extrude)].render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle;
}

void GCodeViewer::update_sequential_view_current(unsigned int first, unsigned int last)
{
    auto is_visible = [this](unsigned int id) {
        for (const TBuffer& buffer : m_buffers) {
            if (buffer.visible) {
                for (const Path& path : buffer.paths) {
                    if (path.sub_paths.front().first.s_id <= id && id <= path.sub_paths.back().last.s_id)
                        return true;
                }
            }
        }
        return false;
    };

    const int first_diff = static_cast<int>(first) - static_cast<int>(m_sequential_view.last_current.first);
    const int last_diff = static_cast<int>(last) - static_cast<int>(m_sequential_view.last_current.last);

    unsigned int new_first = first;
    unsigned int new_last = last;

    if (m_sequential_view.skip_invisible_moves) {
        while (!is_visible(new_first)) {
            if (first_diff > 0)
                ++new_first;
            else
                --new_first;
        }

        while (!is_visible(new_last)) {
            if (last_diff > 0)
                ++new_last;
            else
                --new_last;
        }
    }

    m_sequential_view.current.first = new_first;
    m_sequential_view.current.last = new_last;
    m_sequential_view.last_current = m_sequential_view.current;

    refresh_render_paths(true, true);

    if (new_first != first || new_last != last)
        wxGetApp().plater()->update_preview_moves_slider();
}

bool GCodeViewer::is_toolpath_move_type_visible(EMoveType type) const
{
    size_t id = static_cast<size_t>(buffer_id(type));
    return (id < m_buffers.size()) ? m_buffers[id].visible : false;
}

void GCodeViewer::set_toolpath_move_type_visible(EMoveType type, bool visible)
{
    size_t id = static_cast<size_t>(buffer_id(type));
    if (id < m_buffers.size())
        m_buffers[id].visible = visible;
}

unsigned int GCodeViewer::get_options_visibility_flags() const
{
    auto set_flag = [](unsigned int flags, unsigned int flag, bool active) {
        return active ? (flags | (1 << flag)) : flags;
    };

    unsigned int flags = 0;
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Travel), is_toolpath_move_type_visible(EMoveType::Travel));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Wipe), is_toolpath_move_type_visible(EMoveType::Wipe));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Retractions), is_toolpath_move_type_visible(EMoveType::Retract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Unretractions), is_toolpath_move_type_visible(EMoveType::Unretract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Seams), is_toolpath_move_type_visible(EMoveType::Seam));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolChanges), is_toolpath_move_type_visible(EMoveType::Tool_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ColorChanges), is_toolpath_move_type_visible(EMoveType::Color_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::PausePrints), is_toolpath_move_type_visible(EMoveType::Pause_Print));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CustomGCodes), is_toolpath_move_type_visible(EMoveType::Custom_GCode));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CenterOfGravity), m_cog.is_visible());
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Shells), m_shells.visible);
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolMarker), m_sequential_view.marker.is_visible());
    return flags;
}

void GCodeViewer::set_options_visibility_from_flags(unsigned int flags)
{
    auto is_flag_set = [flags](unsigned int flag) {
        return (flags & (1 << flag)) != 0;
    };

    set_toolpath_move_type_visible(EMoveType::Travel, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Travel)));
    set_toolpath_move_type_visible(EMoveType::Wipe, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Wipe)));
    set_toolpath_move_type_visible(EMoveType::Retract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Retractions)));
    set_toolpath_move_type_visible(EMoveType::Unretract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Unretractions)));
    set_toolpath_move_type_visible(EMoveType::Seam, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Seams)));
    set_toolpath_move_type_visible(EMoveType::Tool_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolChanges)));
    set_toolpath_move_type_visible(EMoveType::Color_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ColorChanges)));
    set_toolpath_move_type_visible(EMoveType::Pause_Print, is_flag_set(static_cast<unsigned int>(Preview::OptionType::PausePrints)));
    set_toolpath_move_type_visible(EMoveType::Custom_GCode, is_flag_set(static_cast<unsigned int>(Preview::OptionType::CustomGCodes)));
    m_cog.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::CenterOfGravity)));
    m_shells.visible = is_flag_set(static_cast<unsigned int>(Preview::OptionType::Shells));
    m_sequential_view.marker.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolMarker)));
}

void GCodeViewer::set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range)
{
    bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
    bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
    m_layers_z_range = layers_z_range;
    refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
    wxGetApp().plater()->update_preview_moves_slider();
}

void GCodeViewer::export_toolpaths_to_obj(const char* filename) const
{
    if (filename == nullptr)
        return;

    if (!has_data())
        return;

    wxBusyCursor busy;

    // the data needed is contained into the Extrude TBuffer
    const TBuffer& t_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
    if (!t_buffer.has_data())
        return;

    if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Triangle)
        return;

    // collect color information to generate materials
    std::vector<ColorRGBA> colors;
    for (const RenderPath& path : t_buffer.render_paths) {
        colors.push_back(path.color);
    }
    sort_remove_duplicates(colors);

    // save materials file
    boost::filesystem::path mat_filename(filename);
    mat_filename.replace_extension("mtl");

    CNumericLocalesSetter locales_setter;

    FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths Materials\n");
    fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);

    unsigned int colors_count = 1;
    for (const ColorRGBA& color : colors) {
        fprintf(fp, "\nnewmtl material_%d\n", colors_count++);
        fprintf(fp, "Ka 1 1 1\n");
        fprintf(fp, "Kd %g %g %g\n", color.r(), color.g(), color.b());
        fprintf(fp, "Ks 0 0 0\n");
    }

    fclose(fp);

    // save geometry file
    fp = boost::nowide::fopen(filename, "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths\n");
    fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);
    fprintf(fp, "\nmtllib ./%s\n", mat_filename.filename().string().c_str());

    const size_t floats_per_vertex = t_buffer.vertices.vertex_size_floats();

    std::vector<Vec3f> out_vertices;
    std::vector<Vec3f> out_normals;

    struct VerticesOffset
    {
        unsigned int vbo;
        size_t offset;
    };
    std::vector<VerticesOffset> vertices_offsets;
    vertices_offsets.push_back({ t_buffer.vertices.vbos.front(), 0 });

    // get vertices/normals data from vertex buffers on gpu
    for (size_t i = 0; i < t_buffer.vertices.vbos.size(); ++i) {
        const size_t floats_count = t_buffer.vertices.sizes[i] / sizeof(float);
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, t_buffer.vertices.vbos[i]));
#if ENABLE_OPENGL_ES
        const VertexBuffer vertices = *static_cast<VertexBuffer*>(::glMapBufferRange(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(t_buffer.vertices.sizes[i]), GL_MAP_READ_BIT));
        glcheck();
        glsafe(::glUnmapBuffer(GL_ARRAY_BUFFER));
#else
        VertexBuffer vertices(floats_count);
        glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(t_buffer.vertices.sizes[i]), static_cast<void*>(vertices.data())));
#endif // ENABLE_OPENGL_ES
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        const size_t vertices_count = floats_count / floats_per_vertex;
        for (size_t j = 0; j < vertices_count; ++j) {
            const size_t base = j * floats_per_vertex;
            out_vertices.push_back({ vertices[base + 0], vertices[base + 1], vertices[base + 2] });
            out_normals.push_back({ vertices[base + 3], vertices[base + 4], vertices[base + 5] });
        }

        if (i < t_buffer.vertices.vbos.size() - 1)
            vertices_offsets.push_back({ t_buffer.vertices.vbos[i + 1], vertices_offsets.back().offset + vertices_count });
    }

    // save vertices to file
    fprintf(fp, "\n# vertices\n");
    for (const Vec3f& v : out_vertices) {
        fprintf(fp, "v %g %g %g\n", v.x(), v.y(), v.z());
    }

    // save normals to file
    fprintf(fp, "\n# normals\n");
    for (const Vec3f& n : out_normals) {
        fprintf(fp, "vn %g %g %g\n", n.x(), n.y(), n.z());
    }

    size_t i = 0;
    for (const ColorRGBA& color : colors) {
        // save material triangles to file
        fprintf(fp, "\nusemtl material_%zu\n", i + 1);
        fprintf(fp, "# triangles material %zu\n", i + 1);

        for (const RenderPath& render_path : t_buffer.render_paths) {
            if (render_path.color != color)
                continue;

            const IBuffer& ibuffer = t_buffer.indices[render_path.ibuffer_id];
            size_t vertices_offset = 0;
            for (size_t j = 0; j < vertices_offsets.size(); ++j) {
                const VerticesOffset& offset = vertices_offsets[j];
                if (offset.vbo == ibuffer.vbo) {
                    vertices_offset = offset.offset;
                    break;
                }
            }

            // get indices data from index buffer on gpu
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuffer.ibo));
            for (size_t j = 0; j < render_path.sizes.size(); ++j) {
#if ENABLE_OPENGL_ES
                const IndexBuffer indices = *static_cast<IndexBuffer*>(::glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLintptr>(render_path.offsets[j]), static_cast<GLsizeiptr>(render_path.sizes[j] * sizeof(IBufferType)),
                    GL_MAP_READ_BIT));
                glcheck();
                glsafe(::glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
#else
                IndexBuffer indices(render_path.sizes[j]);
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(render_path.offsets[j]),
                    static_cast<GLsizeiptr>(render_path.sizes[j] * sizeof(IBufferType)), static_cast<void*>(indices.data())));
#endif // ENABLE_OPENGL_ES

                const size_t triangles_count = render_path.sizes[j] / 3;
                for (size_t k = 0; k < triangles_count; ++k) {
                    const size_t base = k * 3;
                    const size_t v1 = 1 + static_cast<size_t>(indices[base + 0]) + vertices_offset;
                    const size_t v2 = 1 + static_cast<size_t>(indices[base + 1]) + vertices_offset;
                    const size_t v3 = 1 + static_cast<size_t>(indices[base + 2]) + vertices_offset;
                    if (v1 != v2)
                        // do not export dummy triangles
                        fprintf(fp, "f %zu//%zu %zu//%zu %zu//%zu\n", v1, v1, v2, v2, v3, v3);
                }
            }
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        }
        ++i;
    }

    fclose(fp);
}

void GCodeViewer::load_toolpaths(const GCodeProcessorResult& gcode_result)
{
    // max index buffer size, in bytes
    static const size_t IBUFFER_THRESHOLD_BYTES = 64 * 1024 * 1024;

    auto log_memory_usage = [this](const std::string& label, const std::vector<MultiVertexBuffer>& vertices, const std::vector<MultiIndexBuffer>& indices) {
        int64_t vertices_size = 0;
        for (const MultiVertexBuffer& buffers : vertices) {
            for (const VertexBuffer& buffer : buffers) {
                vertices_size += SLIC3R_STDVEC_MEMSIZE(buffer, float);
            }
        }
        int64_t indices_size = 0;
        for (const MultiIndexBuffer& buffers : indices) {
            for (const IndexBuffer& buffer : buffers) {
                indices_size += SLIC3R_STDVEC_MEMSIZE(buffer, IBufferType);
            }
        }
        log_memory_used(label, vertices_size + indices_size);
    };

    // format data into the buffers to be rendered as lines
    auto add_vertices_as_line = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, VertexBuffer& vertices) {
        auto add_vertex = [&vertices](const GCodeProcessorResult::MoveVertex& vertex) {
            // add position
            vertices.push_back(vertex.position.x());
            vertices.push_back(vertex.position.y());
            vertices.push_back(vertex.position.z());
        };

        // add previous vertex
        add_vertex(prev);
        // add current vertex
        add_vertex(curr);
    };
    auto add_indices_as_line = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer,
        unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id, bool account_for_volumetric_rate) {
            if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr, account_for_volumetric_rate)) {
                // add starting index
                indices.push_back(static_cast<IBufferType>(indices.size()));
                buffer.add_path(curr, ibuffer_id, indices.size() - 1, move_id - 1);
                buffer.paths.back().sub_paths.front().first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();
            if (last_path.sub_paths.front().first.i_id != last_path.sub_paths.back().last.i_id) {
                // add previous index
                indices.push_back(static_cast<IBufferType>(indices.size()));
            }

            // add current index
            indices.push_back(static_cast<IBufferType>(indices.size()));
            last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
    };

    // format data into the buffers to be rendered as solid
    auto add_vertices_as_solid = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer,
        unsigned int vbuffer_id, VertexBuffer& vertices, size_t move_id, bool account_for_volumetric_rate) {
        auto store_vertex = [](VertexBuffer& vertices, const Vec3f& position, const Vec3f& normal) {
            // append position
            vertices.push_back(position.x());
            vertices.push_back(position.y());
            vertices.push_back(position.z());
            // append normal
            vertices.push_back(normal.x());
            vertices.push_back(normal.y());
            vertices.push_back(normal.z());
        };

        if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr, account_for_volumetric_rate)) {
            buffer.add_path(curr, vbuffer_id, vertices.size(), move_id - 1);
            buffer.paths.back().sub_paths.back().first.position = prev.position;
        }

        Path& last_path = buffer.paths.back();

        const Vec3f dir = (curr.position - prev.position).normalized();
        const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
        const Vec3f left = -right;
        const Vec3f up = right.cross(dir);
        const Vec3f down = -up;
        const float half_width = 0.5f * last_path.width;
        const float half_height = 0.5f * last_path.height;
        const Vec3f prev_pos = prev.position - half_height * up;
        const Vec3f curr_pos = curr.position - half_height * up;
        const Vec3f d_up = half_height * up;
        const Vec3f d_down = -half_height * up;
        const Vec3f d_right = half_width * right;
        const Vec3f d_left = -half_width * right;

        // vertices 1st endpoint
        if (last_path.vertices_count() == 1 || vertices.empty()) {
            // 1st segment or restart into a new vertex buffer
            // ===============================================
            store_vertex(vertices, prev_pos + d_up, up);
            store_vertex(vertices, prev_pos + d_right, right);
            store_vertex(vertices, prev_pos + d_down, down);
            store_vertex(vertices, prev_pos + d_left, left);
        }
        else {
            // any other segment
            // =================
            store_vertex(vertices, prev_pos + d_right, right);
            store_vertex(vertices, prev_pos + d_left, left);
        }

        // vertices 2nd endpoint
        store_vertex(vertices, curr_pos + d_up, up);
        store_vertex(vertices, curr_pos + d_right, right);
        store_vertex(vertices, curr_pos + d_down, down);
        store_vertex(vertices, curr_pos + d_left, left);

        last_path.sub_paths.back().last = { vbuffer_id, vertices.size(), move_id, curr.position };
    };
    auto add_indices_as_solid = [&](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr,
        const GCodeProcessorResult::MoveVertex* next, TBuffer& buffer, size_t& vbuffer_size, unsigned int ibuffer_id,
        IndexBuffer& indices, size_t move_id, bool account_for_volumetric_rate) {
            static Vec3f prev_dir;
            static Vec3f prev_up;
            static float sq_prev_length;
            auto store_triangle = [](IndexBuffer& indices, IBufferType i1, IBufferType i2, IBufferType i3) {
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i3);
            };
            auto append_dummy_cap = [store_triangle](IndexBuffer& indices, IBufferType id) {
                store_triangle(indices, id, id, id);
                store_triangle(indices, id, id, id);
            };
            auto convert_vertices_offset = [](size_t vbuffer_size, const std::array<int, 8>& v_offsets) {
                std::array<IBufferType, 8> ret = {
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[0]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[1]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[2]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[3]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[4]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[5]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[6]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[7])
                };
                return ret;
            };
            auto append_starting_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[0], v_offsets[2], v_offsets[1]);
                store_triangle(indices, v_offsets[0], v_offsets[3], v_offsets[2]);
            };
            auto append_stem_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[0], v_offsets[1], v_offsets[4]);
                store_triangle(indices, v_offsets[1], v_offsets[5], v_offsets[4]);
                store_triangle(indices, v_offsets[1], v_offsets[2], v_offsets[5]);
                store_triangle(indices, v_offsets[2], v_offsets[6], v_offsets[5]);
                store_triangle(indices, v_offsets[2], v_offsets[3], v_offsets[6]);
                store_triangle(indices, v_offsets[3], v_offsets[7], v_offsets[6]);
                store_triangle(indices, v_offsets[3], v_offsets[0], v_offsets[7]);
                store_triangle(indices, v_offsets[0], v_offsets[4], v_offsets[7]);
            };
            auto append_ending_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[4], v_offsets[6], v_offsets[7]);
                store_triangle(indices, v_offsets[4], v_offsets[5], v_offsets[6]);
            };

            if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr, account_for_volumetric_rate)) {
                buffer.add_path(curr, ibuffer_id, indices.size(), move_id - 1);
                buffer.paths.back().sub_paths.back().first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();

            const Vec3f dir = (curr.position - prev.position).normalized();
            const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
            const Vec3f up = right.cross(dir);
            const float sq_length = (curr.position - prev.position).squaredNorm();

            const std::array<IBufferType, 8> first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { 0, 1, 2, 3, 4, 5, 6, 7 });
            const std::array<IBufferType, 8> non_first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { -4, 0, -2, 1, 2, 3, 4, 5 });
            const bool is_first_segment = (last_path.vertices_count() == 1);
            if (is_first_segment || vbuffer_size == 0) {
                // 1st segment or restart into a new vertex buffer
                // ===============================================
                if (is_first_segment)
                    // starting cap triangles
                    append_starting_cap_triangles(indices, first_seg_v_offsets);
                // dummy triangles outer corner cap
                append_dummy_cap(indices, vbuffer_size);

                // stem triangles
                append_stem_triangles(indices, first_seg_v_offsets);

                vbuffer_size += 8;
            }
            else {
                // any other segment
                // =================
                float displacement = 0.0f;
                const float cos_dir = prev_dir.dot(dir);
                if (cos_dir > -0.9998477f) {
                    // if the angle between adjacent segments is smaller than 179 degrees
                    const Vec3f med_dir = (prev_dir + dir).normalized();
                    const float half_width = 0.5f * last_path.width;
                    displacement = half_width * ::tan(::acos(std::clamp(dir.dot(med_dir), -1.0f, 1.0f)));
                }

                const float sq_displacement = sqr(displacement);
                const bool can_displace = displacement > 0.0f && sq_displacement < sq_prev_length && sq_displacement < sq_length;

                const bool is_right_turn = prev_up.dot(prev_dir.cross(dir)) <= 0.0f;
                // whether the angle between adjacent segments is greater than 45 degrees
                const bool is_sharp = cos_dir < 0.7071068f;

                bool right_displaced = false;
                bool left_displaced = false;

                if (!is_sharp && can_displace) {
                    if (is_right_turn)
                        left_displaced = true;
                    else
                        right_displaced = true;
                }

                // triangles outer corner cap
                if (is_right_turn) {
                    if (left_displaced)
                        // dummy triangles
                        append_dummy_cap(indices, vbuffer_size);
                    else {
                        store_triangle(indices, vbuffer_size - 4, vbuffer_size + 1, vbuffer_size - 1);
                        store_triangle(indices, vbuffer_size + 1, vbuffer_size - 2, vbuffer_size - 1);
                    }
                }
                else {
                    if (right_displaced)
                        // dummy triangles
                        append_dummy_cap(indices, vbuffer_size);
                    else {
                        store_triangle(indices, vbuffer_size - 4, vbuffer_size - 3, vbuffer_size + 0);
                        store_triangle(indices, vbuffer_size - 3, vbuffer_size - 2, vbuffer_size + 0);
                    }
                }

                // stem triangles
                append_stem_triangles(indices, non_first_seg_v_offsets);

                vbuffer_size += 6;
            }

            if (next != nullptr && (curr.type != next->type || !last_path.matches(*next, account_for_volumetric_rate)))
                // ending cap triangles
                append_ending_cap_triangles(indices, is_first_segment ? first_seg_v_offsets : non_first_seg_v_offsets);

            last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
            prev_dir = dir;
            prev_up = up;
            sq_prev_length = sq_length;
    };

    // format data into the buffers to be rendered as instanced model
    auto add_model_instance = [](const GCodeProcessorResult::MoveVertex& curr, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
        // append position
        instances.push_back(curr.position.x());
        instances.push_back(curr.position.y());
        instances.push_back(curr.position.z());
        // append width
        instances.push_back(curr.width);
        // append height
        instances.push_back(curr.height);

        // append id
        instances_ids.push_back(move_id);
    };

    // format data into the buffers to be rendered as batched model
    auto add_vertices_as_model_batch = [](const GCodeProcessorResult::MoveVertex& curr, const GLModel::Geometry& data, VertexBuffer& vertices, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
        const double width = static_cast<double>(1.5f * curr.width);
        const double height = static_cast<double>(1.5f * curr.height);

        const Transform3d trafo = Geometry::translation_transform((curr.position - 0.5f * curr.height * Vec3f::UnitZ()).cast<double>()) * 
          Geometry::scale_transform({ width, width, height });
        const Eigen::Matrix<double, 3, 3, Eigen::DontAlign> normal_matrix = trafo.matrix().template block<3, 3>(0, 0).inverse().transpose();

        // append vertices
        const size_t vertices_count = data.vertices_count();
        for (size_t i = 0; i < vertices_count; ++i) {
            // append position
            const Vec3d position = trafo * data.extract_position_3(i).cast<double>();
            vertices.push_back(float(position.x()));
            vertices.push_back(float(position.y()));
            vertices.push_back(float(position.z()));

            // append normal
            const Vec3d normal = normal_matrix * data.extract_normal_3(i).cast<double>();
            vertices.push_back(float(normal.x()));
            vertices.push_back(float(normal.y()));
            vertices.push_back(float(normal.z()));
        }

        // append instance position
        instances.push_back(curr.position.x());
        instances.push_back(curr.position.y());
        instances.push_back(curr.position.z());
        // append instance id
        instances_ids.push_back(move_id);
    };

    auto add_indices_as_model_batch = [](const GLModel::Geometry& data, IndexBuffer& indices, IBufferType base_index) {
        const size_t indices_count = data.indices_count();
        for (size_t i = 0; i < indices_count; ++i) {
            indices.push_back(static_cast<IBufferType>(data.extract_index(i) + base_index));
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
    m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessorResult::MoveVertex);
    m_statistics.results_time = gcode_result.time;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    m_max_bounding_box.reset();

    m_moves_count = gcode_result.moves.size();
    if (m_moves_count == 0)
        return;

    m_extruders_count = gcode_result.extruders_count;

    unsigned int progress_count = 0;
    static const unsigned int progress_threshold = 1000;
    wxProgressDialog* progress_dialog = wxGetApp().is_gcode_viewer() ?
        new wxProgressDialog(_L("Generating toolpaths"), "...",
            100, wxGetApp().mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL) : nullptr;

    wxBusyCursor busy;

    // extract approximate paths bounding box from result
    for (const GCodeProcessorResult::MoveVertex& move : gcode_result.moves) {
        if (wxGetApp().is_gcode_viewer())
            // for the gcode viewer we need to take in account all moves to correctly size the printbed
            m_paths_bounding_box.merge(move.position.cast<double>());
        else {
            if (move.type == EMoveType::Extrude && move.extrusion_role != GCodeExtrusionRole::Custom && move.width != 0.0f && move.height != 0.0f)
                m_paths_bounding_box.merge(move.position.cast<double>());
        }
    }

    if (wxGetApp().is_editor())
        m_contained_in_bed = wxGetApp().plater()->build_volume().all_paths_inside(gcode_result, m_paths_bounding_box);

    m_cog.reset();

    m_sequential_view.gcode_ids.clear();
    for (size_t i = 0; i < gcode_result.moves.size(); ++i) {
        const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
        if (move.type != EMoveType::Seam)
            m_sequential_view.gcode_ids.push_back(move.gcode_id);
    }

    bool account_for_volumetric_rate = m_view_type == EViewType::VolumetricRate;

    std::vector<MultiVertexBuffer> vertices(m_buffers.size());
    std::vector<MultiIndexBuffer> indices(m_buffers.size());
    std::vector<InstanceBuffer> instances(m_buffers.size());
    std::vector<InstanceIdBuffer> instances_ids(m_buffers.size());
    std::vector<InstancesOffsets> instances_offsets(m_buffers.size());
    std::vector<float> options_zs;

    std::vector<size_t> biased_seams_ids;

    // toolpaths data -> extract vertices from result
    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
        if (curr.type == EMoveType::Seam)
            biased_seams_ids.push_back(i - biased_seams_ids.size() - 1);

        const size_t move_id = i - biased_seams_ids.size();

        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];

        if (curr.type == EMoveType::Extrude &&
            curr.extrusion_role != GCodeExtrusionRole::Skirt &&
            curr.extrusion_role != GCodeExtrusionRole::SupportMaterial &&
            curr.extrusion_role != GCodeExtrusionRole::SupportMaterialInterface &&
            curr.extrusion_role != GCodeExtrusionRole::WipeTower &&
            curr.extrusion_role != GCodeExtrusionRole::Custom) {
            const Vec3d curr_pos = curr.position.cast<double>();
            const Vec3d prev_pos = prev.position.cast<double>();
            m_cog.add_segment(curr_pos, prev_pos, curr.mm3_per_mm * (curr_pos - prev_pos).norm());
        }

        // update progress dialog
        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(i) / (2.0f * float(m_moves_count))),
                _L("Generating vertex buffer") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const unsigned char id = buffer_id(curr.type);
        TBuffer& t_buffer = m_buffers[id];
        MultiVertexBuffer& v_multibuffer = vertices[id];
        InstanceBuffer& inst_buffer = instances[id];
        InstanceIdBuffer& inst_id_buffer = instances_ids[id];
        InstancesOffsets& inst_offsets = instances_offsets[id];

        // ensure there is at least one vertex buffer
        if (v_multibuffer.empty())
            v_multibuffer.push_back(VertexBuffer());

        // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
        // add another vertex buffer
        size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : t_buffer.max_vertices_per_segment_size_bytes();
        if (v_multibuffer.back().size() * sizeof(float) > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
            v_multibuffer.push_back(VertexBuffer());
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                Path& last_path = t_buffer.paths.back();
                if (prev.type == curr.type && last_path.matches(curr, account_for_volumetric_rate))
                    last_path.add_sub_path(prev, static_cast<unsigned int>(v_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        VertexBuffer& v_buffer = v_multibuffer.back();

        switch (t_buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line:     { add_vertices_as_line(prev, curr, v_buffer); break; }
        case TBuffer::ERenderPrimitiveType::Triangle: { add_vertices_as_solid(prev, curr, t_buffer, static_cast<unsigned int>(v_multibuffer.size()) - 1, v_buffer, move_id, account_for_volumetric_rate); break; }
        case TBuffer::ERenderPrimitiveType::InstancedModel:
        {
            add_model_instance(curr, inst_buffer, inst_id_buffer, move_id);
            inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.instances_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            break;
        }
        case TBuffer::ERenderPrimitiveType::BatchedModel:
        {
            add_vertices_as_model_batch(curr, t_buffer.model.data, v_buffer, inst_buffer, inst_id_buffer, move_id);
            inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.batched_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            break;
        }
        }

        // collect options zs for later use
        if (curr.type == EMoveType::Pause_Print || curr.type == EMoveType::Custom_GCode) {
            const float* const last_z = options_zs.empty() ? nullptr : &options_zs.back();
            if (last_z == nullptr || curr.position[2] < *last_z - EPSILON || *last_z + EPSILON < curr.position[2])
                options_zs.emplace_back(curr.position[2]);
        }
    }

    // smooth toolpaths corners for the given TBuffer using triangles
    auto smooth_triangle_toolpaths_corners = [&gcode_result, &biased_seams_ids](const TBuffer& t_buffer, MultiVertexBuffer& v_multibuffer) {
        auto extract_position_at = [](const VertexBuffer& vertices, size_t offset) {
            return Vec3f(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
        };
        auto update_position_at = [](VertexBuffer& vertices, size_t offset, const Vec3f& position) {
            vertices[offset + 0] = position.x();
            vertices[offset + 1] = position.y();
            vertices[offset + 2] = position.z();
        };
        auto match_right_vertices = [&](const Path::Sub_Path& prev_sub_path, const Path::Sub_Path& next_sub_path,
            size_t curr_s_id, size_t vertex_size_floats, const Vec3f& displacement_vec) {
                if (&prev_sub_path == &next_sub_path) { // previous and next segment are both contained into to the same vertex buffer
                    VertexBuffer& vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                    // offset into the vertex buffer of the next segment 1st vertex
                    const size_t next_1st_offset = (prev_sub_path.last.s_id - curr_s_id) * 6 * vertex_size_floats;
                    // offset into the vertex buffer of the right vertex of the previous segment 
                    const size_t prev_right_offset = prev_sub_path.last.i_id - next_1st_offset - 3 * vertex_size_floats;
                    // new position of the right vertices
                    const Vec3f shared_vertex = extract_position_at(vbuffer, prev_right_offset) + displacement_vec;
                    // update previous segment
                    update_position_at(vbuffer, prev_right_offset, shared_vertex);
                    // offset into the vertex buffer of the right vertex of the next segment
                    const size_t next_right_offset = next_sub_path.last.i_id - next_1st_offset;
                    // update next segment
                    update_position_at(vbuffer, next_right_offset, shared_vertex);
                }
                else { // previous and next segment are contained into different vertex buffers
                    VertexBuffer& prev_vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                    VertexBuffer& next_vbuffer = v_multibuffer[next_sub_path.first.b_id];
                    // offset into the previous vertex buffer of the right vertex of the previous segment 
                    const size_t prev_right_offset = prev_sub_path.last.i_id - 3 * vertex_size_floats;
                    // new position of the right vertices
                    const Vec3f shared_vertex = extract_position_at(prev_vbuffer, prev_right_offset) + displacement_vec;
                    // update previous segment
                    update_position_at(prev_vbuffer, prev_right_offset, shared_vertex);
                    // offset into the next vertex buffer of the right vertex of the next segment
                    const size_t next_right_offset = next_sub_path.first.i_id + 1 * vertex_size_floats;
                    // update next segment
                    update_position_at(next_vbuffer, next_right_offset, shared_vertex);
                }
        };
        auto match_left_vertices = [&](const Path::Sub_Path& prev_sub_path, const Path::Sub_Path& next_sub_path,
            size_t curr_s_id, size_t vertex_size_floats, const Vec3f& displacement_vec) {
                if (&prev_sub_path == &next_sub_path) { // previous and next segment are both contained into to the same vertex buffer
                    VertexBuffer& vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                    // offset into the vertex buffer of the next segment 1st vertex
                    const size_t next_1st_offset = (prev_sub_path.last.s_id - curr_s_id) * 6 * vertex_size_floats;
                    // offset into the vertex buffer of the left vertex of the previous segment 
                    const size_t prev_left_offset = prev_sub_path.last.i_id - next_1st_offset - 1 * vertex_size_floats;
                    // new position of the left vertices
                    const Vec3f shared_vertex = extract_position_at(vbuffer, prev_left_offset) + displacement_vec;
                    // update previous segment
                    update_position_at(vbuffer, prev_left_offset, shared_vertex);
                    // offset into the vertex buffer of the left vertex of the next segment
                    const size_t next_left_offset = next_sub_path.last.i_id - next_1st_offset + 1 * vertex_size_floats;
                    // update next segment
                    update_position_at(vbuffer, next_left_offset, shared_vertex);
                }
                else { // previous and next segment are contained into different vertex buffers
                    VertexBuffer& prev_vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                    VertexBuffer& next_vbuffer = v_multibuffer[next_sub_path.first.b_id];
                    // offset into the previous vertex buffer of the left vertex of the previous segment 
                    const size_t prev_left_offset = prev_sub_path.last.i_id - 1 * vertex_size_floats;
                    // new position of the left vertices
                    const Vec3f shared_vertex = extract_position_at(prev_vbuffer, prev_left_offset) + displacement_vec;
                    // update previous segment
                    update_position_at(prev_vbuffer, prev_left_offset, shared_vertex);
                    // offset into the next vertex buffer of the left vertex of the next segment
                    const size_t next_left_offset = next_sub_path.first.i_id + 3 * vertex_size_floats;
                    // update next segment
                    update_position_at(next_vbuffer, next_left_offset, shared_vertex);
                }
        };

        auto extract_move_id = [&biased_seams_ids](size_t id) {
            size_t new_id = size_t(-1);
            auto it = std::lower_bound(biased_seams_ids.begin(), biased_seams_ids.end(), id);
            if (it == biased_seams_ids.end())
                new_id = id + biased_seams_ids.size();
            else {
                if (it == biased_seams_ids.begin() && *it < id)
                    new_id = id;
                else if (it != biased_seams_ids.begin())
                    new_id = id + std::distance(biased_seams_ids.begin(), it);
            }
            return (new_id == size_t(-1)) ? id : new_id;
        };

        const size_t vertex_size_floats = t_buffer.vertices.vertex_size_floats();
        for (const Path& path : t_buffer.paths) {
            // the two segments of the path sharing the current vertex may belong
            // to two different vertex buffers
            size_t prev_sub_path_id = 0;
            size_t next_sub_path_id = 0;
            const size_t path_vertices_count = path.vertices_count();
            const float half_width = 0.5f * path.width;
            for (size_t j = 1; j < path_vertices_count - 1; ++j) {
                const size_t curr_s_id = path.sub_paths.front().first.s_id + j;
                const size_t move_id = extract_move_id(curr_s_id);
                const Vec3f& prev = gcode_result.moves[move_id - 1].position;
                const Vec3f& curr = gcode_result.moves[move_id].position;
                const Vec3f& next = gcode_result.moves[move_id + 1].position;

                // select the subpaths which contains the previous/next segments
                if (!path.sub_paths[prev_sub_path_id].contains(curr_s_id))
                    ++prev_sub_path_id;
                if (!path.sub_paths[next_sub_path_id].contains(curr_s_id + 1))
                    ++next_sub_path_id;
                const Path::Sub_Path& prev_sub_path = path.sub_paths[prev_sub_path_id];
                const Path::Sub_Path& next_sub_path = path.sub_paths[next_sub_path_id];

                const Vec3f prev_dir = (curr - prev).normalized();
                const Vec3f prev_right = Vec3f(prev_dir.y(), -prev_dir.x(), 0.0f).normalized();
                const Vec3f prev_up = prev_right.cross(prev_dir);

                const Vec3f next_dir = (next - curr).normalized();

                const bool is_right_turn = prev_up.dot(prev_dir.cross(next_dir)) <= 0.0f;
                const float cos_dir = prev_dir.dot(next_dir);
                // whether the angle between adjacent segments is greater than 45 degrees
                const bool is_sharp = cos_dir < 0.7071068f;

                float displacement = 0.0f;
                if (cos_dir > -0.9998477f) {
                    // if the angle between adjacent segments is smaller than 179 degrees
                    const Vec3f med_dir = (prev_dir + next_dir).normalized();
                    displacement = half_width * ::tan(::acos(std::clamp(next_dir.dot(med_dir), -1.0f, 1.0f)));
                }

                const float sq_prev_length = (curr - prev).squaredNorm();
                const float sq_next_length = (next - curr).squaredNorm();
                const float sq_displacement = sqr(displacement);
                const bool can_displace = displacement > 0.0f && sq_displacement < sq_prev_length && sq_displacement < sq_next_length;

                if (can_displace) {
                    // displacement to apply to the vertices to match
                    const Vec3f displacement_vec = displacement * prev_dir;
                    // matches inner corner vertices
                    if (is_right_turn)
                        match_right_vertices(prev_sub_path, next_sub_path, curr_s_id, vertex_size_floats, -displacement_vec);
                    else
                        match_left_vertices(prev_sub_path, next_sub_path, curr_s_id, vertex_size_floats, -displacement_vec);

                    if (!is_sharp) {
                        // matches outer corner vertices
                        if (is_right_turn)
                            match_left_vertices(prev_sub_path, next_sub_path, curr_s_id, vertex_size_floats, displacement_vec);
                        else
                            match_right_vertices(prev_sub_path, next_sub_path, curr_s_id, vertex_size_floats, displacement_vec);
                    }
                }
            }
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto load_vertices_time = std::chrono::high_resolution_clock::now();
    m_statistics.load_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // smooth toolpaths corners for TBuffers using triangles
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        const TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle)
            smooth_triangle_toolpaths_corners(t_buffer, vertices[i]);
    }

    // dismiss, no more needed
    std::vector<size_t>().swap(biased_seams_ids);

    for (MultiVertexBuffer& v_multibuffer : vertices) {
        for (VertexBuffer& v_buffer : v_multibuffer) {
            v_buffer.shrink_to_fit();
        }
    }

    // move the wipe toolpaths half height up to render them on proper position
    MultiVertexBuffer& wipe_vertices = vertices[buffer_id(EMoveType::Wipe)];
    for (VertexBuffer& v_buffer : wipe_vertices) {
        for (size_t i = 2; i < v_buffer.size(); i += 3) {
            v_buffer[i] += 0.5f * GCodeProcessor::Wipe_Height;
        }
    }

    // send vertices data to gpu, where needed
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
            const InstanceBuffer& inst_buffer = instances[i];
            if (!inst_buffer.empty()) {
                t_buffer.model.instances.buffer = inst_buffer;
                t_buffer.model.instances.s_ids = instances_ids[i];
                t_buffer.model.instances.offsets = instances_offsets[i];
            }
        }
        else {
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                const InstanceBuffer& inst_buffer = instances[i];
                if (!inst_buffer.empty()) {
                    t_buffer.model.instances.buffer = inst_buffer;
                    t_buffer.model.instances.s_ids = instances_ids[i];
                    t_buffer.model.instances.offsets = instances_offsets[i];
                }
            }
            const MultiVertexBuffer& v_multibuffer = vertices[i];
            for (const VertexBuffer& v_buffer : v_multibuffer) {
                const size_t size_elements = v_buffer.size();
                const size_t size_bytes = size_elements * sizeof(float);
                const size_t vertices_count = size_elements / t_buffer.vertices.vertex_size_floats();
                t_buffer.vertices.count += vertices_count;

#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.total_vertices_gpu_size += static_cast<int64_t>(size_bytes);
                m_statistics.max_vbuffer_gpu_size = std::max(m_statistics.max_vbuffer_gpu_size, static_cast<int64_t>(size_bytes));
                ++m_statistics.vbuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

#if ENABLE_GL_CORE_PROFILE
                GLuint vao_id = 0;
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0)) {
                    glsafe(::glGenVertexArrays(1, &vao_id));
                    glsafe(::glBindVertexArray(vao_id));
                }
#endif // ENABLE_GL_CORE_PROFILE

                GLuint vbo_id = 0;
                glsafe(::glGenBuffers(1, &vbo_id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, vbo_id));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, size_bytes, v_buffer.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0)) {
                    glsafe(::glBindVertexArray(0));
                    t_buffer.vertices.vaos.push_back(static_cast<unsigned int>(vao_id));
                }
#endif // ENABLE_GL_CORE_PROFILE
                t_buffer.vertices.vbos.push_back(static_cast<unsigned int>(vbo_id));
                t_buffer.vertices.sizes.push_back(size_bytes);
            }
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto smooth_vertices_time = std::chrono::high_resolution_clock::now();
    m_statistics.smooth_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - load_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    log_memory_usage("Loaded G-code generated vertex buffers ", vertices, indices);

    // dismiss vertices data, no more needed
    std::vector<MultiVertexBuffer>().swap(vertices);
    std::vector<InstanceBuffer>().swap(instances);
    std::vector<InstanceIdBuffer>().swap(instances_ids);

    // toolpaths data -> extract indices from result
    // paths may have been filled while extracting vertices,
    // so reset them, they will be filled again while extracting indices
    for (TBuffer& buffer : m_buffers) {
        buffer.paths.clear();
    }

    // variable used to keep track of the current vertex buffers index and size
    using CurrVertexBuffer = std::pair<unsigned int, size_t>;
    std::vector<CurrVertexBuffer> curr_vertex_buffers(m_buffers.size(), { 0, 0 });

#if ENABLE_GL_CORE_PROFILE
    // variable used to keep track of the vertex buffers ids
    using VIndexList = std::vector<unsigned int>;
    std::vector<VIndexList> vao_indices(m_buffers.size());
    std::vector<VIndexList> vbo_indices(m_buffers.size());
#else
    // variable used to keep track of the vertex buffers ids
    using VboIndexList = std::vector<unsigned int>;
    std::vector<VboIndexList> vbo_indices(m_buffers.size());
#endif // ENABLE_GL_CORE_PROFILE

    size_t seams_count = 0;

    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
        if (curr.type == EMoveType::Seam)
            ++seams_count;

        const size_t move_id = i - seams_count;

        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessorResult::MoveVertex* next = nullptr;
        if (i < m_moves_count - 1)
            next = &gcode_result.moves[i + 1];

        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(m_moves_count + i) / (2.0f * float(m_moves_count))),
                _L("Generating index buffers") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const unsigned char id = buffer_id(curr.type);
        TBuffer& t_buffer = m_buffers[id];
        MultiIndexBuffer& i_multibuffer = indices[id];
        CurrVertexBuffer& curr_vertex_buffer = curr_vertex_buffers[id];
#if ENABLE_GL_CORE_PROFILE
        VIndexList& vao_index_list = vao_indices[id];
        VIndexList& vbo_index_list = vbo_indices[id];
#else
        VboIndexList& vbo_index_list = vbo_indices[id];
#endif // ENABLE_GL_CORE_PROFILE

        // ensure there is at least one index buffer
        if (i_multibuffer.empty()) {
            i_multibuffer.push_back(IndexBuffer());
#if ENABLE_GL_CORE_PROFILE
            if (!t_buffer.vertices.vaos.empty() && OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                vao_index_list.push_back(t_buffer.vertices.vaos[curr_vertex_buffer.first]);

            if (!t_buffer.vertices.vbos.empty())
                vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
#else
            if (!t_buffer.vertices.vbos.empty())
                vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
#endif // ENABLE_GL_CORE_PROFILE
        }

        // if adding the indices for the current segment exceeds the threshold size of the current index buffer
        // create another index buffer
        size_t indiced_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.indices_size_bytes() : t_buffer.max_indices_per_segment_size_bytes();
        if (i_multibuffer.back().size() * sizeof(IBufferType) >= IBUFFER_THRESHOLD_BYTES - indiced_size_to_add) {
            i_multibuffer.push_back(IndexBuffer());
#if ENABLE_GL_CORE_PROFILE
            if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                vao_index_list.push_back(t_buffer.vertices.vaos[curr_vertex_buffer.first]);
#endif // ENABLE_GL_CORE_PROFILE
            vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
            if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
                Path& last_path = t_buffer.paths.back();
                last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
        // create another index buffer
        size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : t_buffer.max_vertices_per_segment_size_bytes();
        if (curr_vertex_buffer.second * t_buffer.vertices.vertex_size_bytes() > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
            i_multibuffer.push_back(IndexBuffer());

            ++curr_vertex_buffer.first;
            curr_vertex_buffer.second = 0;
#if ENABLE_GL_CORE_PROFILE
            if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                vao_index_list.push_back(t_buffer.vertices.vaos[curr_vertex_buffer.first]);
#endif // ENABLE_GL_CORE_PROFILE
            vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);

            if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
                Path& last_path = t_buffer.paths.back();
                last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        IndexBuffer& i_buffer = i_multibuffer.back();

        switch (t_buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line: {
            add_indices_as_line(prev, curr, t_buffer, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id, account_for_volumetric_rate);
            curr_vertex_buffer.second += t_buffer.max_vertices_per_segment();
            break;
        }
        case TBuffer::ERenderPrimitiveType::Triangle: {
            add_indices_as_solid(prev, curr, next, t_buffer, curr_vertex_buffer.second, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id, account_for_volumetric_rate);
            break;
        }
        case TBuffer::ERenderPrimitiveType::BatchedModel: {
            add_indices_as_model_batch(t_buffer.model.data, i_buffer, curr_vertex_buffer.second);
            curr_vertex_buffer.second += t_buffer.model.data.vertices_count();
            break;
        }
        default: { break; }
        }
    }

    for (MultiIndexBuffer& i_multibuffer : indices) {
        for (IndexBuffer& i_buffer : i_multibuffer) {
            i_buffer.shrink_to_fit();
        }
    }

    // toolpaths data -> send indices data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel) {
            const MultiIndexBuffer& i_multibuffer = indices[i];
            for (const IndexBuffer& i_buffer : i_multibuffer) {
                const size_t size_elements = i_buffer.size();
                const size_t size_bytes = size_elements * sizeof(IBufferType);

                // stores index buffer informations into TBuffer
                t_buffer.indices.push_back(IBuffer());
                IBuffer& ibuf = t_buffer.indices.back();
                ibuf.count = size_elements;
#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                    ibuf.vao = vao_indices[i][t_buffer.indices.size() - 1];
#endif // ENABLE_GL_CORE_PROFILE
                ibuf.vbo = vbo_indices[i][t_buffer.indices.size() - 1];

#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.total_indices_gpu_size += static_cast<int64_t>(size_bytes);
                m_statistics.max_ibuffer_gpu_size = std::max(m_statistics.max_ibuffer_gpu_size, static_cast<int64_t>(size_bytes));
                ++m_statistics.ibuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

                glsafe(::glGenBuffers(1, &ibuf.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf.ibo));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, i_buffer.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }
        }
    }

    if (progress_dialog != nullptr) {
        progress_dialog->Update(100, "");
        progress_dialog->Fit();
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
    }

    auto update_segments_count = [&](EMoveType type, int64_t& count) {
        unsigned int id = buffer_id(type);
        const MultiIndexBuffer& buffers = indices[id];
        int64_t indices_count = 0;
        for (const IndexBuffer& buffer : buffers) {
            indices_count += buffer.size();
        }
        const TBuffer& t_buffer = m_buffers[id];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle)
            indices_count -= static_cast<int64_t>(12 * t_buffer.paths.size()); // remove the starting + ending caps = 4 triangles

        count += indices_count / t_buffer.indices_per_segment();
    };

    update_segments_count(EMoveType::Travel, m_statistics.travel_segments_count);
    update_segments_count(EMoveType::Wipe, m_statistics.wipe_segments_count);
    update_segments_count(EMoveType::Extrude, m_statistics.extrude_segments_count);

    m_statistics.load_indices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - smooth_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    log_memory_usage("Loaded G-code generated indices buffers ", vertices, indices);

    // dismiss indices data, no more needed
    std::vector<MultiIndexBuffer>().swap(indices);

    // layers zs / roles / extruder ids -> extract from result
    size_t last_travel_s_id = 0;
    size_t first_travel_s_id = 0;
    seams_count = 0;
    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
        if (move.type == EMoveType::Seam)
            ++seams_count;

        const size_t move_id = i - seams_count;

        if (move.type == EMoveType::Extrude) {
            // layers zs/ranges
            const double* const last_z = m_layers.empty() ? nullptr : &m_layers.get_zs().back();
            const double z = static_cast<double>(move.position.z());
            if (move.extrusion_role != GCodeExtrusionRole::Custom &&
                (last_z == nullptr || z < *last_z - EPSILON || *last_z + EPSILON < z)) {
                // start a new layer
                const size_t start_it = (m_layers.empty() && first_travel_s_id != 0) ? first_travel_s_id : last_travel_s_id;
                m_layers.append(z, { start_it, move_id });
            }
            else if (!m_layers.empty() && !move.internal_only)
                // update last layer
                m_layers.get_ranges().back().last = move_id;

            // extruder ids
            m_extruder_ids.emplace_back(move.extruder_id);
            // roles
            if (i > 0)
                m_roles.emplace_back(move.extrusion_role);
        }
        else if (move.type == EMoveType::Travel) {
            if (move_id - last_travel_s_id > 1 && !m_layers.empty())
                m_layers.get_ranges().back().last = move_id;
            else if (m_layers.empty() && first_travel_s_id == 0)
                first_travel_s_id = move_id;
            last_travel_s_id = move_id;
        }
    }

    // roles -> remove duplicates
    sort_remove_duplicates(m_roles);
    m_roles.shrink_to_fit();

    // extruder ids -> remove duplicates
    sort_remove_duplicates(m_extruder_ids);
    m_extruder_ids.shrink_to_fit();

    // replace layers for spiral vase mode
    if (!gcode_result.spiral_vase_layers.empty()) {
        m_layers.reset();
        for (const auto& layer : gcode_result.spiral_vase_layers) {
            m_layers.append(layer.first, { layer.second.first, layer.second.second });
        }
    }

    // set layers z range
    if (!m_layers.empty())
        m_layers_z_range = { 0, static_cast<unsigned int>(m_layers.size() - 1) };

    // change color of paths whose layer contains option points
    if (!options_zs.empty()) {
        TBuffer& extrude_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
        for (Path& path : extrude_buffer.paths) {
            const float z = path.sub_paths.front().first.position.z();
            if (std::find_if(options_zs.begin(), options_zs.end(), [z](float f) { return f - EPSILON <= z && z <= f + EPSILON; }) != options_zs.end())
                path.cp_color_id = 255 - path.cp_color_id;
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.load_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (progress_dialog != nullptr)
        progress_dialog->Destroy();
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
        const size_t extruders_count = config.nozzle_diameter.size();
        if (extruders_count > 1 && config.wipe_tower && !config.complete_objects) {
            const WipeTowerData& wipe_tower_data = print.wipe_tower_data(extruders_count);
            const float depth = wipe_tower_data.depth;
            const std::vector<std::pair<float, float>> z_and_depth_pairs = print.wipe_tower_data(extruders_count).z_and_depth_pairs;
            const float brim_width = wipe_tower_data.brim_width;
            if (depth != 0.) {
                m_shells.volumes.load_wipe_tower_preview(config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, depth, z_and_depth_pairs,
                    max_z, config.wipe_tower_cone_angle, config.wipe_tower_rotation_angle, false, brim_width);
                GLVolume* volume = m_shells.volumes.volumes.back();
                volume->color.a(0.25f);
                volume->force_native_color = true;
                volume->set_render_color(true);
                m_shells_bounding_box.merge(volume->transformed_bounding_box());
                m_max_bounding_box.reset();
            }
        }
    }
}

void GCodeViewer::refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    auto extrusion_color = [this](const Path& path) {
        ColorRGBA color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:    { color = Extrusion_Role_Colors[static_cast<unsigned int>(path.role)]; break; }
        case EViewType::Height:         { color = m_extrusions.ranges.height.get_color_at(path.height); break; }
        case EViewType::Width:          { color = m_extrusions.ranges.width.get_color_at(path.width); break; }
        case EViewType::Feedrate:       { color = m_extrusions.ranges.feedrate.get_color_at(path.feedrate); break; }
        case EViewType::FanSpeed:       { color = m_extrusions.ranges.fan_speed.get_color_at(path.fan_speed); break; }
        case EViewType::Temperature:    { color = m_extrusions.ranges.temperature.get_color_at(path.temperature); break; }
        case EViewType::LayerTimeLinear:
        case EViewType::LayerTimeLogarithmic: {
        if (!m_layers_times.empty() && m_layers.size() == m_layers_times.front().size()) {
                const Path::Sub_Path& sub_path = path.sub_paths.front();
                double z = static_cast<double>(sub_path.first.position.z());
                const std::vector<double>& zs = m_layers.get_zs();
                const std::vector<Layers::Range>& ranges = m_layers.get_ranges();
                size_t time_mode_id = static_cast<size_t>(m_time_estimate_mode);
                for (size_t i = 0; i < zs.size(); ++i) {
                    if (std::abs(zs[i] - z) < EPSILON) {
                        if (ranges[i].contains(sub_path.first.s_id)) {
                            color = m_extrusions.ranges.layer_time[time_mode_id].get_color_at(m_layers_times[time_mode_id][i],
                                (m_view_type == EViewType::LayerTimeLinear) ? Extrusions::Range::EType::Linear : Extrusions::Range::EType::Logarithmic);
                            break;
                        }
                    }
                }
            }
            break;
        }
        case EViewType::VolumetricRate: { color = m_extrusions.ranges.volumetric_rate.get_color_at(path.volumetric_rate); break; }
        case EViewType::Tool:           { color = m_tool_colors[path.extruder_id]; break; }
        case EViewType::ColorPrint:     {
            if (path.cp_color_id >= static_cast<unsigned char>(m_tool_colors.size()))
                color = ColorRGBA::GRAY();
            else
                color = m_tool_colors[path.cp_color_id];

            break;
        }
        default: { color = ColorRGBA::WHITE(); break; }
        }

        return color;
    };

    auto travel_color = [](const Path& path) {
        return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
            ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                Travel_Colors[0] /* Move */);
    };

    auto is_in_layers_range = [this](const Path& path, size_t min_id, size_t max_id) {
        auto in_layers_range = [this, min_id, max_id](size_t id) {
            return m_layers.get_range_at(min_id).first <= id && id <= m_layers.get_range_at(max_id).last;
        };

        return in_layers_range(path.sub_paths.front().first.s_id) && in_layers_range(path.sub_paths.back().last.s_id);
    };

    auto is_travel_in_layers_range = [this](size_t path_id, size_t min_id, size_t max_id) {
        const TBuffer& buffer = m_buffers[buffer_id(EMoveType::Travel)];
        if (path_id >= buffer.paths.size())
            return false;

        Path path = buffer.paths[path_id];
        size_t first = path_id;
        size_t last = path_id;

        // check adjacent paths
        while (first > 0) {
            const Path& ref_path = buffer.paths[first - 1];
            if (!path.sub_paths.front().first.position.isApprox(ref_path.sub_paths.back().last.position) ||
                path.role != ref_path.role)
                break;

            path.sub_paths.front().first = ref_path.sub_paths.front().first;
            --first;
        }
        while (last < buffer.paths.size() - 1) {
            const Path& ref_path = buffer.paths[last + 1];
            if (!path.sub_paths.back().last.position.isApprox(ref_path.sub_paths.front().first.position) ||
                path.role != ref_path.role)
                break;

            path.sub_paths.back().last = ref_path.sub_paths.back().last;
            ++last;
        }

        const size_t min_s_id = m_layers.get_range_at(min_id).first;
        const size_t max_s_id = m_layers.get_range_at(max_id).last;

        const bool top_layer_shown = max_id == m_layers.size() - 1;

        return (min_s_id <= path.sub_paths.front().first.s_id && path.sub_paths.front().first.s_id <= max_s_id) || // the leading vertex is contained
               (min_s_id <= path.sub_paths.back().last.s_id && path.sub_paths.back().last.s_id <= max_s_id) ||     // the trailing vertex is contained
               (top_layer_shown && max_s_id < path.sub_paths.front().first.s_id); // the leading vertex is above the top layer and the top layer is shown
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    Statistics* statistics = const_cast<Statistics*>(&m_statistics);
    statistics->render_paths_size = 0;
    statistics->models_instances_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    const bool top_layer_only = get_app_config()->get_bool("seq_top_layer_only");

    SequentialView::Endpoints global_endpoints = { m_moves_count , 0 };
    SequentialView::Endpoints top_layer_endpoints = global_endpoints;
    SequentialView* sequential_view = const_cast<SequentialView*>(&m_sequential_view);
    if (top_layer_only || !keep_sequential_current_first) sequential_view->current.first = 0;
    if (!keep_sequential_current_last) sequential_view->current.last = m_moves_count;

    // first pass: collect visible paths and update sequential view data
    std::vector<std::tuple<unsigned char, unsigned int, unsigned int, unsigned int>> paths;
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
        // reset render paths
        buffer.render_paths.clear();

        if (!buffer.visible)
            continue;

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
            buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            for (size_t id : buffer.model.instances.s_ids) {
                if (id < m_layers.get_range_at(m_layers_z_range[0]).first || m_layers.get_range_at(m_layers_z_range[1]).last < id)
                    continue;

                global_endpoints.first = std::min(global_endpoints.first, id);
                global_endpoints.last = std::max(global_endpoints.last, id);

                if (top_layer_only) {
                    if (id < m_layers.get_range_at(m_layers_z_range[1]).first || m_layers.get_range_at(m_layers_z_range[1]).last < id)
                        continue;

                    top_layer_endpoints.first = std::min(top_layer_endpoints.first, id);
                    top_layer_endpoints.last = std::max(top_layer_endpoints.last, id);
                }
            }
        }
        else {
            for (size_t i = 0; i < buffer.paths.size(); ++i) {
                const Path& path = buffer.paths[i];
                if (path.type == EMoveType::Travel) {
                    if (!is_travel_in_layers_range(i, m_layers_z_range[0], m_layers_z_range[1]))
                          continue;
                }
                else if (!is_in_layers_range(path, m_layers_z_range[0], m_layers_z_range[1]))
                    continue;

                if (path.type == EMoveType::Extrude && !is_visible(path))
                    continue;

                // store valid path
                for (size_t j = 0; j < path.sub_paths.size(); ++j) {
                    paths.push_back({ static_cast<unsigned char>(b), path.sub_paths[j].first.b_id, static_cast<unsigned int>(i), static_cast<unsigned int>(j) });
                }

                global_endpoints.first = std::min(global_endpoints.first, path.sub_paths.front().first.s_id);
                global_endpoints.last = std::max(global_endpoints.last, path.sub_paths.back().last.s_id);

                if (top_layer_only) {
                    if (path.type == EMoveType::Travel) {
                        if (is_travel_in_layers_range(i, m_layers_z_range[1], m_layers_z_range[1])) {
                            top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.sub_paths.front().first.s_id);
                            top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.sub_paths.back().last.s_id);
                        }
                    }
                    else if (is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1])) {
                        top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.sub_paths.front().first.s_id);
                        top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.sub_paths.back().last.s_id);
                    }
                }
            }
        }
    }

    // update current sequential position
    sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(sequential_view->current.first, global_endpoints.first, global_endpoints.last) : global_endpoints.first;
    sequential_view->current.last = keep_sequential_current_last ? std::clamp(sequential_view->current.last, global_endpoints.first, global_endpoints.last) : global_endpoints.last;

    // get the world position from the vertex buffer
    bool found = false;
    for (const TBuffer& buffer : m_buffers) {
        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
            buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            for (size_t i = 0; i < buffer.model.instances.s_ids.size(); ++i) {
                if (buffer.model.instances.s_ids[i] == m_sequential_view.current.last) {
                    size_t offset = i * buffer.model.instances.instance_size_floats();
                    sequential_view->current_position.x() = buffer.model.instances.buffer[offset + 0];
                    sequential_view->current_position.y() = buffer.model.instances.buffer[offset + 1];
                    sequential_view->current_position.z() = buffer.model.instances.buffer[offset + 2];
                    sequential_view->current_offset = buffer.model.instances.offsets[i];
                    found = true;
                    break;
                }
            }
        }
        else {
            // searches the path containing the current position
            for (const Path& path : buffer.paths) {
                if (path.contains(m_sequential_view.current.last)) {
                    const int sub_path_id = path.get_id_of_sub_path_containing(m_sequential_view.current.last);
                    if (sub_path_id != -1) {
                        const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
                        unsigned int offset = static_cast<unsigned int>(m_sequential_view.current.last - sub_path.first.s_id);
                        if (offset > 0) {
                            if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Line)
                                offset = 2 * offset - 1;
                            else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                                unsigned int indices_count = buffer.indices_per_segment();
                                offset = indices_count * (offset - 1) + (indices_count - 2);
                                if (sub_path_id == 0)
                                    offset += 6; // add 2 triangles for starting cap 
                            }
                        }
                        offset += static_cast<unsigned int>(sub_path.first.i_id);

                        // gets the vertex index from the index buffer on gpu
                        const IBuffer& i_buffer = buffer.indices[sub_path.first.b_id];
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
#if ENABLE_OPENGL_ES
                        IBufferType index = *static_cast<IBufferType*>(::glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
                            static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)),
                            GL_MAP_READ_BIT));
                        glcheck();
                        glsafe(::glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
#else
                        IBufferType index = 0;
                        glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&index)));
#endif // ENABLE_OPENGL_ES
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                        // gets the position from the vertices buffer on gpu
                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
#if ENABLE_OPENGL_ES
                        sequential_view->current_position = *static_cast<Vec3f*>(::glMapBufferRange(GL_ARRAY_BUFFER,
                            static_cast<GLintptr>(index * buffer.vertices.vertex_size_bytes()),
                            static_cast<GLsizeiptr>(buffer.vertices.position_size_bytes()), GL_MAP_READ_BIT));
                        glcheck();
                        glsafe(::glUnmapBuffer(GL_ARRAY_BUFFER));
#else
                        glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(index * buffer.vertices.vertex_size_bytes()), static_cast<GLsizeiptr>(3 * sizeof(float)), static_cast<void*>(sequential_view->current_position.data())));
#endif // ENABLE_OPENGL_ES
                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

                        sequential_view->current_offset = Vec3f::Zero();
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found)
            break;
    }

    // second pass: filter paths by sequential data and collect them by color
    RenderPath* render_path = nullptr;
    for (const auto& [tbuffer_id, ibuffer_id, path_id, sub_path_id] : paths) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[tbuffer_id]);
        const Path& path = buffer.paths[path_id];
        const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
        if (m_sequential_view.current.last < sub_path.first.s_id || sub_path.last.s_id < m_sequential_view.current.first)
            continue;

        ColorRGBA color;
        switch (path.type)
        {
        case EMoveType::Tool_change:
        case EMoveType::Color_change:
        case EMoveType::Pause_Print:
        case EMoveType::Custom_GCode:
        case EMoveType::Retract:
        case EMoveType::Unretract:
        case EMoveType::Seam: { color = option_color(path.type); break; }
        case EMoveType::Extrude: {
            if (!top_layer_only ||
                m_sequential_view.current.last == global_endpoints.last ||
                is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1]))
                color = extrusion_color(path);
            else
                color = Neutral_Color;

            break;
        }
        case EMoveType::Travel: {
            if (!top_layer_only || m_sequential_view.current.last == global_endpoints.last || is_travel_in_layers_range(path_id, m_layers_z_range[1], m_layers_z_range[1]))
                color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::ColorPrint) ? extrusion_color(path) : travel_color(path);
            else
                color = Neutral_Color;

            break;
        }
        case EMoveType::Wipe: { color = Wipe_Color; break; }
        default: { color = { 0.0f, 0.0f, 0.0f, 1.0f }; break; }
        }

        RenderPath key{ tbuffer_id, color, static_cast<unsigned int>(ibuffer_id), path_id };
        if (render_path == nullptr || !RenderPathPropertyEqual()(*render_path, key)) {
            buffer.render_paths.emplace_back(key);
            render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
        }

        unsigned int delta_1st = 0;
        if (sub_path.first.s_id < m_sequential_view.current.first && m_sequential_view.current.first <= sub_path.last.s_id)
            delta_1st = static_cast<unsigned int>(m_sequential_view.current.first - sub_path.first.s_id);

        unsigned int size_in_indices = 0;
        switch (buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line:
        case TBuffer::ERenderPrimitiveType::Triangle: {
            unsigned int segments_count = std::min(m_sequential_view.current.last, sub_path.last.s_id) - std::max(m_sequential_view.current.first, sub_path.first.s_id);
            size_in_indices = buffer.indices_per_segment() * segments_count;
            break;
        }
        default: { break; }
        }

        if (size_in_indices == 0)
            continue;

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            if (sub_path_id == 0 && delta_1st == 0)
                size_in_indices += 6; // add 2 triangles for starting cap 
            if (sub_path_id == path.sub_paths.size() - 1 && path.sub_paths.back().last.s_id <= m_sequential_view.current.last)
                size_in_indices += 6; // add 2 triangles for ending cap 
            if (delta_1st > 0)
                size_in_indices -= 6; // remove 2 triangles for corner cap  
        }

        render_path->sizes.push_back(size_in_indices);

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            delta_1st *= buffer.indices_per_segment();
            if (delta_1st > 0) {
                delta_1st += 6; // skip 2 triangles for corner cap 
                if (sub_path_id == 0)
                    delta_1st += 6; // skip 2 triangles for starting cap 
            }
        }

        render_path->offsets.push_back(static_cast<size_t>((sub_path.first.i_id + delta_1st) * sizeof(IBufferType)));

#if 0
        // check sizes and offsets against index buffer size on gpu
        GLint buffer_size;
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->indices[render_path->ibuffer_id].ibo));
        glsafe(::glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        if (render_path->offsets.back() + render_path->sizes.back() * sizeof(IBufferType) > buffer_size)
            BOOST_LOG_TRIVIAL(error) << "GCodeViewer::refresh_render_paths: Invalid render path data";
#endif 
    }

    // Removes empty render paths and sort.
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer* buffer = const_cast<TBuffer*>(&m_buffers[b]);
        buffer->render_paths.erase(std::remove_if(buffer->render_paths.begin(), buffer->render_paths.end(), 
            [](const auto &path){ return path.sizes.empty() || path.offsets.empty(); }),
            buffer->render_paths.end());
    }

    // second pass: for buffers using instanced and batched models, update the instances render ranges
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
        if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel &&
            buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel)
            continue;

        buffer.model.instances.render_ranges.reset();

        if (!buffer.visible || buffer.model.instances.s_ids.empty())
            continue;

        buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, buffer.model.color });
        bool has_second_range = top_layer_only && m_sequential_view.current.last != m_sequential_view.global.last;
        if (has_second_range)
            buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, Neutral_Color });

        if (m_sequential_view.current.first <= buffer.model.instances.s_ids.back() && buffer.model.instances.s_ids.front() <= m_sequential_view.current.last) {
            for (size_t id : buffer.model.instances.s_ids) {
                if (has_second_range) {
                    if (id < m_sequential_view.endpoints.first) {
                        ++buffer.model.instances.render_ranges.ranges.front().offset;
                        if (id <= m_sequential_view.current.first)
                            ++buffer.model.instances.render_ranges.ranges.back().offset;
                        else
                            ++buffer.model.instances.render_ranges.ranges.back().count;
                    }
                    else if (id <= m_sequential_view.current.last)
                        ++buffer.model.instances.render_ranges.ranges.front().count;
                    else
                        break;
                }
                else {
                    if (id <= m_sequential_view.current.first)
                        ++buffer.model.instances.render_ranges.ranges.front().offset;
                    else if (id <= m_sequential_view.current.last)
                        ++buffer.model.instances.render_ranges.ranges.front().count;
                    else
                        break;
                }
            }
        }
    }

    // set sequential data to their final value
    sequential_view->endpoints = top_layer_only ? top_layer_endpoints : global_endpoints;
    sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(sequential_view->current.first, sequential_view->endpoints.first, sequential_view->endpoints.last) : sequential_view->endpoints.first;
    sequential_view->global = global_endpoints;

    // updates sequential range caps
    std::array<SequentialRangeCap, 2>* sequential_range_caps = const_cast<std::array<SequentialRangeCap, 2>*>(&m_sequential_range_caps);
    (*sequential_range_caps)[0].reset();
    (*sequential_range_caps)[1].reset();

    if (m_sequential_view.current.first != m_sequential_view.current.last) {
        for (const auto& [tbuffer_id, ibuffer_id, path_id, sub_path_id] : paths) {
            TBuffer& buffer = const_cast<TBuffer&>(m_buffers[tbuffer_id]);
            if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Triangle)
                continue;

            const Path& path = buffer.paths[path_id];
            const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
            if (m_sequential_view.current.last <= sub_path.first.s_id || sub_path.last.s_id <= m_sequential_view.current.first)
                continue;

            // update cap for first endpoint of current range
            if (m_sequential_view.current.first > sub_path.first.s_id) {
                SequentialRangeCap& cap = (*sequential_range_caps)[0];
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                cap.buffer = &buffer;
#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                    cap.vao = i_buffer.vao;
#endif // ENABLE_GL_CORE_PROFILE
                cap.vbo = i_buffer.vbo;

                // calculate offset into the index buffer
                unsigned int offset = sub_path.first.i_id;
                offset += 6; // add 2 triangles for corner cap
                offset += static_cast<unsigned int>(m_sequential_view.current.first - sub_path.first.s_id) * buffer.indices_per_segment();
                if (sub_path_id == 0)
                    offset += 6; // add 2 triangles for starting cap

                // extract indices from index buffer
                std::array<IBufferType, 6> indices{ 0, 0, 0, 0, 0, 0 };
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
#if ENABLE_OPENGL_ES
                IBufferType* index_ptr = static_cast<IBufferType*>(::glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(14 * sizeof(IBufferType)),
                    GL_MAP_READ_BIT));
                glcheck();
                indices[0] = *(index_ptr + 0);
                indices[1] = *(index_ptr + 7);
                indices[2] = *(index_ptr + 1);
                indices[4] = *(index_ptr + 13);
                glsafe(::glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
#else
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 0) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[0])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 7) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[1])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 1) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[2])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 13) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[4])));
#endif // ENABLE_OPENGL_ES
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                indices[3] = indices[0];
                indices[5] = indices[1];

                // send indices to gpu
                glsafe(::glGenBuffers(1, &cap.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(IBufferType), indices.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                // extract color from render path
                size_t offset_bytes = offset * sizeof(IBufferType);
                for (const RenderPath& render_path : buffer.render_paths) {
                    if (render_path.ibuffer_id == ibuffer_id) {
                        for (size_t j = 0; j < render_path.offsets.size(); ++j) {
                            if (render_path.contains(offset_bytes)) {
                                cap.color = render_path.color;
                                break;
                            }
                        }
                    }
                }
            }

            // update cap for last endpoint of current range
            if (m_sequential_view.current.last < sub_path.last.s_id) {
                SequentialRangeCap& cap = (*sequential_range_caps)[1];
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                cap.buffer = &buffer;
#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                    cap.vao = i_buffer.vao;
#endif // ENABLE_GL_CORE_PROFILE
                cap.vbo = i_buffer.vbo;

                // calculate offset into the index buffer
                unsigned int offset = sub_path.first.i_id;
                offset += 6; // add 2 triangles for corner cap
                offset += static_cast<unsigned int>(m_sequential_view.current.last - 1 - sub_path.first.s_id) * buffer.indices_per_segment();
                if (sub_path_id == 0)
                    offset += 6; // add 2 triangles for starting cap

                // extract indices from index buffer
                std::array<IBufferType, 6> indices{ 0, 0, 0, 0, 0, 0 };
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
#if ENABLE_OPENGL_ES
                IBufferType* index_ptr = static_cast<IBufferType*>(::glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(17 * sizeof(IBufferType)),
                    GL_MAP_READ_BIT));
                glcheck();
                indices[0] = *(index_ptr + 2);
                indices[1] = *(index_ptr + 4);
                indices[2] = *(index_ptr + 10);
                indices[5] = *(index_ptr + 16);
                glsafe(::glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
#else
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 2) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[0])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 4) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[1])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 10) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[2])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 16) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[5])));
#endif // ENABLE_OPENGL_ES
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                indices[3] = indices[0];
                indices[4] = indices[2];

                // send indices to gpu
                glsafe(::glGenBuffers(1, &cap.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(IBufferType), indices.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                // extract color from render path
                size_t offset_bytes = offset * sizeof(IBufferType);
                for (const RenderPath& render_path : buffer.render_paths) {
                    if (render_path.ibuffer_id == ibuffer_id) {
                        for (size_t j = 0; j < render_path.offsets.size(); ++j) {
                            if (render_path.contains(offset_bytes)) {
                                cap.color = render_path.color;
                                break;
                            }
                        }
                    }
                }
            }

            if ((*sequential_range_caps)[0].is_renderable() && (*sequential_range_caps)[1].is_renderable())
                break;
        }
    }

    wxGetApp().plater()->enable_preview_moves_slider(!paths.empty());

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        statistics->render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
        for (const RenderPath& path : buffer.render_paths) {
            statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
            statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
        }
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.buffer, float);
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.s_ids, size_t);
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.render_ranges.ranges, InstanceVBuffer::Ranges::Range);
    }
    statistics->refresh_paths_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::render_toolpaths()
{
    const Camera& camera = wxGetApp().plater()->get_camera();
#if !ENABLE_GL_CORE_PROFILE
    const double zoom = camera.get_zoom();
#endif // !ENABLE_GL_CORE_PROFILE

    auto render_as_lines = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color) {
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(! path.sizes.empty());
            assert(! path.offsets.empty());
            shader.set_uniform(uniform_color, path.color);
#if ENABLE_GL_CORE_PROFILE
            const Camera& camera = wxGetApp().plater()->get_camera();
            const std::array<int, 4>& viewport = camera.get_viewport();
            const float zoom = float(camera.get_zoom());
            shader.set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader.set_uniform("width", (zoom < 5.0f) ? 0.5f : (0.5f + 5.0f * (zoom - 5.0f) / (100.0f - 5.0f)));
            shader.set_uniform("gap_size", 0.0f);
#endif // ENABLE_GL_CORE_PROFILE

#if ENABLE_OPENGL_ES
            for (size_t i = 0; i < path.sizes.size(); ++i) {
                glsafe(::glDrawElements(GL_LINES, (GLsizei)path.sizes[i], GL_UNSIGNED_SHORT, (const void*)path.offsets[i]));
            }
#else
            glsafe(::glMultiDrawElements(GL_LINES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#endif // ENABLE_OPENGL_ES
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_lines_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto render_as_triangles = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color) {
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(! path.sizes.empty());
            assert(! path.offsets.empty());
            shader.set_uniform(uniform_color, path.color);
#if ENABLE_OPENGL_ES
            for (size_t i = 0; i < path.sizes.size(); ++i) {
                glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)path.sizes[i], GL_UNSIGNED_SHORT, (const void*)path.offsets[i]));
            }
#else
            glsafe(::glMultiDrawElements(GL_TRIANGLES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#endif // ENABLE_OPENGL_ES
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto render_as_instanced_model = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        ](TBuffer& buffer, GLShaderProgram & shader) {
        for (auto& range : buffer.model.instances.render_ranges.ranges) {
            if (range.vbo == 0 && range.count > 0) {
                glsafe(::glGenBuffers(1, &range.vbo));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, range.vbo));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, range.count * buffer.model.instances.instance_size_bytes(), (const void*)&buffer.model.instances.buffer[range.offset * buffer.model.instances.instance_size_floats()], GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }

            if (range.vbo > 0) {
                buffer.model.model.set_color(range.color);
                buffer.model.model.render_instanced(range.vbo, range.count);
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_instanced_models_calls_count;
                m_statistics.total_instances_gpu_size += static_cast<int64_t>(range.count * buffer.model.instances.instance_size_bytes());
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
        auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#else
        auto render_as_batched_model = [](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        struct Range
        {
            unsigned int first;
            unsigned int last;
            bool intersects(const Range& other) const { return (other.last < first || other.first > last) ? false : true; }
        };
        Range buffer_range = { 0, 0 };
        const size_t indices_per_instance = buffer.model.data.indices_count();

        for (size_t j = 0; j < buffer.indices.size(); ++j) {
            const IBuffer& i_buffer = buffer.indices[j];
            buffer_range.last = buffer_range.first + i_buffer.count / indices_per_instance;
#if ENABLE_GL_CORE_PROFILE
            if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                glsafe(::glBindVertexArray(i_buffer.vao));
#endif // ENABLE_GL_CORE_PROFILE
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
            if (position_id != -1) {
                glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                glsafe(::glEnableVertexAttribArray(position_id));
            }
            const bool has_normals = buffer.vertices.normal_size_floats() > 0;
            if (has_normals) {
                if (normal_id != -1) {
                    glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                    glsafe(::glEnableVertexAttribArray(normal_id));
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

            for (auto& range : buffer.model.instances.render_ranges.ranges) {
                const Range range_range = { range.offset, range.offset + range.count };
                if (range_range.intersects(buffer_range)) {
                    shader.set_uniform("uniform_color", range.color);
                    const unsigned int offset = (range_range.first > buffer_range.first) ? range_range.first - buffer_range.first : 0;
                    const size_t offset_bytes = static_cast<size_t>(offset) * indices_per_instance * sizeof(IBufferType);
                    const Range render_range = { std::max(range_range.first, buffer_range.first), std::min(range_range.last, buffer_range.last) };
                    const size_t count = static_cast<size_t>(render_range.last - render_range.first) * indices_per_instance;
                    if (count > 0) {
                        glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, (const void*)offset_bytes));
#if ENABLE_GCODE_VIEWER_STATISTICS
                        ++m_statistics.gl_batched_models_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                    }
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

            if (normal_id != -1)
                glsafe(::glDisableVertexAttribArray(normal_id));
            if (position_id != -1)
                glsafe(::glDisableVertexAttribArray(position_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
#if ENABLE_GL_CORE_PROFILE
            if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                glsafe(::glBindVertexArray(0));
#endif // ENABLE_GL_CORE_PROFILE

            buffer_range.first = buffer_range.last;
        }
    };

    auto line_width = [](double zoom) {
        return (zoom < 5.0) ? 1.0 : (1.0 + 5.0 * (zoom - 5.0) / (100.0 - 5.0));
    };

    const unsigned char begin_id = buffer_id(EMoveType::Retract);
    const unsigned char end_id   = buffer_id(EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i) {
        TBuffer& buffer = m_buffers[i];
        if (!buffer.visible || !buffer.has_data())
            continue;

        GLShaderProgram* shader = wxGetApp().get_shader(buffer.shader.c_str());
        if (shader == nullptr)
            continue;

        shader->start_using();

        shader->set_uniform("view_model_matrix", camera.get_view_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("view_normal_matrix", (Matrix3d)Matrix3d::Identity());

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
            shader->set_uniform("emission_factor", 0.25f);
            render_as_instanced_model(buffer, *shader);
            shader->set_uniform("emission_factor", 0.0f);
        }
        else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            shader->set_uniform("emission_factor", 0.25f);
            const int position_id = shader->get_attrib_location("v_position");
            const int normal_id   = shader->get_attrib_location("v_normal");
            render_as_batched_model(buffer, *shader, position_id, normal_id);
            shader->set_uniform("emission_factor", 0.0f);
        }
        else {
            shader->set_uniform("emission_factor", 0.15f);
            const int position_id = shader->get_attrib_location("v_position");
            const int normal_id   = shader->get_attrib_location("v_normal");
            const int uniform_color = shader->get_uniform_location("uniform_color");

            auto it_path = buffer.render_paths.begin();
            for (unsigned int ibuffer_id = 0; ibuffer_id < static_cast<unsigned int>(buffer.indices.size()); ++ibuffer_id) {
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                // Skip all paths with ibuffer_id < ibuffer_id.
                for (; it_path != buffer.render_paths.end() && it_path->ibuffer_id < ibuffer_id; ++it_path);
                if (it_path == buffer.render_paths.end() || it_path->ibuffer_id > ibuffer_id)
                    // Not found. This shall not happen.
                    continue;

#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                    glsafe(::glBindVertexArray(i_buffer.vao));
#endif // ENABLE_GL_CORE_PROFILE
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                if (position_id != -1) {
                    glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                    glsafe(::glEnableVertexAttribArray(position_id));
                }
                const bool has_normals = buffer.vertices.normal_size_floats() > 0;
                if (has_normals) {
                    if (normal_id != -1) {
                        glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                        glsafe(::glEnableVertexAttribArray(normal_id));
                    }
                }

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

                // Render all elements with it_path->ibuffer_id == ibuffer_id, possible with varying colors.
                switch (buffer.render_primitive_type)
                {
                case TBuffer::ERenderPrimitiveType::Line: {
#if ENABLE_GL_CORE_PROFILE
                    if (!OpenGLManager::get_gl_info().is_core_profile())
                        glsafe(::glLineWidth(static_cast<GLfloat>(line_width(camera.get_zoom()))));
#else
                    glsafe(::glLineWidth(static_cast<GLfloat>(line_width(zoom))));
#endif // ENABLE_GL_CORE_PROFILE
                    render_as_lines(it_path, buffer.render_paths.end(), *shader, uniform_color);
                    break;
                }
                case TBuffer::ERenderPrimitiveType::Triangle: {
                    render_as_triangles(it_path, buffer.render_paths.end(), *shader, uniform_color);
                    break;
                }
                default: { break; }
                }

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                if (normal_id != -1)
                    glsafe(::glDisableVertexAttribArray(normal_id));
                if (position_id != -1)
                    glsafe(::glDisableVertexAttribArray(position_id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
#if ENABLE_GL_CORE_PROFILE
                if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
                    glsafe(::glBindVertexArray(0));
#endif // ENABLE_GL_CORE_PROFILE
            }
        }

        shader->stop_using();
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto render_sequential_range_cap = [this, &camera]
#else
    auto render_sequential_range_cap = [&camera]
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    (const SequentialRangeCap& cap) {
        const TBuffer* buffer = cap.buffer;
        GLShaderProgram* shader = wxGetApp().get_shader(buffer->shader.c_str());
        if (shader == nullptr)
            return;

        shader->start_using();

        shader->set_uniform("view_model_matrix", camera.get_view_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("view_normal_matrix", (Matrix3d)Matrix3d::Identity());

        const int position_id = shader->get_attrib_location("v_position");
        const int normal_id   = shader->get_attrib_location("v_normal");

#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
            glsafe(::glBindVertexArray(cap.vao));
#endif // ENABLE_GL_CORE_PROFILE
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, cap.vbo));
        if (position_id != -1) {
            glsafe(::glVertexAttribPointer(position_id, buffer->vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer->vertices.vertex_size_bytes(), (const void*)buffer->vertices.position_offset_bytes()));
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        const bool has_normals = buffer->vertices.normal_size_floats() > 0;
        if (has_normals) {
            if (normal_id != -1) {
                glsafe(::glVertexAttribPointer(normal_id, buffer->vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer->vertices.vertex_size_bytes(), (const void*)buffer->vertices.normal_offset_bytes()));
                glsafe(::glEnableVertexAttribArray(normal_id));
            }
        }

        shader->set_uniform("uniform_color", cap.color);

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
        glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)cap.indices_count(), GL_UNSIGNED_SHORT, nullptr));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

#if ENABLE_GCODE_VIEWER_STATISTICS
        ++m_statistics.gl_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        if (normal_id != -1)
            glsafe(::glDisableVertexAttribArray(normal_id));
        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 0))
            glsafe(::glBindVertexArray(0));
#endif // ENABLE_GL_CORE_PROFILE

        shader->stop_using();
    };

    for (unsigned int i = 0; i < 2; ++i) {
        if (m_sequential_range_caps[i].is_renderable())
            render_sequential_range_cap(m_sequential_range_caps[i]);
    }
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
    m_shells.volumes.render(GLVolumeCollection::ERenderType::Transparent, true, camera.get_view_matrix(), camera.get_projection_matrix());
    shader->set_uniform("emission_factor", 0.0f);
    shader->stop_using();
}

void GCodeViewer::render_legend(float& legend_height)
{
    if (!m_legend_enabled)
        return;

    const Size cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0.0f, 0.0f, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.6f);
    const float max_height = 0.75f * static_cast<float>(cnv_size.get_height());
    const float child_height = 0.3333f * max_height;
    ImGui::SetNextWindowSizeConstraints({ 0.0f, 0.0f }, { -1.0f, max_height });
    imgui.begin(std::string("Legend"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    enum class EItemType : unsigned char
    {
        Rect,
        Circle,
        Hexagon,
        Line
    };

    const PrintEstimatedStatistics::Mode& time_mode = m_print_statistics.modes[static_cast<size_t>(m_time_estimate_mode)];
    bool show_estimated_time = time_mode.time > 0.0f && (m_view_type == EViewType::FeatureType ||
        m_view_type == EViewType::LayerTimeLinear || m_view_type == EViewType::LayerTimeLogarithmic ||
        (m_view_type == EViewType::ColorPrint && !time_mode.custom_gcode_times.empty()));

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
                ImGuiWrapper::to_ImU32(color));
            break;
        }
        case EItemType::Circle: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 16);
            break;
        }
        case EItemType::Hexagon: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size));
            draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 6);
            break;
        }
        case EItemType::Line: {
            draw_list->AddLine({ pos.x + 1, pos.y + icon_size - 1 }, { pos.x + icon_size - 1, pos.y + 1 }, ImGuiWrapper::to_ImU32(color), 3.0f);
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
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
                    ImGui::BeginTooltip();
                    imgui.text(visible ? _u8L("Click to hide") : _u8L("Click to show"));
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
                imgui.text(time);
                ImGui::SameLine(offsets[1]);
                pos = ImGui::GetCursorScreenPos();
                const float width = std::max(1.0f, percent_bar_size * percent / max_percent);
                draw_list->AddRectFilled({ pos.x, pos.y + 2.0f }, { pos.x + width, pos.y + icon_size - 2.0f },
                    ImGui::GetColorU32(ImGuiWrapper::COL_ORANGE_LIGHT));
                ImGui::Dummy({ percent_bar_size, icon_size });
                ImGui::SameLine();
                char buf[64];
                ::sprintf(buf, "%.1f%%", 100.0f * percent);
                ImGui::TextUnformatted((percent > 0.0f) ? buf : "");
                ImGui::SameLine(offsets[2]);
                imgui.text(format("%1$.2f %2%", used_filament_m, (imperial_units ? inches : metres)));
                ImGui::SameLine(offsets[3]);
                imgui.text(format("%1$.2f %2%", used_filament_g, grams));
            }
        }
        else {
            imgui.text(label);
            if (!time.empty()) {
                ImGui::SameLine(offsets[0]);
                imgui.text(time);
                ImGui::SameLine(offsets[1]);
                pos = ImGui::GetCursorScreenPos();
                const float width = std::max(1.0f, percent_bar_size * percent / max_percent);
                draw_list->AddRectFilled({ pos.x, pos.y + 2.0f }, { pos.x + width, pos.y + icon_size - 2.0f },
                    ImGui::GetColorU32(ImGuiWrapper::COL_ORANGE_LIGHT));
                ImGui::Dummy({ percent_bar_size, icon_size });
                ImGui::SameLine();
                char buf[64];
                ::sprintf(buf, "%.1f%%", 100.0f * percent);
                ImGui::TextUnformatted((percent > 0.0f) ? buf : "");
            }
            else if (used_filament_m > 0.0) {
                ImGui::SameLine(offsets[0]);
                imgui.text(format("%1$.2f %2%", used_filament_m, (imperial_units ? inches : metres)));
                ImGui::SameLine(offsets[1]);
                imgui.text(format("%1$.2f %2%", used_filament_g, grams));
            }
        }

        if (!visible)
            ImGui::PopStyleVar();
    };

    auto append_range = [append_item](const Extrusions::Range& range, unsigned int decimals) {
        auto append_range_item = [append_item](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
            append_item(EItemType::Rect, Range_Colors[i], buf);
        };

        if (range.count == 1)
            // single item use case
            append_range_item(0, range.min, decimals);
        else if (range.count == 2) {
            // two items use case
            append_range_item(static_cast<int>(Range_Colors.size()) - 1, range.max, decimals);
            append_range_item(0, range.min, decimals);
        }
        else {
            const float step_size = range.step_size();
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                append_range_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    auto append_time_range = [append_item](const Extrusions::Range& range, Extrusions::Range::EType type) {
        auto append_range_item = [append_item](int i, float value) {
            std::string str_value = get_time_dhms(value);
            if (str_value == "0s")
                str_value = "< 1s";
            append_item(EItemType::Rect, Range_Colors[i], str_value);
        };

        if (range.count == 1)
            // single item use case
            append_range_item(0, range.min);
        else if (range.count == 2) {
            // two items use case
            append_range_item(static_cast<int>(Range_Colors.size()) - 1, range.max);
            append_range_item(0, range.min);
        }
        else {
            float step_size = range.step_size(type);
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                float value = 0.0f;
                switch (type)
                {
                default:
                case Extrusions::Range::EType::Linear:      { value = range.min + static_cast<float>(i) * step_size; break; }
                case Extrusions::Range::EType::Logarithmic: { value = ::exp(::log(range.min) + static_cast<float>(i) * step_size);  break; }
                }
                append_range_item(i, value);
            }
        }
    };

    auto append_headers = [&imgui](const std::array<std::string, 5>& texts, const std::array<float, 4>& offsets) {
        size_t i = 0;
        for (; i < offsets.size(); i++) {
            imgui.text(texts[i]);
            ImGui::SameLine(offsets[i]);
        }
        imgui.text(texts[i]);
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

            if (item.type != ColorChange)
                continue;

            const std::vector<double> zs = m_layers.get_zs();
            auto lower_b = std::lower_bound(zs.begin(), zs.end(), item.print_z - Slic3r::DoubleSlider::epsilon());
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

    auto role_time_and_percent = [time_mode](GCodeExtrusionRole role) {
        auto it = std::find_if(time_mode.roles_times.begin(), time_mode.roles_times.end(), [role](const std::pair<GCodeExtrusionRole, float>& item) { return role == item.first; });
        return (it != time_mode.roles_times.end()) ? std::make_pair(it->second, it->second / time_mode.time) : std::make_pair(0.0f, 0.0f);
    };

    auto used_filament_per_role = [this, imperial_units](GCodeExtrusionRole role) {
        auto it = m_print_statistics.used_filaments_per_role.find(role);
        if (it == m_print_statistics.used_filaments_per_role.end())
            return std::make_pair(0.0, 0.0);

        double koef = imperial_units ? 1000.0 / ObjectManipulation::in_to_mm : 1.0;
        return std::make_pair(it->second.first * koef, it->second.second);
    };

    // data used to properly align items in columns when showing time
    std::array<float, 4> offsets = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<std::string> labels;
    std::vector<std::string> times;
    std::vector<float> percents;
    std::vector<double> used_filaments_m;
    std::vector<double> used_filaments_g;
    float max_time_percent = 0.0f;

    if (m_view_type == EViewType::FeatureType) {
        // calculate offsets to align time/percentage data
        for (GCodeExtrusionRole role : m_roles) {
            assert(role < GCodeExtrusionRole::Count);
            if (role < GCodeExtrusionRole::Count) {
                labels.push_back(_u8L(gcode_extrusion_role_to_string(role)));
                auto [time, percent] = role_time_and_percent(role);
                times.push_back((time > 0.0f) ? short_time_ui(get_time_dhms(time)) : "");
                percents.push_back(percent);
                max_time_percent = std::max(max_time_percent, percent);
                auto [used_filament_m, used_filament_g] = used_filament_per_role(role);
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

    if (m_view_type == EViewType::Tool) {
        // calculate used filaments data
        used_filaments_m = std::vector<double>(m_extruders_count, 0.0);
        used_filaments_g = std::vector<double>(m_extruders_count, 0.0);
        for (size_t extruder_id : m_extruder_ids) {
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
    int old_view_type = static_cast<int>(get_view_type());
    int view_type = old_view_type;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.1f, 0.1f, 0.8f });
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, { 0.2f, 0.2f, 0.2f, 0.8f });
    std::vector<std::string> view_options;
    std::vector<int> view_options_id;
    if (!m_layers_times.empty() && m_layers.size() == m_layers_times.front().size()) {
        view_options = { _u8L("Feature type"), _u8L("Height (mm)"), _u8L("Width (mm)"), _u8L("Speed (mm/s)"), _u8L("Fan speed (%)"),
                         _u8L("Temperature (°C)"), _u8L("Volumetric flow rate (mm³/s)"), _u8L("Layer time (linear)"), _u8L("Layer time (logarithmic)"),
                         _u8L("Tool"), _u8L("Color Print") };
        view_options_id = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    }
    else {
        view_options = { _u8L("Feature type"), _u8L("Height (mm)"), _u8L("Width (mm)"), _u8L("Speed (mm/s)"), _u8L("Fan speed (%)"),
                         _u8L("Temperature (°C)"), _u8L("Volumetric flow rate (mm³/s)"), _u8L("Tool"), _u8L("Color Print") };
        view_options_id = { 0, 1, 2, 3, 4, 5, 6, 9, 10 };
        if (view_type == 7 || view_type == 8)
            view_type = 0;
    }
    auto view_type_it = std::find(view_options_id.begin(), view_options_id.end(), view_type);
    int view_type_id = (view_type_it == view_options_id.end()) ? 0 : std::distance(view_options_id.begin(), view_type_it);
    if (imgui.combo(std::string(), view_options, view_type_id, ImGuiComboFlags_HeightLargest, 0.0f, -1.0f))
        view_type = view_options_id[view_type_id];
    ImGui::PopStyleColor(2);
   
    if (old_view_type != view_type) {
        set_view_type(static_cast<EViewType>(view_type));
        wxGetApp().plater()->set_keep_current_preview_type(true);
        wxGetApp().plater()->refresh_print();
        view_type_changed = true;
    }

    // extrusion paths section -> title
    if (m_view_type == EViewType::FeatureType)
        append_headers({ "", _u8L("Time"), _u8L("Percentage"), _u8L("Used filament") }, offsets);
    else if (m_view_type == EViewType::Tool)
        append_headers({ "", _u8L("Used filament"), "", ""}, offsets);
    else
        ImGui::Separator();

    if (!view_type_changed) {
        // extrusion paths section -> items
        switch (m_view_type)
        {
        case EViewType::FeatureType:
        {
            max_time_percent = std::max(max_time_percent, time_mode.travel_time / time_mode.time);

            for (size_t i = 0; i < m_roles.size(); ++i) {
                GCodeExtrusionRole role = m_roles[i];
                if (role >= GCodeExtrusionRole::Count)
                    continue;
                const bool visible = is_visible(role);
                append_item(EItemType::Rect, Extrusion_Role_Colors[static_cast<unsigned int>(role)], labels[i],
                    visible, times[i], percents[i], max_time_percent, offsets, used_filaments_m[i], used_filaments_g[i], [this, role, visible]() {
                        m_extrusions.role_visibility_flags = visible ? m_extrusions.role_visibility_flags & ~(1 << int(role)) : m_extrusions.role_visibility_flags | (1 << int(role));
                        // update buffers' render paths
                        refresh_render_paths(false, false);
                        wxGetApp().plater()->update_preview_moves_slider();
                        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                    }
                );
            }

            if (m_buffers[buffer_id(EMoveType::Travel)].visible)
                append_item(EItemType::Line, Travel_Colors[0], _u8L("Travel"), true, short_time_ui(get_time_dhms(time_mode.travel_time)),
                    time_mode.travel_time / time_mode.time, max_time_percent, offsets, 0.0f, 0.0f);

            break;
        }
        case EViewType::Height:               { append_range(m_extrusions.ranges.height, 3); break; }
        case EViewType::Width:                { append_range(m_extrusions.ranges.width, 3); break; }
        case EViewType::Feedrate:             { append_range(m_extrusions.ranges.feedrate, 1); break; }
        case EViewType::FanSpeed:             { append_range(m_extrusions.ranges.fan_speed, 0); break; }
        case EViewType::Temperature:          { append_range(m_extrusions.ranges.temperature, 0); break; }
        case EViewType::VolumetricRate:       { append_range(m_extrusions.ranges.volumetric_rate, 3); break; }
        case EViewType::LayerTimeLinear:      { append_time_range(m_extrusions.ranges.layer_time[static_cast<size_t>(m_time_estimate_mode)], Extrusions::Range::EType::Linear); break; }
        case EViewType::LayerTimeLogarithmic: { append_time_range(m_extrusions.ranges.layer_time[static_cast<size_t>(m_time_estimate_mode)], Extrusions::Range::EType::Logarithmic); break; }
        case EViewType::Tool:                 {
            // shows only extruders actually used
            for (unsigned char extruder_id : m_extruder_ids) {
                if (used_filaments_m[extruder_id] > 0.0 && used_filaments_g[extruder_id] > 0.0)
                    append_item(EItemType::Rect, m_tool_colors[extruder_id], _u8L("Extruder") + " " + std::to_string(extruder_id + 1),
                        true, "", 0.0f, 0.0f, offsets, used_filaments_m[extruder_id], used_filaments_g[extruder_id]);
            }
            break;
        }
        case EViewType::ColorPrint:
        {
            const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = wxGetApp().is_editor() ? wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes : m_custom_gcode_per_print_z;
            size_t total_items = 1;
            for (unsigned char i : m_extruder_ids) {
                total_items += color_print_ranges(i, custom_gcode_per_print_z).size();
            }

            const bool need_scrollable = static_cast<float>(total_items) * icon_size + (static_cast<float>(total_items) - 1.0f) * ImGui::GetStyle().ItemSpacing.y > child_height;

            // add scrollable region, if needed
            if (need_scrollable)
                ImGui::BeginChild("color_prints", { -1.0f, child_height }, false);
            if (m_extruders_count == 1) { // single extruder use case
                const std::vector<std::pair<ColorRGBA, std::pair<double, double>>> cp_values = color_print_ranges(0, custom_gcode_per_print_z);
                const int items_cnt = static_cast<int>(cp_values.size());
                if (items_cnt == 0)  // There are no color changes, but there are some pause print or custom Gcode
                    append_item(EItemType::Rect, m_tool_colors.front(), _u8L("Default color"));
                else {
                    for (int i = items_cnt; i >= 0; --i) {
                        // create label for color change item
                        if (i == 0) {
                            append_item(EItemType::Rect, m_tool_colors[0], upto_label(cp_values.front().second.first));
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
                for (unsigned char i : m_extruder_ids) {
                    const std::vector<std::pair<ColorRGBA, std::pair<double, double>>> cp_values = color_print_ranges(i, custom_gcode_per_print_z);
                    const int items_cnt = static_cast<int>(cp_values.size());
                    if (items_cnt == 0)
                        // There are no color changes, but there are some pause print or custom Gcode
                        append_item(EItemType::Rect, m_tool_colors[i], _u8L("Extruder") + " " + std::to_string(i + 1) + " " + _u8L("default color"));
                    else {
                        for (int j = items_cnt; j >= 0; --j) {
                            // create label for color change item
                            std::string label = _u8L("Extruder") + " " + std::to_string(i + 1);
                            if (j == 0) {
                                label += " " + upto_label(cp_values.front().second.first);
                                append_item(EItemType::Rect, m_tool_colors[i], label);
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
    if (m_view_type == EViewType::ColorPrint) {
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

            std::vector<CustomGCode::Item> custom_gcode_per_print_z = wxGetApp().is_editor() ? wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes : m_custom_gcode_per_print_z;
            std::vector<ColorRGBA> last_color(m_extruders_count);
            for (size_t i = 0; i < m_extruders_count; ++i) {
                last_color[i] = m_tool_colors[i];
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

        auto append_color_change = [&imgui](const ColorRGBA& color1, const ColorRGBA& color2, const std::array<float, 4>& offsets, const Times& times) {
            imgui.text(_u8L("Color change"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiWrapper::to_ImU32(color1));
            pos.x += icon_size;
            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiWrapper::to_ImU32(color2));

            ImGui::SameLine(offsets[0]);
            imgui.text(short_time_ui(get_time_dhms(times.second - times.first)));
        };

        auto append_print = [&imgui, imperial_units](const ColorRGBA& color, const std::array<float, 4>& offsets, const Times& times, std::pair<double, double> used_filament) {
            imgui.text(_u8L("Print"));
            ImGui::SameLine();

            float icon_size = ImGui::GetTextLineHeight();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x -= 0.5f * ImGui::GetStyle().ItemSpacing.x;

            draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f }, { pos.x + icon_size - 1.0f, pos.y + icon_size - 1.0f },
                ImGuiWrapper::to_ImU32(color));

            ImGui::SameLine(offsets[0]);
            imgui.text(short_time_ui(get_time_dhms(times.second)));
            ImGui::SameLine(offsets[1]);
            imgui.text(short_time_ui(get_time_dhms(times.first)));
            if (used_filament.first > 0.0f) {
                char buffer[64];
                ImGui::SameLine(offsets[2]);
                ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", used_filament.first);
                imgui.text(buffer);

                ImGui::SameLine(offsets[3]);
                ::sprintf(buffer, "%.2f g", used_filament.second);
                imgui.text(buffer);
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
                    imgui.text(_u8L("Pause"));
                    ImGui::SameLine(offsets[0]);
                    imgui.text(short_time_ui(get_time_dhms(item.times.second - item.times.first)));
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

    auto add_strings_row_to_table = [&imgui](const std::string& col_1, const ImVec4& col_1_color, const std::string& col_2, const ImVec4& col_2_color) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        imgui.text_colored(col_1_color, col_1.c_str());
        ImGui::TableSetColumnIndex(1);
        imgui.text_colored(col_2_color, col_2.c_str());
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
    show_settings &= (m_view_type == EViewType::FeatureType || m_view_type == EViewType::Tool);
    show_settings &= has_settings;
    if (show_settings) {
        ImGui::Spacing();
        imgui.title(_u8L("Settings"));

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
                add_strings_row_to_table(_u8L("Printer") + ":", ImGuiWrapper::COL_ORANGE_LIGHT,
                    trim_text_if_needed(m_settings_ids.printer), ImGuiWrapper::to_ImVec4(ColorRGBA::WHITE()));
            if (!m_settings_ids.print.empty())
                add_strings_row_to_table(_u8L("Print settings") + ":", ImGuiWrapper::COL_ORANGE_LIGHT,
                    trim_text_if_needed(m_settings_ids.print), ImGuiWrapper::to_ImVec4(ColorRGBA::WHITE()));
            if (!m_settings_ids.filament.empty()) {
                for (unsigned char i : m_extruder_ids) {
                    if (i < static_cast<unsigned char>(m_settings_ids.filament.size()) && !m_settings_ids.filament[i].empty()) {
                        std::string txt = _u8L("Filament");
                        txt += (m_extruder_ids.size() == 1) ? ":" : " " + std::to_string(i + 1);
                        add_strings_row_to_table(txt, ImGuiWrapper::COL_ORANGE_LIGHT,
                            trim_text_if_needed(m_settings_ids.filament[i]), ImGuiWrapper::to_ImVec4(ColorRGBA::WHITE()));
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    if (m_view_type == EViewType::Width || m_view_type == EViewType::VolumetricRate) {
      const auto custom_it = std::find(m_roles.begin(), m_roles.end(), GCodeExtrusionRole::Custom);
      if (custom_it != m_roles.end()) {
          const bool custom_visible = is_visible(GCodeExtrusionRole::Custom);
          const wxString btn_text = custom_visible ? _L("Hide Custom G-code") : _L("Show Custom G-code");
          ImGui::Separator();
          if (imgui.button(btn_text, ImVec2(-1.0f, 0.0f), true)) {
              m_extrusions.role_visibility_flags = custom_visible ? m_extrusions.role_visibility_flags & ~(1 << int(GCodeExtrusionRole::Custom)) :
                  m_extrusions.role_visibility_flags | (1 << int(GCodeExtrusionRole::Custom));
              wxGetApp().plater()->refresh_print();
          }
        }
    }

    // total estimated printing time section
    if (show_estimated_time) {
        ImGui::Spacing();
        std::string time_title = _u8L("Estimated printing times");
        auto can_show_mode_button = [this](PrintEstimatedStatistics::ETimeMode mode) {
            bool show = false;
            if (m_print_statistics.modes.size() > 1 && m_print_statistics.modes[static_cast<size_t>(mode)].roles_times.size() > 0) {
                for (size_t i = 0; i < m_print_statistics.modes.size(); ++i) {
                    if (i != static_cast<size_t>(mode) &&
                        m_print_statistics.modes[i].time > 0.0f &&
                        short_time(get_time_dhms(m_print_statistics.modes[static_cast<size_t>(mode)].time)) != short_time(get_time_dhms(m_print_statistics.modes[i].time))) {
                        show = true;
                        break;
                    }
                }
            }
            return show;
        };

        if (can_show_mode_button(m_time_estimate_mode)) {
            switch (m_time_estimate_mode)
            {
            case PrintEstimatedStatistics::ETimeMode::Normal:  { time_title += " [" + _u8L("Normal mode") + "]"; break; }
            case PrintEstimatedStatistics::ETimeMode::Stealth: { time_title += " [" + _u8L("Stealth mode") + "]"; break; }
            default: { assert(false); break; }
            }
        }

        imgui.title(time_title + ":");

        if (ImGui::BeginTable("Times", 2)) {
            if (!time_mode.layers_times.empty()) {
                add_strings_row_to_table(_u8L("First layer") + ":", ImGuiWrapper::COL_ORANGE_LIGHT,
                    short_time_ui(get_time_dhms(time_mode.layers_times.front())), ImGuiWrapper::to_ImVec4(ColorRGBA::WHITE()));
            }

            add_strings_row_to_table(_u8L("Total") + ":", ImGuiWrapper::COL_ORANGE_LIGHT,
                short_time_ui(get_time_dhms(time_mode.time)), ImGuiWrapper::to_ImVec4(ColorRGBA::WHITE()));

            ImGui::EndTable();
        }

        auto show_mode_button = [this, &imgui, can_show_mode_button](const wxString& label, PrintEstimatedStatistics::ETimeMode mode) {
            if (can_show_mode_button(mode)) {
                if (imgui.button(label)) {
                    m_time_estimate_mode = mode;
                    if (m_view_type == EViewType::LayerTimeLinear || m_view_type == EViewType::LayerTimeLogarithmic)
                        refresh_render_paths(false, false);
                    imgui.set_requires_extra_frame();
                }
            }
        };

        switch (m_time_estimate_mode) {
        case PrintEstimatedStatistics::ETimeMode::Normal: {
            show_mode_button(_L("Show stealth mode"), PrintEstimatedStatistics::ETimeMode::Stealth);
            break;
        }
        case PrintEstimatedStatistics::ETimeMode::Stealth: {
            show_mode_button(_L("Show normal mode"), PrintEstimatedStatistics::ETimeMode::Normal);
            break;
        }
        default : { assert(false); break; }
        }
    }

    // toolbar section
    auto toggle_button = [this, &imgui, icon_size](Preview::OptionType type, const std::string& name,
        std::function<void(ImGuiWindow& window, const ImVec2& pos, float size)> draw_callback) {
            auto is_flag_set = [](unsigned int flags, unsigned int flag) {
            return (flags & (1 << flag)) != 0;
        };

        auto set_flag = [](unsigned int flags, unsigned int flag, bool active) {
            return active ? (flags | (1 << flag)) : (flags & ~(1 << flag));
        };

        unsigned int flags = get_options_visibility_flags();
        unsigned int flag = static_cast<unsigned int>(type);
        bool active = is_flag_set(flags, flag);

        if (imgui.draw_radio_button(name, 1.5f * icon_size, active, draw_callback)) {
            unsigned int new_flags = set_flag(flags, flag, !active);
            set_options_visibility_from_flags(new_flags);

            const unsigned int diff_flags = flags ^ new_flags;
            if (m_view_type == GCodeViewer::EViewType::Feedrate && is_flag_set(diff_flags, static_cast<unsigned int>(Preview::OptionType::Travel)))
                wxGetApp().plater()->refresh_print();
            else {
                bool keep_first = m_sequential_view.current.first != m_sequential_view.global.first;
                bool keep_last = m_sequential_view.current.last != m_sequential_view.global.last;
                wxGetApp().plater()->get_current_canvas3D()->refresh_gcode_preview_render_paths(keep_first, keep_last);
            }
            wxGetApp().plater()->update_preview_moves_slider();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
            ImGui::BeginTooltip();
            imgui.text(name);
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

    imgui.end();
    ImGui::PopStyleVar();
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeViewer::render_statistics()
{
    static const float offset = 275.0f;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto add_time = [&imgui](const std::string& label, int64_t time) {
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(std::to_string(time) + " ms (" + get_time_dhms(static_cast<float>(time) * 0.001f) + ")");
    };

    auto add_memory = [&imgui](const std::string& label, int64_t memory) {
        auto format_string = [memory](const std::string& units, float value) {
            return std::to_string(memory) + " bytes (" +
                   Slic3r::float_to_string_decimal_point(float(memory) * value, 3)
                    + " " + units + ")";
        };

        static const float kb = 1024.0f;
        static const float inv_kb = 1.0f / kb;
        static const float mb = 1024.0f * kb;
        static const float inv_mb = 1.0f / mb;
        static const float gb = 1024.0f * mb;
        static const float inv_gb = 1.0f / gb;
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        if (static_cast<float>(memory) < mb)
            imgui.text(format_string("KB", inv_kb));
        else if (static_cast<float>(memory) < gb)
            imgui.text(format_string("MB", inv_mb));
        else
            imgui.text(format_string("GB", inv_gb));
    };

    auto add_counter = [&imgui](const std::string& label, int64_t counter) {
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(std::to_string(counter));
    };

    imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
    ImGui::SetNextWindowSizeConstraints({ 300.0f, 100.0f }, { 600.0f, 900.0f });
    imgui.begin(std::string("GCodeViewer Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImGui::CollapsingHeader("Time")) {
        add_time(std::string("GCodeProcessor:"), m_statistics.results_time);

        ImGui::Separator();
        add_time(std::string("Load:"), m_statistics.load_time);
        add_time(std::string("  Load vertices:"), m_statistics.load_vertices);
        add_time(std::string("  Smooth vertices:"), m_statistics.smooth_vertices);
        add_time(std::string("  Load indices:"), m_statistics.load_indices);
        add_time(std::string("Refresh:"), m_statistics.refresh_time);
        add_time(std::string("Refresh paths:"), m_statistics.refresh_paths_time);
    }

    if (ImGui::CollapsingHeader("OpenGL calls")) {
        add_counter(std::string("Multi GL_LINES:"), m_statistics.gl_multi_lines_calls_count);
        add_counter(std::string("Multi GL_TRIANGLES:"), m_statistics.gl_multi_triangles_calls_count);
        add_counter(std::string("GL_TRIANGLES:"), m_statistics.gl_triangles_calls_count);
        ImGui::Separator();
        add_counter(std::string("Instanced models:"), m_statistics.gl_instanced_models_calls_count);
        add_counter(std::string("Batched models:"), m_statistics.gl_batched_models_calls_count);
    }

    if (ImGui::CollapsingHeader("CPU memory")) {
        add_memory(std::string("GCodeProcessor results:"), m_statistics.results_size);

        ImGui::Separator();
        add_memory(std::string("Paths:"), m_statistics.paths_size);
        add_memory(std::string("Render paths:"), m_statistics.render_paths_size);
        add_memory(std::string("Models instances:"), m_statistics.models_instances_size);
    }

    if (ImGui::CollapsingHeader("GPU memory")) {
        add_memory(std::string("Vertices:"), m_statistics.total_vertices_gpu_size);
        add_memory(std::string("Indices:"), m_statistics.total_indices_gpu_size);
        add_memory(std::string("Instances:"), m_statistics.total_instances_gpu_size);
        ImGui::Separator();
        add_memory(std::string("Max VBuffer:"), m_statistics.max_vbuffer_gpu_size);
        add_memory(std::string("Max IBuffer:"), m_statistics.max_ibuffer_gpu_size);
    }

    if (ImGui::CollapsingHeader("Other")) {
        add_counter(std::string("Travel segments count:"), m_statistics.travel_segments_count);
        add_counter(std::string("Wipe segments count:"), m_statistics.wipe_segments_count);
        add_counter(std::string("Extrude segments count:"), m_statistics.extrude_segments_count);
        add_counter(std::string("Instances count:"), m_statistics.instances_count);
        add_counter(std::string("Batched count:"), m_statistics.batched_count);
        ImGui::Separator();
        add_counter(std::string("VBuffers count:"), m_statistics.vbuffers_count);
        add_counter(std::string("IBuffers count:"), m_statistics.ibuffers_count);
    }

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

void GCodeViewer::log_memory_used(const std::string& label, int64_t additional) const
{
    if (Slic3r::get_logging_level() >= 5) {
        int64_t paths_size = 0;
        int64_t render_paths_size = 0;
        for (const TBuffer& buffer : m_buffers) {
            paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
            render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
            for (const RenderPath& path : buffer.render_paths) {
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
            }
        }
        int64_t layers_size = SLIC3R_STDVEC_MEMSIZE(m_layers.get_zs(), double);
        layers_size += SLIC3R_STDVEC_MEMSIZE(m_layers.get_ranges(), Layers::Range);
        BOOST_LOG_TRIVIAL(trace) << label
            << "(" << format_memsize_MB(additional + paths_size + render_paths_size + layers_size) << ");"
            << log_memory_info();
    }
}

ColorRGBA GCodeViewer::option_color(EMoveType move_type) const
{
    switch (move_type)
    {
    case EMoveType::Tool_change:  { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ToolChanges)]; }
    case EMoveType::Color_change: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ColorChanges)]; }
    case EMoveType::Pause_Print:  { return Options_Colors[static_cast<unsigned int>(EOptionsColors::PausePrints)]; }
    case EMoveType::Custom_GCode: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::CustomGCodes)]; }
    case EMoveType::Retract:      { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Retractions)]; }
    case EMoveType::Unretract:    { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Unretractions)]; }
    case EMoveType::Seam:         { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Seams)]; }
    default:                      { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    }
}

} // namespace GUI
} // namespace Slic3r
