#ifndef libslic3r_SeamShells_hpp_
#define libslic3r_SeamShells_hpp_

#include <tcbspan/span.hpp>
#include <vector>
#include <cstddef>

#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"

namespace Slic3r {
class Layer;
}

namespace Slic3r::Seams::Shells {
template<typename T = Perimeters::Perimeter> struct Slice
{
    T boundary;
    std::size_t layer_index;
};

template<typename T = Perimeters::Perimeter> using Shell = std::vector<Slice<T>>;

template<typename T = Perimeters::Perimeter> using Shells = std::vector<Shell<T>>;

inline std::size_t get_layer_count(
    const Shells<> &shells
) {
    std::size_t layer_count{0};
    for (const Shell<> &shell : shells) {
        for (const Slice<>& slice : shell) {
            if (slice.layer_index >= layer_count) {
                layer_count = slice.layer_index + 1;
            }
        }
    }
    return layer_count;
}

Shells<> create_shells(
    Perimeters::LayerPerimeters &&perimeters, const double max_distance
);
} // namespace Slic3r::Seams::Shells

#endif // libslic3r_SeamShells_hpp_
