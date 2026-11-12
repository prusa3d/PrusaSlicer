#include "Slic3r/Biz/Config/BedShape.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Biz/Algorithms/Bed.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

#include "Slic3r/Assert.hpp"

#include <fmt/format.h>

using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::Biz::Config {

BedShape::BedShape(const Domain::Vec2ds& points)
{
    Domain::BedCreationData data{
        .type         = Bed::detect_bed_type_from_contour(points),
        .contour      = points,
        .contour_mesh = Bed::bed_contour_as_its(points)
    };
    m_bed = Domain::Bed::create(data);
}

bool BedShape::is_custom() const
{
    Domain::BedType bed_type = m_bed.type();
    return bed_type == Domain::BedType::Convex || bed_type == Domain::BedType::Custom;
}

bool BedShape::is_equal_to(const Domain::Vec2ds& points) const
{
    return m_bed.contour() == points;
}

const BedShape::ParamAttributes& BedShape::attributes(Parameter param)
{
    static const std::map<BedShape::Parameter, BedShape::ParamAttributes> param_attributes_mapping = {
        {BedShape::Parameter::RectSize,
         {_u8L("Size"), _u8L("Size in X and Y of the rectangular plate."), 0, 1'200, 200}},
        {BedShape::Parameter::RectOrigin,
         {_u8L("Origin"),
          _u8L(
              "Distance of the 0,0 G-code coordinate from the front left corner of the rectangle."
          ),
          -600,
          600,
          0}},
        {BedShape::Parameter::Diameter,
         {_u8L("Diameter"),
          _u8L(
              "Diameter of the print bed. It is assumed that origin (0,0) is located in the center."
          ),
          0,
          600,
          200}},
    };

    ASSERT(param_attributes_mapping.contains(param));
    return param_attributes_mapping.at(param);
}

static std::string get_param_label(BedShape::Parameter param)
{
    return BedShape::attributes(param).name;
}

std::string BedShape::get_type_name(Type type)
{
    switch (type) {
    case Type::Rectangle:
        return Biz::_u8L("Rectangular");
    case Type::Circle:
        return Biz::_u8L("Circular");
    case Type::Custom:
        return Biz::_u8L("Custom");
    }
    // make visual studio happy
    assert(false);
    return {};
}

BedShape::Type BedShape::get_type() const
{
    Domain::BedType bed_type = m_bed.type();
    switch (bed_type) {
    case Domain::BedType::Rectangle:
    case Domain::BedType::Invalid:
        return Type::Rectangle;
    case Domain::BedType::Circle:
        return Type::Circle;
    case Domain::BedType::Convex:
    case Domain::BedType::Custom:
        return Type::Custom;
    }
    // make visual studio happy
    assert(false);
    return Type::Rectangle;
}

Domain::Vec2ds BedShape::triangles() const
{
    return Bed::bed_contour_as_triangles(m_bed);
}

const Domain::Vec2ds& BedShape::contour() const
{
    return m_bed.contour();
}

Domain::Vec2d BedShape::get_size() const
{
    return m_bed.contour_aabb_extent();
}

Domain::Vec2d BedShape::get_origin() const
{
    return m_bed.contour_aabb().min;
}

double BedShape::get_diameter() const
{
    ASSERT(m_bed.type() == Domain::BedType::Circle);
    return m_bed.contour_aabb_extent().x();
}

std::string BedShape::get_full_name_with_params() const
{
    Domain::BedType bed_type = m_bed.type();
    std::string out          = Biz::_u8L("Shape") + ": " + get_type_name(this->get_type());
    switch (bed_type) {
    case Domain::BedType::Circle: {
        const double diameter = get_diameter();
        out += "\n" + get_param_label(Parameter::Diameter) + fmt::format(": [{:.10g}]", diameter);
    } break;
    default: {
        Domain::Vec2d size   = get_size();
        Domain::Vec2d bb_min = get_origin();
        if (bb_min != Domain::Vec2d::Zero())
            bb_min *= -1.;

        out += "\n"
            + get_param_label(Parameter::RectSize)
            + fmt::format(": [{:.10g}, {:.10g}]\n", size.x(), size.y());
        out += get_param_label(Parameter::RectOrigin)
            + fmt::format(": [{:.10g}, {:.10g}]\n", bb_min.x(), bb_min.y());
    } break;
    }
    return out;
}

} // namespace Slic3r::Biz::Config
