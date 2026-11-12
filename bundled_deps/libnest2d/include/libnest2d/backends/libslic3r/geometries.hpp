#ifndef CLIPPER_BACKEND_HPP
#define CLIPPER_BACKEND_HPP

#include <sstream>
#include <unordered_map>
#include <cassert>
#include <vector>
#include <iostream>

#include <libnest2d/geometry_traits.hpp>
#include <libnest2d/geometry_traits_nfp.hpp>

#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"

namespace Slic3r {

template<class T, class En = void> struct IsVec_ : public std::false_type {};

template<class T> struct IsVec_< Domain::Advanced::Vec<T, 2> >: public std::true_type {};

template<class T>
static constexpr const bool IsVec = IsVec_<libnest2d::remove_cvref_t<T>>::value;

template<class T, class O> using VecOnly = std::enable_if_t<IsVec<T>, O>;

inline Domain::Point operator+(const Domain::Point& p1, const Domain::Point& p2) {
    Domain::Point ret = p1;
    ret += p2;
    return ret;
}

inline Domain::Point operator -(const Domain::Point& p ) {
    Domain::Point ret = p;
    ret.x() = -ret.x();
    ret.y() = -ret.y();
    return ret;
}

inline Domain::Point operator-(const Domain::Point& p1, const Domain::Point& p2) {
    Domain::Point ret = p1;
    ret -= p2;
    return ret;
}

inline Domain::Point& operator *=(Domain::Point& p, const Domain::Point& pa ) {
    p.x() *= pa.x();
    p.y() *= pa.y();
    return p;
}

inline Domain::Point operator*(const Domain::Point& p1, const Domain::Point& p2) {
    Domain::Point ret = p1;
    ret *= p2;
    return ret;
}

} // namespace Slic3r

namespace libnest2d {

template<class T> using Vec = Slic3r::Domain::Advanced::Vec<T, 2>;

// Aliases for convinience
using PointImpl = Slic3r::Domain::Point;
using PathImpl  = Slic3r::Domain::Polygon;
using HoleStore = Slic3r::Domain::Polygons;
using PolygonImpl = Slic3r::Domain::ExPolygon;

template<> struct ShapeTag<Slic3r::Domain::Vec2crd> { using Type = PointTag; };

template<> struct ShapeTag<std::vector<Slic3r::Domain::Vec2crd>> { using Type = PathTag; };
template<> struct ShapeTag<Slic3r::Domain::Polygon> { using Type = PathTag; };
template<> struct ShapeTag<Slic3r::Domain::Points>  { using Type = PathTag; };
template<> struct ShapeTag<Slic3r::Domain::ExPolygon> { using Type = PolygonTag; };
template<> struct ShapeTag<Slic3r::Domain::ExPolygons> { using Type = MultiPolygonTag; };

// Type of coordinate units used by Clipper. Enough to specialize for point,
// the rest of the types will work (Path, Polygon)
template<> struct CoordType<Slic3r::Domain::Point> {
    using Type = Slic3r::Domain::coord_t;
    static const constexpr Slic3r::Domain::coord_t MM_IN_COORDS = 1000000;
};

// Enough to specialize for path, it will work for multishape and Polygon
template<> struct PointType<std::vector<Slic3r::Domain::Vec2crd>> { using Type = Slic3r::Domain::Vec2crd; };
template<> struct PointType<Slic3r::Domain::Polygon> { using Type = Slic3r::Domain::Point; };
template<> struct PointType<Slic3r::Domain::Points> { using Type = Slic3r::Domain::Point; };

// This is crucial. CountourType refers to itself by default, so we don't have
// to secialize for clipper Path. ContourType<PathImpl>::Type is PathImpl.
template<> struct ContourType<Slic3r::Domain::ExPolygon> { using Type = Slic3r::Domain::Polygon; };

// The holes are contained in Clipper::Paths
template<> struct HolesContainer<Slic3r::Domain::ExPolygon> { using Type = Slic3r::Domain::Polygons; };

template<>
struct OrientationType<Slic3r::Domain::Polygon> {
    static const constexpr Orientation Value = Orientation::COUNTER_CLOCKWISE;
};

template<>
struct OrientationType<Slic3r::Domain::Points> {
    static const constexpr Orientation Value = Orientation::COUNTER_CLOCKWISE;
};

template<>
struct ClosureType<Slic3r::Domain::Polygon> {
    static const constexpr Closure Value = Closure::OPEN;
};

template<>
struct ClosureType<Slic3r::Domain::Points> {
    static const constexpr Closure Value = Closure::OPEN;
};

template<> struct MultiShape<Slic3r::Domain::ExPolygon> { using Type = Slic3r::Domain::ExPolygons; };
template<> struct ContourType<Slic3r::Domain::ExPolygons> { using Type = Slic3r::Domain::Polygon; };

// Using the libnest2d default area implementation
#define DISABLE_BOOST_AREA

namespace shapelike {

template<>
inline void offset(Slic3r::Domain::ExPolygon& sh, Slic3r::Domain::coord_t distance, const PolygonTag&)
{
#define DISABLE_BOOST_OFFSET
    auto res = Slic3r::Biz::Algorithms::ClipperUtils::offset_ex(sh, distance, ClipperLib::jtSquare);
    if (!res.empty()) sh = res.front();
}

template<>
inline void offset(Slic3r::Domain::Polygon& sh, Slic3r::Domain::coord_t distance, const PathTag&)
{
    auto res = Slic3r::Biz::Algorithms::ClipperUtils::offset(sh, distance, ClipperLib::jtSquare);
    if (!res.empty()) sh = res.front();
}

// Tell libnest2d how to make string out of a ClipperPolygon object
template<> inline std::string toString(const Slic3r::Domain::ExPolygon& sh)
{
    std::stringstream ss;

    ss << "Contour {\n";
    for(auto &p : sh.contour.points) {
        ss << "\t" << p.x() << " " << p.y() << "\n";
    }
    ss << "}\n";

    for(auto& h : sh.holes) {
        ss << "Holes {\n";
        for(auto p : h.points)  {
            ss << "\t{\n";
            ss << "\t\t" << p.x() << " " << p.y() << "\n";
            ss << "\t}\n";
        }
        ss << "}\n";
    }

    return ss.str();
}

template<>
inline Slic3r::Domain::ExPolygon create(const Slic3r::Domain::Polygon& path, const Slic3r::Domain::Polygons& holes)
{
    Slic3r::Domain::ExPolygon p;
    p.contour = path;
    p.holes = holes;

    return p;
}

template<> inline Slic3r::Domain::ExPolygon create(Slic3r::Domain::Polygon&& path, Slic3r::Domain::Polygons&& holes) {
    Slic3r::Domain::ExPolygon p;
    p.contour.points.swap(path.points);
    p.holes.swap(holes);

    return p;
}

template<>
inline const THolesContainer<PolygonImpl>& holes(const Slic3r::Domain::ExPolygon& sh)
{
    return sh.holes;
}

template<> inline THolesContainer<PolygonImpl>& holes(Slic3r::Domain::ExPolygon& sh)
{
    return sh.holes;
}

template<>
inline Slic3r::Domain::Polygon& hole(Slic3r::Domain::ExPolygon& sh, unsigned long idx)
{
    return sh.holes[idx];
}

template<>
inline const Slic3r::Domain::Polygon& hole(const Slic3r::Domain::ExPolygon& sh, unsigned long idx)
{
    return sh.holes[idx];
}

template<> inline size_t holeCount(const Slic3r::Domain::ExPolygon& sh)
{
    return sh.holes.size();
}

template<> inline Slic3r::Domain::Polygon& contour(Slic3r::Domain::ExPolygon& sh)
{
    return sh.contour;
}

template<>
inline const Slic3r::Domain::Polygon& contour(const Slic3r::Domain::ExPolygon& sh)
{
    return sh.contour;
}

template<>
inline void reserve(Slic3r::Domain::Polygon& p, size_t vertex_capacity, const PathTag&)
{
    p.points.reserve(vertex_capacity);
}

template<>
inline void addVertex(Slic3r::Domain::Polygon& sh, const PathTag&, const Slic3r::Domain::Point &p)
{
    sh.points.emplace_back(p);
}

#define DISABLE_BOOST_TRANSLATE
template<>
inline void translate(Slic3r::Domain::ExPolygon& sh, const Slic3r::Domain::Point& offs)
{
    sh.translate(offs);
}

template<>
inline void translate(Slic3r::Domain::Polygon& sh, const Slic3r::Domain::Point& offs)
{
    sh.translate(offs);
}

#define DISABLE_BOOST_ROTATE
template<>
inline void rotate(Slic3r::Domain::ExPolygon& sh, const Radians& rads)
{
    sh.rotate(rads);
}

template<>
inline void rotate(Slic3r::Domain::Polygon& sh, const Radians& rads)
{
    sh.rotate(rads);
}

} // namespace shapelike

namespace nfp {

#define DISABLE_BOOST_NFP_MERGE
template<>
inline TMultiShape<PolygonImpl> merge(const TMultiShape<PolygonImpl>& shapes)
{
    return Slic3r::Biz::Algorithms::ClipperUtils::union_ex(shapes);
}

} // namespace nfp
} // namespace libnest2d

#define DISABLE_BOOST_CONVEX_HULL

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
// All other operators and algorithms are implemented with boost
#include <libnest2d/utils/boost_alg.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // CLIPPER_BACKEND_HPP
