#include "Slic3r/Biz/GCodeReader/Utils.hpp"

#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "LocalesUtils.hpp"

#include <boost/algorithm/string/predicate.hpp>

namespace Slic3r::Biz::GCodeReader {

bool contains_reserved_tags(
    const std::string& gcode,
    const std::vector<std::string_view>& reserved_tags,
    unsigned int max_count,
    std::vector<std::string>& found_tag
)
{
    max_count = std::max(max_count, 1U);

    bool ret = false;

    CNumericLocalesSetter locales_setter;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&reserved_tags, &ret, &found_tag, max_count](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (std::string_view s : reserved_tags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag.push_back(comment);
                    if (found_tag.size() == max_count) {
                        parser.quit_parsing();
                        return;
                    }
                }
            }
        }
    });

    return ret;
}

} // namespace Slic3r::Biz::GCodeReader
