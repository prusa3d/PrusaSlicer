#ifndef POINTGRID_HPP
#define POINTGRID_HPP

#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include <libslic3r/Point.hpp>

namespace Slic3r {

template<class T>
class PointGrid {
    std::array<int, 3> m_size;
    std::vector<LegacyVec<3, T>> m_data;
    const int XY;

public:
    explicit PointGrid(std::vector<LegacyVec<3, T>> data, const std::array<int, 3> &size)
        : m_size{size}, m_data(std::move(data)), XY{m_size[0] * m_size[1]}
    {}

    const LegacyVec<3, T> & get(size_t idx) const { return m_data[idx]; }
    const LegacyVec<3, T> & get(const Domain::Index3 &coord) const
    {
        return m_data[get_idx(coord)];
    }

    size_t get_idx(const Domain::Index3 &coord) const
    {
        size_t ret = coord[2] * XY + coord[1] * m_size[0] + coord[0];

        return ret;
    }

    Domain::Index3 get_coord(size_t idx) const {
        int iz = idx / XY;
        int iy = (idx / m_size[0]) % m_size[1];
        int ix = idx % m_size[0];

        return {ix, iy, iz};
    }

    const std::vector<LegacyVec<3, T>> & data() const { return m_data; }
    size_t point_count() const { return m_data.size(); }
    bool empty() const { return m_data.empty(); }
};

template<class Ex, class CoordT>
PointGrid<CoordT> point_grid(
    Ex policy,
    const Domain::BoundingBox<CoordT, 3>& bounds,
    const LegacyVec<3, CoordT>& stride
)
{
    namespace execution = Slic3r::Biz::Algorithms::Execution;

    std::array<int, 3> numpts = {0, 0, 0};

    for (int n = 0; n < 3; ++n)
        numpts[n] = (bounds.max(n) - bounds.min(n)) / stride(n);

    std::vector<LegacyVec<3, CoordT>> out(numpts[0] * numpts[1] * numpts[2]);

    size_t XY = numpts[X] * numpts[Y];

    execution::for_each(policy, size_t(0), out.size(), [&](size_t i) {
        int iz = i / XY;
        int iy = (i / numpts[X]) % numpts[Y];
        int ix = i % numpts[X];

        out[i] = LegacyVec<3, CoordT>(ix * stride.x(), iy * stride.y(), iz * stride.z());
    });

    return PointGrid{std::move(out), numpts};
}

} // namespace Slic3r

#endif // POINTGRID_HPP
