#include "WipeTowerIntegration.hpp"

#include <LocalesUtils.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <cmath>
#include <iomanip>
#include <istream>
#include <iterator>
#include <utility>
#include <cassert>
#include <cstdlib>

#include "libslic3r/GCode.hpp"
#include "libslic3r/libslic3r.h"
#include "boost/algorithm/string/replace.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/GCode/Travels.hpp"
#include "libslic3r/GCode/Wipe.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"

namespace Slic3r::GCode {

static inline Point wipe_tower_point_to_object_point(GCodeGenerator &gcodegen, const Vec2f& wipe_tower_pt)
{
    return scaled(Vec2d(wipe_tower_pt.x() - gcodegen.origin()(0), wipe_tower_pt.y() - gcodegen.origin()(1)));
}

std::string WipeTowerIntegration::append_tcr(
    GCodeGenerator& gcodegen,
    const Domain::ConfigView& config,
    const WipeTower::ToolChangeResult& tcr,
    int new_extruder_id,
    double z
) const
{
    if (new_extruder_id != -1 && new_extruder_id != tcr.new_tool)
        throw Slic3r::InvalidArgument("Error: WipeTowerIntegration::append_tcr was asked to do a toolchange it didn't expect.");

    std::string gcode;


    Vec2f start_pos = tcr.start_pos;
    Vec2f end_pos = tcr.end_pos;
    if (! tcr.priming) {
        start_pos = transform_wt_pt(start_pos);
        end_pos = transform_wt_pt(end_pos);
    }

    Vec2f wipe_tower_offset = tcr.priming ? Vec2f::Zero() : m_wipe_tower_pos;
    float wipe_tower_rotation = tcr.priming ? 0.f : this->get_alpha();

    std::string tcr_rotated_gcode = post_process_wipe_tower_moves(tcr, wipe_tower_offset, wipe_tower_rotation);

    double current_z = gcodegen.writer().get_position().z();
    gcode += gcodegen.writer().travel_to_z(current_z);

    if (z == -1.) // in case no specific z was provided, print at current_z pos
        z = current_z;

    const bool needs_toolchange = gcodegen.writer().need_toolchange(new_extruder_id);
    const bool will_go_down = ! is_approx(z, current_z);
    const bool is_ramming = (config.get<bool>("single_extruder_multi_material"))
                         || (! config.get<bool>("single_extruder_multi_material") && config.get<std::vector<bool>>("filament_multitool_ramming").at(tcr.initial_tool));
    const bool should_travel_to_tower = ! tcr.priming
                                     && (tcr.force_travel        // wipe tower says so
                                         || ! needs_toolchange   // this is just finishing the tower with no toolchange
                                         || is_ramming
                                         || will_go_down);       // don't dig into the print
    if (should_travel_to_tower) {
        const Point xy_point = wipe_tower_point_to_object_point(gcodegen, start_pos);
        const Vec3crd to{to_3d(xy_point, scaled(z))};
        gcode += gcodegen.m_label_objects.maybe_stop_instance();

        const std::vector<double> retract_speed{config.get<std::vector<double>>("retract_speed")};
        const double travel_speed{config.get<double>("travel_speed")};
        const Biz::Slicing::ExtrudeConfig extrude_config{config};

        gcode += gcodegen.retract_and_wipe(retract_speed, travel_speed);
        gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
        const std::string comment{"Travel to a Wipe Tower"};
        if (!gcodegen.m_moved_to_first_layer_point) {
            gcode += gcodegen.travel_to_first_position(to, current_z, ExtrusionRole::Mixed, [](){return "";}, extrude_config);
        } else {
            if (gcodegen.last_position) {
                const Vec3crd from{to_3d(*gcodegen.last_position, scaled(current_z))};
                gcode += gcodegen.travel_to(
                    from, to, ExtrusionRole::Mixed, comment, [](){return "";}, extrude_config
                );
            } else {
                gcode += gcodegen.writer().travel_to_xy(gcodegen.point_to_gcode(xy_point), comment);
                gcode += gcodegen.writer().travel_to_z_force(z, comment);
            }
        }
        gcode += gcodegen.unretract();
    } else {
        // When this is multiextruder printer without any ramming, we can just change
        // the tool without travelling to the tower.
    }

    if (will_go_down) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(z, "Travel down to the last wipe tower layer.");
        gcode += gcodegen.writer().unretract();
    }

    // Travels before the tool change belong to the old extruder, so they are expanded before set_extruder().
    const Biz::Slicing::ExtrudeConfig travel_config{config};
    const int tower_extruder{new_extruder_id > -1 ? new_extruder_id : tcr.initial_tool};
    const unsigned int tower_print_acceleration{
        config.get<std::vector<double>>("default_acceleration").at(tower_extruder) > 0 ?
            fast_round_up<unsigned int>(config.get<double>("wipe_tower_acceleration")) :
            0
    };

    // Set before the travels inside the wipe tower, the writer would skip it after they restore it.
    gcode += gcodegen.writer().set_print_acceleration(tower_print_acceleration);

    const size_t toolchange_position{
        tcr_rotated_gcode.find("[toolchange_gcode_from_wipe_tower_generator]")
    };
    Vec2f travel_position{start_pos - m_extruder_offsets[tcr.initial_tool].cast<float>()};
    const std::string gcode_before_toolchange{expand_print_travels(
        gcodegen,
        travel_config,
        tcr_rotated_gcode.substr(0, toolchange_position),
        tower_print_acceleration,
        travel_position,
        z
    )};

    std::string toolchange_gcode_str;
    std::string deretraction_str;
    bool spreads_deretraction{false};

    if (tcr.priming || (new_extruder_id >= 0 && needs_toolchange)) {
        if (is_ramming) {
            gcodegen.m_wipe.reset_path(); // We don't want wiping on the ramming lines.
        }

        // The placeholder is replaced after post_process_wipe_tower_moves(), transform the points here.
        const Vec2f new_extruder_offset{m_extruder_offsets[tcr.new_tool].cast<float>()};
        std::optional<Vec2d> spread_start;
        if (tcr.deretraction_spread.has_value()) {
            spread_start =
                (transform_wt_pt(tcr.deretraction_spread->start) - new_extruder_offset)
                    .cast<double>();
        }

        // The firmware returns the head right to the start of the spread, not to the purge start next to the ramming end.
        // Without the ramming the wipe tower G-code travels to the start of its block after the tool change.
        std::optional<Vec2d> toolchange_return_position;
        if (is_ramming
            && spread_start.has_value()
            && gcodegen.writer().can_unretract_while_moving(tcr.new_tool))
        {
            toolchange_return_position = spread_start;
        }

        toolchange_gcode_str =
            gcodegen.set_extruder(new_extruder_id, tcr.print_z, config, toolchange_return_position); // TODO: toolchange_z vs print_z
        if (config.get<bool>("wipe_tower")) {
            GCodeWriter& writer = gcodegen.writer();

            // The custom tool change G-code may have changed the retraction.
            spreads_deretraction =
                spread_start.has_value() && writer.can_unretract_while_moving(tcr.new_tool);

            if (spreads_deretraction) {
                const Vec2f spread_end =
                    transform_wt_pt(tcr.deretraction_spread->end) - new_extruder_offset;

                // Replaces the travel of the wipe tower to the purge start, still at the height of the tool change
                // unless the firmware returned the head here already.
                deretraction_str += writer.travel_to_xy_force(*spread_start);
                deretraction_str += writer.travel_to_z_force(z, "restore layer Z");

                // Tagged so that the G-code preview shows the move as a de-retraction, not as an extrusion.
                deretraction_str += ";"
                    + std::string{Biz::libpgcode::reserved_tag(
                        Biz::libpgcode::Tags::Moving_Unretract_Start
                    )}
                    + "\n";
                deretraction_str += writer.unretract_while_moving(
                    spread_end.cast<double>(),
                    tcr.deretraction_spread->max_feedrate
                );
                deretraction_str += ";"
                    + std::string{Biz::libpgcode::reserved_tag(
                        Biz::libpgcode::Tags::Moving_Unretract_End
                    )}
                    + "\n";
            } else {
                deretraction_str += writer.travel_to_z_force(z, "restore layer Z");
                deretraction_str += gcodegen.unretract();
            }
        }
    }

    const std::string gcode_after_toolchange{
        toolchange_position == std::string::npos ?
            std::string{} :
            expand_print_travels(
                gcodegen,
                travel_config,
                tcr_rotated_gcode.substr(toolchange_position),
                tower_print_acceleration,
                travel_position,
                z
            )
    };
    tcr_rotated_gcode = gcode_before_toolchange + gcode_after_toolchange;

    assert(toolchange_gcode_str.empty() || toolchange_gcode_str.back() == '\n');
    assert(deretraction_str.empty() || deretraction_str.back() == '\n');

    // The travel of the wipe tower back to the purge start after the tool change, the de-retraction spread replaces it.
    const std::string toolchange_travel_tag{" " + WipeTower::toolchange_travel_tag()};
    if (const size_t tag_position{tcr_rotated_gcode.find(toolchange_travel_tag)};
        tag_position != std::string::npos)
    {
        if (spreads_deretraction) {
            const size_t line_start{tcr_rotated_gcode.rfind('\n', tag_position) + 1};
            const size_t line_end{tcr_rotated_gcode.find('\n', tag_position)};
            tcr_rotated_gcode.erase(
                line_start,
                line_end == std::string::npos ? std::string::npos : line_end + 1 - line_start
            );
        } else {
            tcr_rotated_gcode.erase(tag_position, toolchange_travel_tag.size());
        }
    } else {
        assert(!spreads_deretraction);
    }

    // Insert the toolchange and deretraction gcode into the generated gcode.
    boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_from_wipe_tower_generator]", toolchange_gcode_str);
    boost::replace_first(tcr_rotated_gcode, "[deretraction_from_wipe_tower_generator]", deretraction_str);
    std::string tcr_gcode;
    Biz::Algorithms::unescape_string_cstyle(tcr_rotated_gcode, tcr_gcode);

    const int acceleation_extruder{new_extruder_id > -1 ? new_extruder_id : tcr.initial_tool};
    gcode += tcr_gcode;
    gcode += gcodegen.writer().set_print_acceleration(
        fast_round_up<unsigned int>(
            config.get<std::vector<double>>("default_acceleration").at(acceleation_extruder)
        )
    );

    // A phony move to the end position at the wipe tower.
    gcodegen.writer().travel_to_xy(end_pos.cast<double>());
    gcodegen.last_position = wipe_tower_point_to_object_point(gcodegen, end_pos);
    if (!is_approx(z, current_z)) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(current_z, "Travel back up to the topmost object layer.");
        gcode += gcodegen.writer().unretract();
    }

    else {
        // Prepare a future wipe.
        // Convert to a smooth path.
        Geometry::ArcWelder::Path path;
        path.reserve(tcr.wipe_path.size());
        std::transform(tcr.wipe_path.begin(), tcr.wipe_path.end(), std::back_inserter(path),
            [&gcodegen, this](const Vec2f &wipe_pt) {
                return Geometry::ArcWelder::Segment{ wipe_tower_point_to_object_point(gcodegen, transform_wt_pt(wipe_pt)) };
            });
        // Pass to the wipe cache.
        gcodegen.m_wipe.set_path(std::move(path));
    }

    // Let the planner know we are traveling between objects.
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
    return gcode;
}

// This function postprocesses gcode_original, rotates and moves all G1 extrusions and returns resulting gcode
// Starting position has to be supplied explicitely (otherwise it would fail in case first G1 command only contained one coordinate)
std::string WipeTowerIntegration::post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const
{
    Vec2f extruder_offset = m_extruder_offsets[tcr.initial_tool].cast<float>();

    std::istringstream gcode_str(tcr.gcode);
    std::string gcode_out;
    std::string line;
    Vec2f pos = tcr.start_pos;
    Vec2f transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;
    Vec2f old_pos(-1000.1f, -1000.1f);

    while (gcode_str) {
        std::getline(gcode_str, line);  // we read the gcode line by line

        // All G1 commands should be translated and rotated. X and Y coords are
        // only pushed to the output when they differ from last time.
        // WT generator can override this by appending the never_skip_tag
        if (boost::starts_with(line, "G1 ")) {
            bool never_skip = false;
            auto it = line.find(WipeTower::never_skip_tag());
            if (it != std::string::npos) {
                // remove the tag and remember we saw it
                never_skip = true;
                line.erase(it, it + WipeTower::never_skip_tag().size());
            }
            std::ostringstream line_out;
            std::istringstream line_str(line);
            line_str >> std::noskipws;  // don't skip whitespace
            char ch = 0;
            line_str >> ch >> ch; // read the "G1"
            while (line_str >> ch) {
                if (ch == 'X' || ch == 'Y')
                    line_str >> (ch == 'X' ? pos.x() : pos.y());
                else
                    line_out << ch;
            }

            line = line_out.str();
            boost::trim(line); // Remove leading and trailing spaces.

            transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;

            if (transformed_pos != old_pos || never_skip || ! line.empty()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3) << "G1";
                if (transformed_pos.x() != old_pos.x() || never_skip)
                    oss << " X" << transformed_pos.x() - extruder_offset.x();
                if (transformed_pos.y() != old_pos.y() || never_skip)
                    oss << " Y" << transformed_pos.y() - extruder_offset.y();
                if (! line.empty())
                    oss << " ";
                line = oss.str() + line;
                old_pos = transformed_pos;
            }
        }

        gcode_out += line + "\n";

        // If this was a toolchange command, we should change current extruder offset
        if (line == "[toolchange_gcode_from_wipe_tower_generator]") {
            extruder_offset = m_extruder_offsets[tcr.new_tool].cast<float>();

            // If the extruder offset changed, add an extra move so everything is continuous
            if (extruder_offset != m_extruder_offsets[tcr.initial_tool].cast<float>()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3)
                    << "G1 X" << transformed_pos.x() - extruder_offset.x()
                    << " Y" << transformed_pos.y() - extruder_offset.y()
                    << "\n";
                gcode_out += oss.str();
            }
        }
    }
    return gcode_out;
}

/**
 * @brief Offset of the last G1 line of the wipe tower G-code that moves the extruder, npos if there is none.
 */
static size_t find_last_extruder_move(const std::string& tower_gcode)
{
    size_t last_extruder_move_start{std::string::npos};
    size_t next_line_start{0};
    std::istringstream tower_lines{tower_gcode};
    std::string line;
    while (std::getline(tower_lines, line)) {
        const size_t line_start{next_line_start};
        next_line_start += line.size() + 1;
        if (!boost::starts_with(line, "G1 ")) {
            continue;
        }

        std::istringstream words{line};
        std::string word;
        while (words >> word && word.front() != ';') {
            if (word.front() == 'E') {
                last_extruder_move_start = line_start;
            }
        }
    }

    return last_extruder_move_start;
}

std::string WipeTowerIntegration::expand_print_travels(
    GCodeGenerator& gcodegen,
    const Biz::Slicing::ExtrudeConfig& config,
    const std::string& tower_gcode,
    const unsigned int tower_print_acceleration,
    Vec2f& position,
    const double z
) const
{
    const std::string print_travel_tag{WipeTower::print_travel_tag()};
    const size_t last_extruder_move_start{find_last_extruder_move(tower_gcode)};

    std::string result;
    std::string feedrate_word;
    std::istringstream tower_lines{tower_gcode};
    std::string line;
    size_t next_line_start{0};
    while (std::getline(tower_lines, line)) {
        const size_t line_start{next_line_start};
        const Vec2f travel_start{position};

        next_line_start += line.size() + 1;

        if (boost::starts_with(line, "G1 ")) {
            std::istringstream words{line};
            std::string word;
            while (words >> word && word.front() != ';') {
                if (word.front() == 'X') {
                    position.x() = string_to_float_decimal_point(std::string_view{word}.substr(1));
                } else if (word.front() == 'Y') {
                    position.y() = string_to_float_decimal_point(std::string_view{word}.substr(1));
                } else if (word.front() == 'F') {
                    feedrate_word = word;
                }
            }
        }

        const size_t tag_position{line.find(print_travel_tag)};
        if (tag_position == std::string::npos) {
            result += line + "\n";
            continue;
        }

        line.erase(tag_position);
        boost::trim_right(line);

        GCodeWriter& writer = gcodegen.writer();
        if (writer.extruder() == nullptr
            || (position - travel_start).norm()
                < float(config.retract_before_travel.at(writer.extruder()->id())))
        {
            result += line + "\n";
            continue;
        }

        // The same retraction and lift as GCodeGenerator::travel_to() does for the print.
        const unsigned int extruder_id{writer.extruder()->id()};
        const Polyline xy_path{
            gcodegen.gcode_to_point(travel_start.cast<double>()),
            gcodegen.gcode_to_point(position.cast<double>())
        };

        writer.update_position(Vec3d{travel_start.x(), travel_start.y(), z});
        result += writer.retract();

        Points3 travel{
            config.retract_length.at(extruder_id) == 0. ?
                Impl::Travels::generate_flat_travel(xy_path.points, float(z)) :
                Impl::Travels::generate_travel_to_extrusion(
                    xy_path,
                    config,
                    extruder_id,
                    z,
                    gcodegen.m_travel_obstacle_tracker,
                    scaled(gcodegen.origin())
                )
        };

        travel.emplace_back(to_3d(xy_path.points.back(), scaled(z)));
        result +=
            gcodegen
                .generate_travel_gcode(travel, "wipe tower travel", []() { return ""; }, config);
        result += writer.set_print_acceleration(tower_print_acceleration);

        // De-retract after the travel, unless only the tool change follows and its retraction
        // is longer than the travel retraction. The tool change then retracts just the rest.
        const bool is_extruder_move_following{
            last_extruder_move_start != std::string::npos && last_extruder_move_start > line_start
        };
        if (is_extruder_move_following
            || writer.extruder()->retract_length_toolchange()
                <= writer.extruder()->retract_length())
        {
            result += writer.unretract();
        }

        if (!feedrate_word.empty()) {
            // The wipe tower expects its last feedrate to be still active.
            result += "G1 " + feedrate_word + "\n";
        }
    }

    return result;
}

std::string WipeTowerIntegration::prime(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
{
    std::string gcode;
    for (const WipeTower::ToolChangeResult& tcr : m_priming) {
        if (! tcr.extrusions.empty())
            gcode += append_tcr(gcodegen, config, tcr, tcr.new_tool);
    }
    return gcode;
}

std::string WipeTowerIntegration::tool_change(GCodeGenerator &gcodegen, const Domain::ConfigView& config, int extruder_id, bool finish_layer)
{
    std::string gcode;
    assert(m_layer_idx >= 0);
    if (gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
        if (m_layer_idx < (int)m_tool_changes.size()) {
            if (!(size_t(m_tool_change_idx) < m_tool_changes[m_layer_idx].size()))
                throw Slic3r::RuntimeError("Wipe tower generation failed, possibly due to empty first layer.");

            // Calculate where the wipe tower layer will be printed. -1 means that print z will not change,
            // resulting in a wipe tower with sparse layers.
            double wipe_tower_z = -1;
            bool ignore_sparse = false;
            if (config.get<bool>("wipe_tower_no_sparse_layers")) {
                wipe_tower_z = m_last_wipe_tower_print_z;
                ignore_sparse = (m_tool_changes[m_layer_idx].size() == 1 && m_tool_changes[m_layer_idx].front().initial_tool == m_tool_changes[m_layer_idx].front().new_tool && m_layer_idx != 0);
                if (m_tool_change_idx == 0 && !ignore_sparse)
                    wipe_tower_z = m_last_wipe_tower_print_z + m_tool_changes[m_layer_idx].front().layer_height;
            }

            if (!ignore_sparse) {
                gcode += append_tcr(gcodegen, config, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id, wipe_tower_z);
                m_last_wipe_tower_print_z = wipe_tower_z;
            }
        }
    }
    return gcode;
}

// Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
std::string WipeTowerIntegration::finalize(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
{
    std::string gcode;
    const double purge_z{m_final_purge.print_z + config.get<double>("z_offset")};
    if (std::abs(gcodegen.writer().get_position().z() - purge_z) > EPSILON)
        gcode += gcodegen.generate_travel_gcode(
            {{gcodegen.last_position->x(), gcodegen.last_position->y(), scaled(purge_z)}},
            "move to safe place for purging", [](){return "";}, Biz::Slicing::ExtrudeConfig{config}
        );
    gcode += append_tcr(gcodegen, config, m_final_purge, -1);
    return gcode;
}

} // namespace Slic3r::GCode
