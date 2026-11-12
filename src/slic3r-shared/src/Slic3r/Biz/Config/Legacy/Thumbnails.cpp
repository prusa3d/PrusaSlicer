#include "Thumbnails.hpp"

#include <stdlib.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>
#include <string>
#include <cstdint>

#include "Config.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"

using Slic3r::Domain::enum_bitmask;
using Slic3r::Domain::ThumbnailError;
using Slic3r::Domain::ThumbnailErrors;

namespace Slic3rLegacy::GCodeThumbnails {

using namespace std::literals;




std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext /*= "PNG"sv*/)
{
    if (thumbnails_string.empty())
        return {};

    std::istringstream is(thumbnails_string);
    std::string point_str;

    ThumbnailErrors errors;

    // generate thumbnails data to process it

    GCodeThumbnailDefinitionsList thumbnails_list;
    while (std::getline(is, point_str, ',')) {
        Vec2d point(Vec2d::Zero());
        GCodeThumbnailsFormat format;
        std::istringstream iss(point_str);
        std::string coord_str;
        if (std::getline(iss, coord_str, 'x') && !coord_str.empty()) {
            std::istringstream(coord_str) >> point(0);
            if (std::getline(iss, coord_str, '/') && !coord_str.empty()) {
                std::istringstream(coord_str) >> point(1);

                if (0 < point(0) && point(0) < 1000 && 0 < point(1) && point(1) < 1000) {
                    std::string ext_str;
                    std::getline(iss, ext_str, '/');

                    if (ext_str.empty())
                        ext_str = def_ext.empty() ? "PNG"sv : def_ext;

                    // check validity of extention
                    boost::to_upper(ext_str);
                    if (!ConfigOptionEnum<GCodeThumbnailsFormat>::from_string(ext_str, format)) {
                        format = GCodeThumbnailsFormat::PNG;
                        errors = enum_bitmask(errors | ThumbnailError::InvalidExt);
                    }

                    thumbnails_list.emplace_back(std::make_pair(format, point));
                }
                else
                    errors = enum_bitmask(errors | ThumbnailError::OutOfRange);
                continue;
            }
        }
        errors = enum_bitmask(errors | ThumbnailError::InvalidVal);
    }

    return std::make_pair(std::move(thumbnails_list), errors);
}

std::string get_error_string(const ThumbnailErrors& errors)
{
    std::string error_str;

    if (errors.has(ThumbnailError::InvalidVal))
        error_str += "\n - " + (boost::format("Invalid input format. Expected vector of dimensions in the following format: \"%1%\"") % "XxY/EXT, XxY/EXT, ...").str();
    if (errors.has(ThumbnailError::OutOfRange))
        error_str += "\n - Input value is out of range";
    if (errors.has(ThumbnailError::InvalidExt))
        error_str += "\n - Some extension in the input is invalid";

    return error_str;
}

} // namespace Slic3r::GCodeThumbnails
