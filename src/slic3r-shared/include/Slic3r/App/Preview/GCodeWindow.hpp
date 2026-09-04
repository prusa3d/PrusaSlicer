#pragma once

#include <Slic3r/Biz/libpgcode/LineView.hpp>
#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"

#include <string_view>
#include <optional>
#include <cstdint>
#include <memory>
namespace Slic3r::App::libvgcode {
class FdmViewer;
} // namespace Slic3r::App::libvgcode

namespace Slic3r::App::Preview {
class GCodeDisplay;

class GCodeWindowData
{
public:
    struct Line
    {
        std::string_view command;
        std::string_view parameters;
        std::string_view comment;

        bool empty() const { return command.empty() && parameters.empty() && comment.empty(); }
    };

    struct Range
    {
        std::optional<uint32_t> min;
        std::optional<uint32_t> max;
        bool empty() const { return !min.has_value() || !max.has_value(); }
        bool contains(const Range& other) const {
            return !this->empty() && !other.empty() && *this->min <= *other.min && *this->max >= *other.max;
        }
        uint32_t size() const { return empty() ? 0 : *this->max - *this->min + 1; }
        void reset() { min = std::nullopt; max = std::nullopt; }
    };


    void set_gcode(std::shared_ptr<const Biz::libpgcode::LineView>& gcode) { m_gcode = gcode; }
    bool has_data() const { return (m_gcode && !m_gcode->empty()); }

    void reset() { m_gcode = nullptr; }

    Line line_at(uint32_t id) const;

    void resize_range(Range& range, uint32_t lines_count, uint32_t curr_line_id) const;

private:
    std::shared_ptr<const Biz::libpgcode::LineView> m_gcode;
};

class GCodeWindow : public Yoga::CollapsibleWindow {
public:
    GCodeWindow(libvgcode::FdmViewer* viewer, GCodeWindowData* data);

    void set_clip_text(bool clip_text);
private:
    GCodeDisplay* m_gcode{ nullptr };
};

} // namespace Slic3r::App::Preview
