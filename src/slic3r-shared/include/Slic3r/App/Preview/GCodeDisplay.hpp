#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::libvgcode {
class FdmViewer;
} // namespace Slic3r::App::libvgcode

namespace Slic3r::App::Preview {

class GCodeWindowData;

/** @brief ImGui widget to show the gcode lines as list of strings.
 *
 * @param viewer data The data to show
 * @param data The data to show
 * @param clip_text Whether or not to clip the text
 */
class GCodeDisplay : public Yoga::Item {
public:
    GCodeDisplay(libvgcode::FdmViewer* viewer, GCodeWindowData* data);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void set_clip_text(bool clip_text);

private:
    libvgcode::FdmViewer* m_viewer{ nullptr };
    GCodeWindowData* m_data{ nullptr };
    bool m_clip_text{ false };
};

} // namespace Slic3r::App::Preview
