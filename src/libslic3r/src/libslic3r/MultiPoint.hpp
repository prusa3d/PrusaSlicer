#ifndef slic3r_MultiPoint_hpp_
#define slic3r_MultiPoint_hpp_

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include <Eigen/Geometry>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstddef>

#include "Slic3r/Domain/MultiPoint.hpp"
#include "Slic3r/Biz/Algorithms/MultiPoint.hpp"
#include "libslic3r.h"
#include "Line.hpp"
#include "Point.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

// Reduces polyline in the <begin, end) range, outputs into the output iterator.
// Output iterator may be equal to input iterator as long as the iterator value type move operator supports move at the same input / output address.
template<typename SquareLengthType, typename InputIterator, typename OutputIterator, typename TakeFloaterPredicate, typename PointGetter>
inline OutputIterator douglas_peucker(InputIterator begin, InputIterator end, OutputIterator out, TakeFloaterPredicate take_floater_predicate, PointGetter point_getter)
{
    using InputIteratorCategory = typename std::iterator_traits<InputIterator>::iterator_category;
    static_assert(std::is_base_of_v<std::input_iterator_tag, InputIteratorCategory>);
    using Vector = Eigen::Matrix<SquareLengthType, 2, 1, Eigen::DontAlign>;
    if (begin != end) {
        // Supporting in-place reduction and the data type may be generic, thus we are always making a copy of the point value before there is a chance
        // to override input by moving the data to the output.
        auto a = point_getter(*begin);
        *out ++ = std::move(*begin);
        if (auto next = std::next(begin); next == end) {
            // Single point input only.
        } else if (std::next(next) == end) {
            // Two points input.
            *out ++ = std::move(*next);
        } else {
            InputIterator anchor  = begin;
            InputIterator floater = std::prev(end);
            std::vector<InputIterator> dpStack;
            if constexpr (std::is_base_of_v<std::random_access_iterator_tag, InputIteratorCategory>)
                dpStack.reserve(end - begin);
            dpStack.emplace_back(floater);
            auto f = point_getter(*floater);
            for (;;) {
                assert(anchor != floater);
                bool            take_floater = false;
                InputIterator   furthest     = anchor;
                if (std::next(anchor) == floater) {
                    // Two point segment. Accept the floater.
                    take_floater = true;
                } else {
                    std::optional<SquareLengthType> max_dist_sq;
                    // Find point furthest from line seg created by (anchor, floater) and note it.
                    const Vector v = (f - a).template cast<SquareLengthType>();
                    if (const SquareLengthType l2 = v.squaredNorm(); l2 == 0) {
                        // Zero length segment, find the furthest point between anchor and floater.
                        for (auto it = std::next(anchor); it != floater; ++ it) {
                            if (SquareLengthType dist_sq = (point_getter(*it) - a).template cast<SquareLengthType>().squaredNorm(); !max_dist_sq.has_value() || dist_sq > max_dist_sq) {
                                max_dist_sq = dist_sq;
                                furthest    = it;
                            }
                        }
                    } else {
                        // Find Find the furthest point from the line <anchor, floater>.
                        const double dl2 = double(l2);
                        const Vec2d  dv  = v.template cast<double>();
                        for (auto it = std::next(anchor); it != floater; ++ it) {
                            const auto   p  = point_getter(*it);
                            const Vector va = (p - a).template cast<SquareLengthType>();
                            const SquareLengthType t = va.dot(v);
                            SquareLengthType dist_sq;
                            if (t <= 0) {
                                dist_sq = va.squaredNorm();
                            } else if (t >= l2) {
                                dist_sq = (p - f).template cast<SquareLengthType>().squaredNorm();
                            } else if (double dt = double(t) / dl2; dt <= 0) {
                                dist_sq = va.squaredNorm();
                            } else if (dt >= 1.) {
                                dist_sq = (p - f).template cast<SquareLengthType>().squaredNorm();
                            } else {
                                const Vector w = (dt * dv).cast<SquareLengthType>();
                                dist_sq = (w - va).squaredNorm();
                            }

                            if (!max_dist_sq.has_value() || dist_sq > max_dist_sq) {
                                max_dist_sq  = dist_sq;
                                furthest     = it;
                            }
                        }                        
                    }

                    assert(max_dist_sq.has_value());

                    // Remove points between the anchor and the floater when the predicate is satisfied.
                    take_floater = take_floater_predicate(anchor, floater, *max_dist_sq);
                }

                if (take_floater) {
                    // The points between anchor and floater are close to the <anchor, floater> line.
                    // Drop the points between them.
                    a = f;
                    *out ++ = std::move(*floater);
                    anchor = floater;
                    assert(dpStack.back() == floater);
                    dpStack.pop_back();
                    if (dpStack.empty())
                        break;

                    floater = dpStack.back();
                    f = point_getter(*floater);
                } else {
                    // The furthest point is too far from the segment <anchor, floater>. 
                    // Divide recursively.
                    floater = furthest;
                    f = point_getter(*floater);
                    dpStack.emplace_back(floater);
                }
            }
        }
    }
    return out;
}

template<typename SquareLengthType, typename InputIterator, typename OutputIterator, typename PointGetter>
inline OutputIterator douglas_peucker(InputIterator begin, InputIterator end, OutputIterator out, const double tolerance, PointGetter point_getter) {
    const auto tolerance_sq = static_cast<SquareLengthType>(sqr(tolerance));

    const auto take_floater_predicate = [&tolerance_sq](InputIterator, InputIterator, const SquareLengthType max_dist_sq) -> bool {
        return max_dist_sq <= tolerance_sq;
    };

    return douglas_peucker<SquareLengthType>(begin, end, out, take_floater_predicate, point_getter);
}

extern BoundingBox get_extents_rotated(const Points &points, double angle);

} // namespace Slic3r

#endif
