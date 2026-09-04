#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <regex>
#include <vector>

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Model.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace {
struct Extrusion {
    Domain::Vec4d start;
    Domain::Vec4d end;
};

std::vector<Extrusion> get_first_layer_extrusions(std::istream& gcode) {
    Domain::Vec4d previous_point{Domain::Vec4d::Zero()};

    std::vector<Extrusion> result;

    std::string line;
    while (std::getline(gcode, line)) {
        if (!line.starts_with("G1")) {
            continue;
        }
        const std::regex pattern(R"(\b([XYZE])([-+]?\d*\.?\d+))");

        const auto begin{std::sregex_iterator(line.begin(), line.end(), pattern)};
        const auto end{std::sregex_iterator()};
        if (begin == end) {
            continue;
        }

        Domain::Vec4d current_point{previous_point};
        for (auto it{begin}; it != end; ++it) {
            std::string value{(*it)[2]};
            if ((*it)[1] == "X") {
                current_point[0] = boost::lexical_cast<double>(value);
            }
            if ((*it)[1] == "Y") {
                current_point[1] = boost::lexical_cast<double>(value);
            }
            if ((*it)[1] == "Z") {
                current_point[2] = boost::lexical_cast<double>(value);
            }
            if ((*it)[1] == "E") {
                current_point[3] = boost::lexical_cast<double>(value);
            }
        }

        if (current_point.z() > previous_point.z() && previous_point.z() != 0) {
            return result;
        }

        const auto epsilon{std::numeric_limits<double>::epsilon()};
        if (
            (current_point.head<3>() - previous_point.head<3>()).norm() > epsilon
            && current_point[3] - previous_point[3] > 0
        ) {
            result.push_back({previous_point, current_point});
        }
        previous_point = current_point;
    }
    throw std::runtime_error{"Did not find the end of the first layer!"};
}

bool is_within(const Extrusion &extrusion, const Domain::BoundingBox2d &bounding_box) {
    if (!bounding_box.contains(extrusion.start.head<2>())) {
        return false;
    }
    if (!bounding_box.contains(extrusion.end.head<2>())) {
        return false;
    }
    return true;
}

std::optional<std::string> are_extrusions_within_bounds(
    const std::vector<Extrusion>& extrusions,
    const std::vector<Domain::BoundingBox2d>& bounding_boxes
) {
    if (bounding_boxes.empty()) {
        return "No bounding boxes were provided. There might be a problem with the model.";
    }
    if (extrusions.empty()) {
        return "There are no extrusions in the gcode!";
    }
    std::vector<bool> has_extrusions(bounding_boxes.size(), false);

    for (const Extrusion &extrusion : extrusions) {
        bool is_within_one{false};
        for (std::size_t i{}; i < bounding_boxes.size(); ++i) {
            const Domain::BoundingBox2d& bounding_box{bounding_boxes[i]};
            if (is_within(extrusion, bounding_box)) {
                is_within_one = true;
                has_extrusions[i] = true;
                break;
            }
        }
        if (!is_within_one) {
            return "There is an extrusion outside all objects bounding boxes.";
        }
    }

    if (std::ranges::any_of(has_extrusions, [](const bool has){return !has;})) {
        return "Ther is an object with no extrusions within its bounding box.";
    }

    return std::nullopt;
}

std::optional<float> parse_float_option_from_gcode(
    const std::string& key,
    const std::string& gcode
) {
    std::regex re("; " + key + R"(.*= (\d+(\.\d+)?))");
    std::smatch match;
    if (!std::regex_search(gcode, match, re)) {
        return std::nullopt;
    }
    return boost::lexical_cast<float>(match[1].str());
}

std::optional<std::vector<float>> parse_vector_option_from_gcode(
    const std::string& key,
    const std::string& gcode
) {
    std::regex re("; " + key + R"(.*= ([0-9.,\s]+))");
    std::smatch match;
    if (!std::regex_search(gcode, match, re)) {
        return std::nullopt;
    }

    std::vector<float> result;
    std::stringstream ss(match[1].str());
    std::string token;
    while (std::getline(ss, token, ',')) {
        boost::algorithm::trim(token);
        result.push_back(boost::lexical_cast<float>(token));
    }

    return result;
}

std::optional<int>
parse_printing_time_seconds_from_gcode(const std::string& key, const std::string& gcode)
{
    std::regex re("; " + key + R"(.*=\s*(?:(\d+)h)?\s*(?:(\d+)m)?\s*(?:(\d+)s)?)");
    std::smatch match;

    if (!std::regex_search(gcode, match, re))
        return std::nullopt;

    const auto to_int{[](const std::ssub_match& m)
                      { return m.matched ? boost::lexical_cast<int>(m.str()) : 0; }};

    int hours{to_int(match[1])};
    int minutes{to_int(match[2])};
    int seconds{to_int(match[3])};

    return hours * 3600 + minutes * 60 + seconds;
}

}

namespace Slic3r::Test {

std::optional<std::string> are_statistics_sane(const std::string& gcode)
{
    const auto filament_used_wipe_tower{
        parse_float_option_from_gcode("total filament used for wipe tower", gcode)
    };
    if (!filament_used_wipe_tower) {
        return "Unable to parse wipe tower filament used";
    }

    const bool multiple_extruders{filament_used_wipe_tower > 0};

    for (const std::string& key :
         {"filament used \\[mm\\]",
          "filament used \\[cm3\\]",
          "filament used \\[g\\]",
          "filament cost"})
    {
        const auto stats{parse_vector_option_from_gcode(key, gcode)};
        if (!stats) {
            return "Unable to parse: " + key;
        }
        int nonzero_count{};
        for (float value : *stats) {
            if (value > 0) {
                nonzero_count++;
            }
        }
        if (multiple_extruders) {
            if (nonzero_count <= 1) {
                return "There are too many zeros in: " + key;
            }
        } else {
            if (nonzero_count < 1) {
                return "There must be at least one non zero stat: " + key;
            }
        }
    }

    for (const std::string& key : {"total filament used \\[g\\]", "total filament cost"}) {
        const auto total_filament{
            parse_float_option_from_gcode(key, gcode)
        };
        if (!total_filament) {
            return "Unable to parse " + key;
        }
        if (total_filament <= 0) {
            return key + " is 0 or less";
        }
    }

    if (multiple_extruders) {
        const auto total_toolchanges{parse_float_option_from_gcode("total toolchanges", gcode)};
        if (!total_toolchanges) {
            return "Unable to parse total toolchanges";
        }
        if (total_toolchanges <= 0) {
            return "total toolchanges is 0 or less";
        }
    }

    for (const std::string& key : {"estimated printing time \\(normal mode\\)", "estimated first layer printing time \\(normal mode\\)"}) {
        const auto estimated_printing_time{
            parse_printing_time_seconds_from_gcode(key, gcode)
        };
        if (!estimated_printing_time) {
            return "Unable to parse " + key;
        }
        if (estimated_printing_time <= 0) {
            return key + " is 0 or less";
        }
    }

    return std::nullopt;
}

std::optional<std::string> is_gcode_sane(const std::string& gcode, const Domain::Model &model) {
    if (gcode.empty()) {
        return "GCode is empty!";
    }

    std::stringstream buffer{gcode};
    const std::vector<Extrusion> extrusions{get_first_layer_extrusions(buffer)};
    std::vector<Domain::BoundingBox2d> bounding_boxes;
    for (const Domain::ModelObject* object : model.objects) {
        bounding_boxes.push_back(Domain::BoundingBox2d{
            Algorithms::ModelObject::bounding_box_exact(*object).min.head<2>(),
            Algorithms::ModelObject::bounding_box_exact(*object).max.head<2>()
        });
    }

    if (auto error{are_statistics_sane(gcode)}) {
        return error;
    }
    return are_extrusions_within_bounds(extrusions, bounding_boxes);
}

}
