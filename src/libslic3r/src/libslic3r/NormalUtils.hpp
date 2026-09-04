#ifndef slic3r_NormalUtils_hpp_
#define slic3r_NormalUtils_hpp_

#include <vector>

#include "Point.hpp"
#include "Model.hpp"
#include "admesh/stl.h"

namespace Slic3r {

/**
@brief Collection of static function
to create normals
*/
class NormalUtils
{
public:
    using Normal = Vec3f;
    using Normals = std::vector<Normal>;
    NormalUtils() = delete; // only static functions

    enum class VertexNormalType {
        AverageNeighbor,
        AngleWeighted,
        NelsonMaxWeighted
    };

    /**
    @brief Create normal for triangle defined by indices from vertices
    @param indices index into vertices
    @param vertices vector of vertices
    @return normal to triangle(normalized to size 1)
    */
    static Normal create_triangle_normal(
        const stl_triangle_vertex_indices &indices,
        const std::vector<stl_vertex> &    vertices);

    /**
    @brief Create normals for each vertices
    @param its indices and vertices
    @return Vector of normals
    */
    static Normals create_triangle_normals(const indexed_triangle_set &its);

    /**
    @brief Create normals for each vertex by averaging neighbor triangles normal
    @param its Triangle indices and vertices
    @param type Type of calculation normals
    @return Normal for each vertex
    */
    static Normals create_normals(
        const indexed_triangle_set &its,
        VertexNormalType type = VertexNormalType::NelsonMaxWeighted);
    static Normals create_normals_average_neighbor(const indexed_triangle_set &its);
    static Normals create_normals_angle_weighted(const indexed_triangle_set &its);
    static Normals create_normals_nelson_weighted(const indexed_triangle_set &its);

    /**
    @brief Calculate angle of trinagle side.
    @param i index to indices, define angle point
    @param indice address to vertices
    @param vertices vertices data
    @return Angle [in radian]
    */
    static float indice_angle(int i, const std::vector<stl_vertex> &vertices);
};

} // namespace Slic3r
#endif // slic3r_NormalUtils_hpp_
