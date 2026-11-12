/*
 *  THIS FILE IS A MODIFICATION OF THE ORIGINAL ImGuizmo.cpp file from LIBRARY:
 *  https://github.com/CedricGuillemet/ImGuizmo
 *  WHOSE LICENSE IS AS FOLLOWS:
 *
 *  v1.91.3 WIP
 *
 *  The MIT License(MIT)
 *
 *  Copyright(c) 2021 Cedric Guillemet
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "Slic3r/App/Imgui/NavigationCube.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <array>
#include <string>

#include <math.h>

namespace Slic3r::App::Imgui::NavCube {

static void frustum(float left, float right, float bottom, float top, float z_near, float z_far, Domain::SquareMatrix4f& m)
{
    float temp = 2.0f * z_near;
    float temp2 = right - left;
    float temp3 = top - bottom;
    float temp4 = z_far - z_near;
    m <<
        temp / temp2, 0.0f, (right + left) / temp2, 0.0f,
        0.0f, temp / temp3, (top + bottom) / temp3, 0.0f,
        0.0f, 0.0f, (-z_far - z_near) / temp4, (-temp * z_far) / temp4,
        0.0f, 0.0f, -1.0f, 0.0f;
}

static void perspective(float fovy_in_degrees, float aspect_ratio, float z_near, float z_far, Domain::SquareMatrix4f& m)
{
    float y_max = z_near * tanf(deg2rad(fovy_in_degrees));
    float x_max = y_max * aspect_ratio;
    frustum(-x_max, x_max, -y_max, y_max, z_near, z_far, m);
}

static void orthographic(float l, float r, float b, float t, float z_near, float z_far, Domain::SquareMatrix4f& m)
{
    m <<
        2.0f / (r - l), 0.0f, 0.0f, (l + r) / (l - r),
        0.0f, 2.0f / (t - b), 0.0f, (t + b) / (b - t),
        0.0f, 0.0f, 1.0f / (z_far - z_near), z_near / (z_near - z_far),
        0.0f, 0.0f, 0.0f, 1.0f;
}

static void look_at(const Domain::Vec3f& eye, const Domain::Vec3f& at, const Domain::Vec3f& up, Domain::SquareMatrix4f& m)
{
    Domain::Vec3f tmp = eye - at;
    Domain::Vec3f z = tmp.normalized();
    Domain::Vec3f y = up.normalized();
    Domain::Vec3f x = y.cross(z).normalized();
    y = z.cross(x).normalized();

    m << 
        x[0], x[1], x[2], -x.dot(eye),
        y[0], y[1], y[2], -y.dot(eye),
        z[0], z[1], z[2], -z.dot(eye),
        0.0f, 0.0f, 0.0f, 1.0f;
}

static Domain::Vec4f build_plan(const Domain::Vec3f& p_point, const Domain::Vec3f& p_normal)
{
    Domain::Vec3f normal = p_normal.normalized();
    return { normal.x(), normal.y(), normal.z(), normal.dot(p_point) };
}

enum class Color
{
    DirX,
    DirY,
    DirZ,
    Text,
    Face,
    FaceHighlight,
    Count
};

enum class Face
{
    Right,
    Back,
    Top,
    Left,
    Front,
    Bottom
};

struct Style
{
    Style() {
        // initialize default colors
        Colors[size_t(Color::DirX)] = to_ImVec4(Domain::ColorRGBA::X());
        Colors[size_t(Color::DirY)] = to_ImVec4(Domain::ColorRGBA::Y());
        Colors[size_t(Color::DirZ)] = to_ImVec4(Domain::ColorRGBA::Z());
        Colors[size_t(Color::Text)] = ImVec4(0.000f, 0.000f, 0.000f, 1.000f);
        Colors[size_t(Color::Face)] = ImVec4(0.808f, 0.808f, 0.800f, 0.800f);
        Colors[size_t(Color::FaceHighlight)] = ImVec4(0.918f, 0.498f, 0.259f, 1.000f);

        axis_labels = {"X", "Y", "Z"};
        // Right, Back, Top, Left, Front, Bottom
        face_labels = {"+X", "+Y", "+Z", "-X", "-Y", "-Z"};
    }

    float translation_line_thickness{3.0f};
    float translation_line_arrow_size{6.0f};

    std::array<ImVec4, size_t(Color::Count)> Colors;
    std::array<std::string, 3> axis_labels;
    std::array<std::string, 6> face_labels;
};

struct Context
{
    ImDrawList* drawlist{nullptr};
    Style style;

    Domain::SquareMatrix4f view_mat;
    Domain::SquareMatrix4f projection_mat;
    Domain::SquareMatrix4f view_projection;

    Domain::Vec3f ray_origin;
    Domain::Vec3f ray_vector;

    bool animation_running{ false };

    bool is_view_manipulator_hovered{false};
    bool mouse_over{false};
    bool reversed{false};

    bool is_orthographic{false};
    float gizmo_size_clip_space{0.1f};

    float azimuth{ 0.0f };
    float zenith{ 0.0f };
};

static Context gContext;

class Interpolation
{
public:
    void set(const Domain::Vec3f& start_dir, const Domain::Vec3f& end_dir)
    {
        Domain::Vec3f start_dir_xy = Domain::Vec3f(start_dir.x(), start_dir.y(), 0.0f).normalized();
        Domain::Vec3f end_dir_xy = Domain::Vec3f(end_dir.x(), end_dir.y(), 0.0f).normalized();

        m_start_azimuth = std::acos(std::clamp(start_dir_xy.dot(-Domain::Vec3f::UnitX()), -1.0f, 1.0f));
        if (start_dir_xy.cross(Domain::Vec3f::UnitX()).dot(Domain::Vec3f::UnitZ()) < 0.0f)
            m_start_azimuth = 2.0f * float(M_PI) - m_start_azimuth;
        m_start_zenith  = std::acos(std::clamp(start_dir.dot(-Domain::Vec3f::UnitZ()), -1.0f, 1.0f));

        float end_zenith = std::acos(std::clamp(end_dir.dot(-Domain::Vec3f::UnitZ()), -1.0f, 1.0f));
        float end_azimuth;
        if (end_dir_xy.y() > 0.0f) {
            if (std::abs(end_dir_xy.x()) < FLT_EPSILON)
                end_azimuth = 1.5f * float(M_PI);
            else if (end_dir_xy.x() < 0.0f)
                end_azimuth = 1.75f * float(M_PI);
            else
                end_azimuth = 1.25f * float(M_PI);
        }
        else {
            end_azimuth = std::acos(std::clamp(end_dir_xy.dot(-Domain::Vec3f::UnitX()), -1.0f, 1.0f));
            if (end_dir_xy.cross(Domain::Vec3f::UnitX()).dot(Domain::Vec3f::UnitZ()) < 0.0f)
                end_azimuth = -end_azimuth;
        }

        m_delta_zenith = end_zenith - m_start_zenith;
        m_delta_azimuth = end_azimuth - m_start_azimuth;
        if (std::abs(m_delta_azimuth) > float(M_PI)) {
            if (m_delta_azimuth > 0.0f)
                m_delta_azimuth = -(2.0f * float(M_PI) - m_delta_azimuth);
            else
                m_delta_azimuth = 2.0f * float(M_PI) + m_delta_azimuth;
        }

        gContext.azimuth = m_start_azimuth;
        gContext.zenith = m_start_zenith;

        m_remaining_frames = (m_delta_azimuth != 0.0f || m_delta_zenith != 0.0f) ? MAX_FRAMES_COUNT : -1;
    }


    bool running() const {
        return 0 <= m_remaining_frames;
    }

    void interpolate() {
        if (m_remaining_frames < 0)
            return;

        else if (m_remaining_frames > 0){
            float curr_frame = smoothstep(float(1 + MAX_FRAMES_COUNT - m_remaining_frames) / MAX_FRAMES_COUNT);
            gContext.azimuth = m_start_azimuth + curr_frame * m_delta_azimuth;
            gContext.zenith = m_start_zenith + curr_frame * m_delta_zenith;
        }
        --m_remaining_frames;
    }

private:
    float m_start_azimuth;
    float m_start_zenith;

    float m_delta_azimuth;
    float m_delta_zenith;

    int m_remaining_frames{-1};

    static constexpr int MAX_FRAMES_COUNT = 40;
};

static const Domain::Vec3f direction_unary[3] = { Domain::Vec3f::UnitX(), Domain::Vec3f::UnitY(), Domain::Vec3f::UnitZ() };

static ImU32 get_color_u32(int idx)
{
    IM_ASSERT(idx < int(Color::Count));
    return ImGui::ColorConvertFloat4ToU32(gContext.style.Colors[idx]);
}

static ImVec2 world_to_pos(const Domain::Vec3f& world_pos, const Domain::SquareMatrix4f& m, const ImVec2& pos = ImVec2(0.0f, 0.0f),
    const ImVec2& size = ImVec2(0.0f, 0.0f))
{
    Domain::Vec4f world_pos_homo(world_pos.x(), world_pos.y(), world_pos.z(), 1.0f);
    Domain::Vec4f trans = m * world_pos_homo;
    trans *= 0.5f / trans.w();
    trans += Domain::Vec4f(0.5f, 0.5f, 0.0f, 0.0f);
    trans.y() = 1.0f - trans.y();
    trans.x() *= size.x;
    trans.y() *= size.y;
    trans.x() += pos.x;
    trans.y() += pos.y;
    return { trans.x(), trans.y() };
}

static void compute_camera_ray(Domain::Vec3f& ray_origin, Domain::Vec3f& ray_dir, const ImVec2& position = ImVec2(0.0f, 0.0f),
    const ImVec2& size = ImVec2(0.0f, 0.0f))
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.MousePos.x == -FLT_MAX || io.MousePos.y == -FLT_MAX)
        return;

    Domain::SquareMatrix4f view_proj_inverse = (gContext.projection_mat * gContext.view_mat).inverse();

    const float mox = ((io.MousePos.x - position.x) / size.x) * 2.0f - 1.0f;
    const float moy = (1.0f - ((io.MousePos.y - position.y) / size.y)) * 2.0f - 1.0f;

    const float z_near = gContext.reversed ? (1.0f - FLT_EPSILON) : 0.0f;
    const float z_far = gContext.reversed ? 0.0f : (1.0f - FLT_EPSILON);

    Domain::Vec4f ray_origin_homo = view_proj_inverse * Domain::Vec4f(mox, moy, z_near, 1.0f);
    ray_origin = Domain::Vec3f(ray_origin_homo.x(), ray_origin_homo.y(), ray_origin_homo.z()) / ray_origin_homo.w();

    Domain::Vec4f ray_end_homo = view_proj_inverse * Domain::Vec4f(mox, moy, z_far, 1.0f);
    Domain::Vec3f ray_end = Domain::Vec3f(ray_end_homo.x(), ray_end_homo.y(), ray_end_homo.z()) / ray_end_homo.w();
    ray_dir = (ray_end - ray_origin).normalized();
}

static float intersect_ray_plane(const Domain::Vec3f& r_origin, const Domain::Vec3f& r_vector, const Domain::Vec4f& plan)
{
    Domain::Vec3f plan_normal(plan.x(), plan.y(), plan.z());

    float numer = plan_normal.dot(r_origin) - plan.w();
    float denom = plan_normal.dot(r_vector);

    if (fabsf(denom) < FLT_EPSILON)  // normal is orthogonal to vector, cant intersect
        return -1.0f;

    return -(numer / denom);
}

static bool is_hovering_window()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* window = ImGui::FindWindowByName(gContext.drawlist->_OwnerName);
    if (g.HoveredWindow == window)   // Mouse hovering drawlist window
        return true;
    if (g.HoveredWindow != NULL)     // Any other window is hovered
        return false;
    if (ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max, false))   // Hovering drawlist window rect, while no other window is hovered (for _NoInputs windows)
        return true;
    return false;
}

void set_orthographic(bool is_orthographic)
{
    gContext.is_orthographic = is_orthographic;
}

void set_draw_list(ImDrawList* drawlist)
{
    gContext.drawlist = drawlist ? drawlist : ImGui::GetWindowDrawList();
}

bool is_animation_running()
{
    return gContext.animation_running;
}

bool is_view_manipulate_hovered()
{
    return gContext.is_view_manipulator_hovered;
}

static void compute_context(const Domain::SquareMatrix4f& view, const Domain::SquareMatrix4f& projection)
{
    gContext.view_mat = view;
    gContext.projection_mat = projection;
    gContext.view_projection = gContext.projection_mat * gContext.view_mat;
    gContext.mouse_over = is_hovering_window();

    // projection reverse
    Domain::Vec4f near_pos = gContext.projection_mat * Domain::Vec4f(0.0f, 0.0f, 1.0f, 1.0f);
    Domain::Vec4f far_pos = gContext.projection_mat * Domain::Vec4f(0.0f, 0.0f, 2.0f, 1.0f);
    gContext.reversed = near_pos.z() / near_pos.w() > far_pos.z() / far_pos.w();

    compute_camera_ray(gContext.ray_origin, gContext.ray_vector);
}

static void view_manipulate(const Domain::SquareMatrix4f& view, float length, const ImVec2& position, const ImVec2& size,
    ImU32 backgroundColor)
{
    if (size == ImVec2(0.0f, 0.0f))
        return;

    static bool is_clicking = false;
    static Domain::Vec3f interpolation_dir;

    static Interpolation interpolation;

    Domain::SquareMatrix4f svg_view = gContext.view_mat;
    Domain::SquareMatrix4f svg_projection = gContext.projection_mat;

    gContext.drawlist->PushClipRectFullScreen();

    ImGuiIO& io = ImGui::GetIO();
    gContext.drawlist->AddRectFilled(position, position + size, backgroundColor);

    Domain::SquareMatrix4f view_inverse = view.inverse();

    // view/projection matrices
    float distance = 3.0f;
    Domain::SquareMatrix4f cube_projection, cube_view;
    if (gContext.is_orthographic)
        orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.01f, 1000.0f, cube_projection);
    else {
        float fov = rad2deg(acosf(distance / (sqrtf(distance * distance + 3.0f))));
        perspective(fov / sqrtf(2.0f), size.x / size.y, 0.01f, 1000.0f, cube_projection);
    }

    Domain::Vec3f dir = view_inverse.block<3, 1>(0, 2);
    Domain::Vec3f up = view_inverse.block<3, 1>(0, 1);
    Domain::Vec3f eye = dir * distance;
    look_at(eye, Domain::Vec3f::Zero(), up, cube_view);

    // set context
    gContext.view_mat = cube_view;
    gContext.projection_mat = cube_projection;

    compute_camera_ray(gContext.ray_origin, gContext.ray_vector, position, size);

    Domain::SquareMatrix4f res = cube_projection * cube_view;

    // panels
    static const ImVec2 panel_position[9] = {
        ImVec2(0.75f,0.75f), ImVec2(0.25f, 0.75f), ImVec2(0.0f, 0.75f),
        ImVec2(0.75f, 0.25f), ImVec2(0.25f, 0.25f), ImVec2(0.0f, 0.25f),
        ImVec2(0.75f, 0.0f), ImVec2(0.25f, 0.0f), ImVec2(0.0f, 0.0f)
    };

    static const ImVec2 panel_size[9] = {
        ImVec2(0.25f,0.25f), ImVec2(0.5f, 0.25f), ImVec2(0.25f, 0.25f),
        ImVec2(0.25f, 0.5f), ImVec2(0.5f, 0.5f), ImVec2(0.25f, 0.5f),
        ImVec2(0.25f, 0.25f), ImVec2(0.5f, 0.25f), ImVec2(0.25f, 0.25f)
    };

    // tag faces
    bool boxes[27]{};
    static int over_box = -1;
    for (int i_pass = 0; i_pass < 2; i_pass++) {
        for (int i_face = 0; i_face < 6; i_face++) {
            int normal_index = (i_face % 3);
            int perp_x_index = (normal_index + 1) % 3;
            int perp_y_index = (normal_index + 2) % 3;
            float invert = (i_face > 2) ? -1.0f : 1.0f;
            Domain::Vec3f index_vector_x = direction_unary[perp_x_index] * invert;
            Domain::Vec3f index_vector_y = direction_unary[perp_y_index] * invert;
            Domain::Vec3f box_origin = direction_unary[normal_index] * -invert - index_vector_x - index_vector_y;

            // plan local space
            Domain::Vec3f n = direction_unary[normal_index] * invert;
            Domain::Vec4f n_homo(n.x(), n.y(), n.z(), 0.0f);
            Domain::Vec4f view_space_normal_homo = cube_view * n_homo;
            Domain::Vec3f view_space_normal = Domain::Vec3f(view_space_normal_homo.x(), view_space_normal_homo.y(), view_space_normal_homo.z()).normalized();

            Domain::Vec3f view_space_point = Domain::Transform3f(cube_view) * (n * 0.5f);
            Domain::Vec4f view_space_face_plan = build_plan(view_space_point, view_space_normal);

            // back face culling
            // Threshold for orthographic camera is needed to avoid unwanted culling of faces
            if ((gContext.is_orthographic && view_space_face_plan.w() > 0.5f) ||
               (!gContext.is_orthographic && view_space_face_plan.w() > 0.0f))
                continue;

            Domain::Vec4f face_plan = build_plan(n * 0.5f, n);
            float len = intersect_ray_plane(gContext.ray_origin, gContext.ray_vector, face_plan);
            Domain::Vec3f pos_on_plan = gContext.ray_origin + gContext.ray_vector * len - (n * 0.5f);

            float localx = direction_unary[perp_x_index].dot(pos_on_plan) * invert + 0.5f;
            float localy = direction_unary[perp_y_index].dot(pos_on_plan) * invert + 0.5f;

            // panels
            Domain::Vec3f dx = direction_unary[perp_x_index];
            Domain::Vec3f dy = direction_unary[perp_y_index];
            Domain::Vec3f origin = direction_unary[normal_index] - dx - dy;

            for (int i_panel = 0; i_panel < 9; i_panel++) {
                Domain::Vec3f box_coord = box_origin + index_vector_x * float(i_panel % 3) + index_vector_y * float(i_panel / 3) + Domain::Vec3f::Ones();

                ImVec2 p = panel_position[i_panel] * 2.0f;
                ImVec2 s = panel_size[i_panel] * 2.0f;
                ImVec2 faceC_coords_screen[4];
                Domain::Vec3f panel_pos[4] = { dx * p.x + dy * p.y,
                                               dx * p.x + dy * (p.y + s.y),
                                               dx * (p.x + s.x) + dy * (p.y + s.y),
                                               dx * (p.x + s.x) + dy * p.y };

                for (unsigned int iCoord = 0; iCoord < 4; iCoord++) {
                    faceC_coords_screen[iCoord] = world_to_pos((panel_pos[iCoord] + origin) * 0.5f * invert, res, position, size);
                }

                ImVec2 panel_corners[2] = { panel_position[i_panel], panel_position[i_panel] + panel_size[i_panel] };
                bool inside_panel = localx > panel_corners[0].x && localx < panel_corners[1].x &&
                    localy > panel_corners[0].y && localy < panel_corners[1].y;
                int box_coord_int = int(box_coord.x() * 9.0f + box_coord.y() * 3.0f + box_coord.z());
                IM_ASSERT(box_coord_int < 27);
                boxes[box_coord_int] |= inside_panel && gContext.mouse_over;

                if (i_pass) {
                    // draw faces
                    gContext.drawlist->AddConvexPolyFilled(faceC_coords_screen, 4, get_color_u32(int(Color::Face)));
                    if (boxes[box_coord_int]) {
                        gContext.drawlist->AddConvexPolyFilled(faceC_coords_screen, 4, get_color_u32(int(Color::FaceHighlight)));
                        if (io.MouseDown[0] && !is_clicking && GImGui->ActiveId == 0) {
                            over_box = box_coord_int;
                            is_clicking = true;
                        }
                    }

                    // draw face labels
                    {
                        int vtx_write_start = gContext.drawlist->VtxBuffer.Size;

                        auto label = gContext.style.face_labels[i_face];
                        ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
                        float scale_factor = 2.0f / size.y;
                        auto label_origin = label_size * 0.5f;
                        gContext.drawlist->AddText(ImVec2(0.0f, 0.0f), get_color_u32(int(Color::Text)), label.c_str());
                        ImDrawVert* vtx_write_end = gContext.drawlist->_VtxWritePtr;

                        Domain::Vec3f tdx = direction_unary[perp_x_index];
                        Domain::Vec3f tdy = direction_unary[perp_y_index];

                        ImVec2 invert2 = {1, 1};
                        switch (Face(i_face)) {
                          case Face::Right:
                              invert2.y = -1;
                              break;
                          case Face::Back:
                              tdx = direction_unary[0];
                              tdy = direction_unary[2];
                              invert2 = {-1, -1};
                              break;
                          case Face::Top:
                              invert2.y = -1;
                              break;
                          case Face::Left:
                              break;
                          case Face::Front:
                              tdx = direction_unary[0];
                              tdy = direction_unary[2];
                              invert2.x = -1;
                              break;
                          case Face::Bottom:
                              invert2 = {-1, -1};
                              break;
                        }

                        for (auto v = (gContext.drawlist->VtxBuffer.Data + vtx_write_start); v < vtx_write_end; v++) {
                            auto  pp = ((v->pos - label_origin) * scale_factor * invert2 + ImVec2{0.5, 0.5}) * 2.f;
                            Domain::Vec3f pt = tdx * pp.x + tdy * pp.y;
                            v->pos = world_to_pos((pt + origin) * 0.5 * invert, res, position, size);
                        }
                    }
                }
            }
        }
    }
    
    // draw axes
    {
        Domain::Vec3f origin(-0.5f, -0.5f, -0.5f);

        for (int i = 0; i < 3; ++i) {
            Domain::Vec3f dir_axis = direction_unary[i];

            ImVec2 base_sspace = world_to_pos(origin, res, position, size);
            ImVec2 world_dir_sspace = world_to_pos(origin + dir_axis, res, position, size);

            bool visible = false;
            {
                Domain::Vec3f mid = origin + (dir_axis * 0.5f);
                Domain::Vec3f eye = Domain::Vec3f(0.0f, 0.0f, 0.5f).normalized();

                for (int j = 1; j <= 2; j++) {
                    Domain::Vec3f f = mid + (direction_unary[(i + j) % 3] * 0.5f);
                    Domain::Vec4f f_homo(f.x(), f.y(), f.z(), 0.0f);
                    Domain::Vec4f f_homo_transf = cube_view * f_homo;
                    f = Domain::Vec3f(f_homo_transf.x(), f_homo_transf.y(), f_homo_transf.z()).normalized();

                    auto w = f.dot(eye);
                    if (w > 0.0f) {
                        visible = true;
                        break;
                    }
                }
            }

            ImVec4 direction_color_v = gContext.style.Colors[size_t(Color::DirX) + i];
            if (!visible)
                direction_color_v.w *= 0.3f;

            ImU32 direction_color = ImGui::ColorConvertFloat4ToU32(direction_color_v);
            gContext.drawlist->AddLine(base_sspace, world_dir_sspace, direction_color, gContext.style.translation_line_thickness);

            // Arrow head begin
            ImVec2 dir(base_sspace - world_dir_sspace);

            float d = sqrtf(ImLengthSqr(dir));
            dir /= d; // Normalize
            dir *= gContext.style.translation_line_arrow_size;
            
            ImVec2 ortogonal_dir(dir.y, -dir.x); // Perpendicular vector
            ImVec2 a(world_dir_sspace + dir);
            gContext.drawlist->AddTriangleFilled(world_dir_sspace - dir, a + ortogonal_dir, a - ortogonal_dir, direction_color);
            // Arrow head end

            // Axis text
            ImVec2 label_sspace = world_to_pos(origin + dir_axis * 1.3f, res, position, size);
            ImVec2 label_size = ImGui::CalcTextSize(gContext.style.axis_labels[i].c_str());
            gContext.drawlist->AddText(label_sspace - label_size * 0.5f, direction_color, gContext.style.axis_labels[i].c_str());
        }
    }

    if (interpolation.running())
        interpolation.interpolate();

    gContext.is_view_manipulator_hovered = gContext.mouse_over && ImRect(position, position + size).Contains(io.MousePos);

    if (io.MouseDown[0] && (fabsf(io.MouseDelta[0]) || fabsf(io.MouseDelta[1])) && is_clicking)
        is_clicking = false;

    if (!io.MouseDown[0]) {
        if (is_clicking) {
            // apply new view direction
            int cx = over_box / 9;
            int cy = (over_box - cx * 9) / 3;
            int cz = over_box % 3;
            interpolation_dir = Domain::Vec3f(1.0f - (float)cx, 1.0f - (float)cy, 1.0f - (float)cz).normalized();
            if (interpolation_dir.dot(dir) <= 1.0f - 0.01f)
                interpolation.set(dir, interpolation_dir);
        }
        is_clicking = false;
    }

    gContext.animation_running = interpolation.running();
    if (is_clicking || gContext.animation_running || gContext.is_view_manipulator_hovered)
        ImGui::SetNextFrameWantCaptureMouse(true);

    // restore view/projection because it was used to compute ray
    compute_context(svg_view, svg_projection);

    gContext.drawlist->PopClipRect();
}

void view_manipulate(const Domain::SquareMatrix4f& view, const Domain::SquareMatrix4f& projection, float length, const ImVec2& position,
    const ImVec2& size, ImU32 backgroundColor)
{
    // Scale is always local or matrix will be skewed when applying world scale or oriented matrix
    compute_context(view, projection);
    view_manipulate(view, length, position, size, backgroundColor);
}

std::pair<float, float> get_azimuth_and_zenith()
{
    return std::make_pair(gContext.azimuth, gContext.zenith);
}

} // namespace Slic3r::App::Imgui::NavCube
