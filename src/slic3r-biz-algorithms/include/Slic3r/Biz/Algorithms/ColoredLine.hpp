#pragma once

#include "Slic3r/Domain/Line.hpp"
#include "boost/polygon/segment_concept.hpp"

namespace Slic3r::Biz::Algorithms {
struct ColoredLine
{
    Domain::Line line;
    int  color;
    int  poly_idx       = -1;
    int  local_line_idx = -1;
};

using ColoredLines = std::vector<ColoredLine>;

}

namespace boost::polygon {
template<> struct geometry_concept<Slic3r::Biz::Algorithms::ColoredLine>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::Biz::Algorithms::ColoredLine>
{
    typedef Slic3r::Domain::coord_t       coordinate_type;
    typedef Slic3r::Domain::Point point_type;

    static inline point_type get(const Slic3r::Biz::Algorithms::ColoredLine &line, const direction_1d &dir)
    {
        return dir.to_int() ? line.line.b : line.line.a;
    }
};
} // namespace boost::polygon
