/*
 *  THIS FILE IS A MODIFICATION OF THE ORIGINAL ImGuizmo.h file from LIBRARY:
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

#pragma once

#include "Slic3r/Domain/Types.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <utility>

namespace Slic3r::App::Imgui::NavCube {

    /**
      * @brief Set ImGui drawlist
      * 
      * @note: call inside your own window and before view_manipulate() in order to draw gizmo to that window.
      *        or pass a specific ImDrawList to draw to (e.g. ImGui::GetForegroundDrawList()).
      */
    void set_draw_list(ImDrawList* drawlist = nullptr);
    /**
      * @brief return true if the view manipulator is in animation state
      */
    bool is_animation_running();
    /**
      * @brief only check if your mouse is over the view manipulator
      */
    bool is_view_manipulate_hovered();
    /**
      * @brief Set the camera projection type
      */
    void set_orthographic(bool is_orthographic);

    /**
      * @brief Draw the view manipulator
      */
    void view_manipulate(const Domain::SquareMatrix4f& view, const Domain::SquareMatrix4f& projection, float length,
        const ImVec2& position, const ImVec2& size, ImU32 backgroundColor);

    /**
      * @brief Return current azimuth and zenith
      * @note This is valid only while is_animation_running() returns true
      */
    std::pair<float, float> get_azimuth_and_zenith();

} // namespace Slic3r::App::Imgui::NavCube
