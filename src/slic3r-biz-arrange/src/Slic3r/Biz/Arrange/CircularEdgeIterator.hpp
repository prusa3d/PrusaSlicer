#ifndef CIRCULAR_EDGEITERATOR_HPP
#define CIRCULAR_EDGEITERATOR_HPP

#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Utils.hpp"

namespace Slic3r::Biz::Arrange {

// Circular iterator over a polygon yielding individual edges as Line objects
// if flip_lines is true, the orientation of each line is flipped (not the
// direction of traversal)
template <bool flip_lines = false>
class CircularEdgeIterator_
{
    const Domain::Polygon* m_poly = nullptr;
    size_t m_i                    = 0;
    size_t m_c                    = 0; // counting how many times the iterator has circled over

public:
    // i: vertex position of first line's starting vertex
    // poly: target polygon
    CircularEdgeIterator_(size_t i, const Domain::Polygon& poly) :
        m_poly{&poly},
        m_i{!poly.empty() ? i % poly.size() : 0},
        m_c{!poly.empty() ? i / poly.size() : 0}
    {}

    explicit CircularEdgeIterator_(const Domain::Polygon& poly) : CircularEdgeIterator_(0, poly) {}

    using iterator_category = std::forward_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = Domain::Line;
    using pointer           = Domain::Line*;
    using reference         = Domain::Line&;

    CircularEdgeIterator_& operator++()
    {
        assert(m_poly);
        ++m_i;
        if (m_i == m_poly->size()) { // faster than modulo (?)
            m_i = 0;
            ++m_c;
        }

        return *this;
    }

    CircularEdgeIterator_ operator++(int)
    {
        auto cpy = *this;
        ++(*this);
        return cpy;
    }

    Domain::Line operator*() const
    {
        size_t nx = m_i == m_poly->size() - 1 ? 0 : m_i + 1;
        Domain::Line ret;
        if constexpr (flip_lines)
            ret = Domain::Line((*m_poly)[nx], (*m_poly)[m_i]);
        else
            ret = Domain::Line((*m_poly)[m_i], (*m_poly)[nx]);

        return ret;
    }

    Domain::Line operator->() const
    {
        return *(*this);
    }

    bool operator==(const CircularEdgeIterator_& other) const
    {
        return m_i == other.m_i && m_c == other.m_c;
    }

    bool operator!=(const CircularEdgeIterator_& other) const
    {
        return !(*this == other);
    }

    CircularEdgeIterator_& operator+=(size_t dist)
    {
        m_i = (m_i + dist) % m_poly->size();
        m_c = (m_i + (m_c * m_poly->size()) + dist) / m_poly->size();

        return *this;
    }

    CircularEdgeIterator_ operator+(size_t dist)
    {
        auto cpy = *this;
        cpy += dist;

        return cpy;
    }
};

using CircularEdgeIterator        = CircularEdgeIterator_<>;
using CircularReverseEdgeIterator = CircularEdgeIterator_<true>;

inline Range<CircularEdgeIterator> line_range(const Domain::Polygon& poly)
{
    return Range{CircularEdgeIterator{0, poly}, CircularEdgeIterator{poly.size(), poly}};
}

inline Range<CircularReverseEdgeIterator> line_range_flp(const Domain::Polygon& poly)
{
    return Range{CircularReverseEdgeIterator{0, poly}, CircularReverseEdgeIterator{poly.size(), poly}};
}

} // namespace Slic3r::Biz::Arrange

#endif // CIRCULAR_EDGEITERATOR_HPP
