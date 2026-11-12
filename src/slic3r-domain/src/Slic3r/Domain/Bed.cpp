#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include <cfloat>

namespace Slic3r::Domain {

bool operator==(const BedSegments& a, const BedSegments& b)
{
    return a.x_count == b.x_count && a.y_count == b.y_count;
}

static bool check_texture(const std::string& filename)
{
    boost::system::error_code ec; // so the exists call does not throw (e.g. after a permission problem)
    return !filename.empty()
        && (boost::algorithm::iends_with(filename, ".png") || boost::algorithm::iends_with(filename, ".svg"))
        && boost::filesystem::exists(filename, ec);
}

static bool check_model(const std::string& filename)
{
    boost::system::error_code ec;
    return !filename.empty() && boost::algorithm::iends_with(filename, ".stl") && boost::filesystem::exists(filename, ec);
}

Bed Bed::create(const BedCreationData& data)
{
    Bed ret;
    ret.m_type                    = data.type;
    ret.m_contour                 = data.contour;
    ret.m_contour_mesh            = data.contour_mesh;
    ret.m_max_print_height        = data.max_print_height;
    ret.m_segments                = data.segments;
    ret.m_auxiliary_travel_anchor = data.auxiliary_travel_anchor;
    ret.m_model_filename          = data.model_filename;
    ret.m_texture_filename        = data.texture_filename;

    Vec2d min = {DBL_MAX, DBL_MAX};
    Vec2d max = {-DBL_MAX, -DBL_MAX};

    for (const Vec2d& v : ret.m_contour) {
        min.x() = std::min(v.x(), min.x());
        min.y() = std::min(v.y(), min.y());
        max.x() = std::max(v.x(), max.x());
        max.y() = std::max(v.y(), max.y());
    }

    ret.m_contour_aabb = BoundingBoxf{ min, max };
    ret.m_center       = 0.5 * (min + max);
    return ret;
}

static bool vec2d_equal(const Vec2d& a, const Vec2d& b)
{
    return Domain::fuzzy_compare(a.x(), b.x()) && Domain::fuzzy_compare(a.y(), b.y());
}

bool Bed::operator==(const Bed& rhs) const
{
    if (m_type != rhs.m_type) {
        return false;
    }
    if (!vec2d_equal(m_center, rhs.m_center)) {
        return false;
    }
    if (!Domain::fuzzy_compare(m_max_print_height, rhs.m_max_print_height)) {
        return false;
    }
    if (m_model_filename != rhs.m_model_filename) {
        return false;
    }
    if (m_texture_filename != rhs.m_texture_filename) {
        return false;
    }
    if (m_segments != rhs.m_segments) {
        return false;
    }
    if (m_contour.size() != rhs.m_contour.size()) {
        return false;
    }
    for (size_t i = 0; i < m_contour.size(); ++i) {
        if (!vec2d_equal(m_contour[i], rhs.m_contour[i])) {
            return false;
        }
    }
    return true;
}

bool Bed::matches(const Bed& rhs) const
{
    if (m_type != rhs.m_type)
        return false;

    if (m_contour.size() != rhs.m_contour.size())
        return false;

    for (size_t i = 0; i < m_contour.size(); ++i) {
        if (!vec2d_equal(m_contour[i], rhs.m_contour[i]))
            return false;
    }

    if (!Domain::fuzzy_compare(m_max_print_height, rhs.m_max_print_height))
        return false;

    if (m_segments != rhs.m_segments)
        return false;

    if (m_auxiliary_travel_anchor != rhs.m_auxiliary_travel_anchor)
        return false;

    if (m_model_filename != rhs.m_model_filename)
        return false;

    if (m_texture_filename != rhs.m_texture_filename)
        return false;

    return true;
}

} // namespace Slic3r::Domain
