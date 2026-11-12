#include "PlaceOnFaceGizmoPlanes.hpp"

#include "Slic3r/Domain/ModelObject.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"

#include <algorithm>
#include <numeric>


namespace Slic3r {

namespace {

using Slic3r::App::Plater::PlaneData;
using Domain::Vec3f;
using Domain::Transform3f;
using Domain::Vec3fs;

struct Patch {
    Vec3fs vertices;
    Vec3f normal;
    Transform3f trafo_to_horizontal;
};

Domain::TriangleMesh transformed_convex_hull(const Domain::ModelObject& model_object)
{
    Domain::TriangleMesh ch;
    for (const Domain::ModelVolume* vol : model_object.volumes) {
        if (vol->type() != Domain::ModelVolumeType::MODEL_PART)
            continue;
        Domain::TriangleMesh vol_ch = vol->get_convex_hull();
        vol_ch.transform(vol->get_matrix());
        ch.merge(vol_ch);
    }
    return Biz::Algorithms::TriangleMesh::convex_hull_3d(ch);
}


void transform_vertices(Vec3fs& vertices, const Transform3f& trafo)
{
    for (Vec3f& pt : vertices)
        pt = trafo * pt;
}

std::vector<Patch> calculate_patches(const Domain::TriangleMesh& mesh, const Transform3f inst_matrix, float minimal_side)
{
    std::vector<Patch> patches;
    // Now we'll go through all the facets and append Points of facets sharing the same normal.
    // This part is still performed in mesh coordinate system.
    const int                num_of_facets  = mesh.facets_count();
    const std::vector<Vec3f> face_normals   = Biz::Algorithms::TriangleMesh::its_face_normals(mesh.its);
    const std::vector<Domain::Index3> face_neighbors = Biz::Algorithms::TriangleMesh::its_face_neighbors(mesh.its);
    std::vector<int>         facet_queue(num_of_facets, 0);
    std::vector<bool>        facet_visited(num_of_facets, false);
    int                      facet_queue_cnt = 0;
    const stl_normal*        normal_ptr      = nullptr;
    int facet_idx = 0;
    while (1) {
        // Find next unvisited triangle:
        for (; facet_idx < num_of_facets; ++ facet_idx)
            if (!facet_visited[facet_idx]) {
                facet_queue[facet_queue_cnt ++] = facet_idx;
                facet_visited[facet_idx] = true;
                normal_ptr = &face_normals[facet_idx];
                patches.emplace_back();
                break;
            }
        if (facet_idx == num_of_facets)
            break; // Everything was visited already

        while (facet_queue_cnt > 0) {
            int facet_idx = facet_queue[-- facet_queue_cnt];
            const stl_normal& this_normal = face_normals[facet_idx];
            if (std::abs(this_normal(0) - (*normal_ptr)(0)) < 0.001 && std::abs(this_normal(1) - (*normal_ptr)(1)) < 0.001 && std::abs(this_normal(2) - (*normal_ptr)(2)) < 0.001) {
                const Domain::Index3 face = mesh.its.indices[facet_idx];
                for (int j=0; j<3; ++j)
                    patches.back().vertices.emplace_back(mesh.its.vertices[face[j]]);

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j)
                    if (int neighbor_idx = face_neighbors[facet_idx][j]; neighbor_idx >= 0 && ! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
            }
        }
        patches.back().normal = *normal_ptr;

        Vec3fs& verts = patches.back().vertices;
        // Now we'll transform all the points into world coordinates, so that the areas, angles and distances
        // make real sense.
        transform_vertices(verts, inst_matrix);

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected later anyway):
        if (verts.size() == 3 &&
            ((verts[0] - verts[1]).norm() < minimal_side
            || (verts[0] - verts[2]).norm() < minimal_side
            || (verts[1] - verts[2]).norm() < minimal_side))
            patches.pop_back();
    }
    return patches;
}

Transform3f get_transform_to_horizontal(Patch& patch, const Eigen::Matrix3f& normal_matrix)
{
    // transform the normal according to the instance matrix:
    const Vec3f normal_transformed = normal_matrix * patch.normal;

    // We are going to rotate about z and y to flatten the plane
    Eigen::Quaternionf q;
    auto m = Transform3f::Identity();
    m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(normal_transformed, Vec3f::UnitZ()).toRotationMatrix();
    return m;
}

void remove_inner_points(Vec3fs& verts)
{
    // Now to remove the inner points. We'll misuse Geometry::convex_hull for that, but since
    // it works in fixed point representation, we will rescale the polygon to avoid overflows.
    // And yes, it is a nasty thing to do. Whoever has time is free to refactor.
    Domain::BoundingBox3f bb = Biz::Algorithms::BoundingBox::construct(verts);
    Vec3f bb_size = Biz::Algorithms::BoundingBox::sizes(bb);
    double sf = std::min(1./bb_size(0), 1./bb_size(1));
    Transform3f tr = Domain::scale_transform({ sf, sf, 1. }).cast<float>();
    transform_vertices(verts, tr);
    verts = Biz::Algorithms::Geometry::convex_hull_2d_xy(verts);
    transform_vertices(verts, tr.inverse());
}

bool should_be_discarded(const Vec3fs& polygon)
{
    const float minimal_area = 5.f; // in square mm (world coordinates)
    // Calculate area of the polygons and discard ones that are too small
    float area = 0.f;
    for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
        area += polygon[i](0)*polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0)*polygon[i](1);
    area = 0.5f * std::abs(area);
    
    if (area < minimal_area)
        return true;

    // We also check the inner angles and discard polygons with angles smaller than the following threshold
    const float angle_threshold = std::cos(10.f * (float)M_PI / 180.f);
    for (unsigned int i = 0; i < polygon.size(); ++i) {
        const Vec3f& prec = polygon[(i == 0) ? polygon.size() - 1 : i - 1];
        const Vec3f& curr = polygon[i];
        const Vec3f& next = polygon[(i == polygon.size() - 1) ? 0 : i + 1];
        if ((prec - curr).normalized().dot((next - curr).normalized()) > angle_threshold)
            return true;
    }
    return false;
}

void shrink_polygon(Vec3fs& polygon)
{
    // We will shrink the polygon a little bit so it does not touch the object edges:
    Vec3f centroid = std::accumulate(polygon.begin(), polygon.end(), Vec3f(0.f, 0.f, 0.f));
    centroid /= (float)polygon.size();
    for (Vec3f& vertex : polygon)
        vertex = 0.9f*vertex + 0.1f*centroid;

    // Polygon is now simple and convex, we'll round the corners to make them look nicer.
    // The algorithm takes a vertex, calculates middles of respective sides and moves the vertex
    // towards their average (controlled by 'aggressivity'). This is repeated k times.
    // In next iterations, the neighbours are not always taken at the middle (to increase the
    // rounding effect at the corners, where we need it most).
    const unsigned int k = 10; // number of iterations
    const float aggressivity = 0.2f;  // agressivity
    const unsigned int N = polygon.size();
    std::vector<std::pair<unsigned int, unsigned int>> neighbours;
    if (k != 0) {
        Vec3fs points_out(2*k*N); // vector long enough to store the future vertices
        for (unsigned int j=0; j<N; ++j) {
            points_out[j*2*k] = polygon[j];
            neighbours.push_back(std::make_pair((int)(j*2*k-k) < 0 ? (N-1)*2*k+k : j*2*k-k, j*2*k+k));
        }

        for (unsigned int i=0; i<k; ++i) {
            // Calculate middle of each edge so that neighbours points to something useful:
            for (unsigned int j=0; j<N; ++j)
                if (i==0)
                    points_out[j*2*k+k] = 0.5f * (points_out[j*2*k] + points_out[j==N-1 ? 0 : (j+1)*2*k]);
                else {
                    float r = 0.2+0.3/(k-1)*i; // the neighbours are not always taken in the middle
                    points_out[neighbours[j].first] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].first-1];
                    points_out[neighbours[j].second] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].second+1];
                }
            // Now we have a triangle and valid neighbours, we can do an iteration:
            for (unsigned int j=0; j<N; ++j)
                points_out[2*k*j] = (1-aggressivity) * points_out[2*k*j] +
                                    aggressivity*0.5f*(points_out[neighbours[j].first] + points_out[neighbours[j].second]);

            for (auto& n : neighbours) {
                ++n.first;
                --n.second;
            }
        }
        polygon = std::move(points_out); // replace the coarse polygon with the smooth one that we just created
    }
}

std::vector<PlaneData> triangulate_and_convert_to_its(std::vector<Patch>& patches, const Transform3f& inst_matrix)
{
    // The polygon is convex with the vertices in order, so triangulation is trivial.
    std::vector<PlaneData> planes_out;
    planes_out.reserve(patches.size());
    for (Patch& patch : patches) {
        PlaneData& plane = planes_out.emplace_back();
        plane.normal = patch.normal;
        plane.its.vertices = std::move(patch.vertices);
        plane.its.indices.reserve(plane.its.vertices.size() / 3);
        for (int i = 1; i < int(plane.its.vertices.size()) - 1; ++i)
            plane.its.indices.emplace_back(Domain::Index3{0, i, i + 1}); // triangle fan
        if (Domain::Transformation(inst_matrix.cast<double>()).is_left_handed()) {
            // we need to swap face normals in case the object is mirrored
            // for the raycaster to work properly
            for (stl_triangle_vertex_indices& face : plane.its.indices) {
                if (Biz::Algorithms::TriangleMesh::its_face_normal(plane.its, face).dot(plane.normal) < 0.f)
                    std::swap(face[1], face[2]);
            }
        }
    }
    return planes_out;
}

} // anonymous namespace
} // namespace Slic3r


namespace Slic3r::App::Plater {

std::vector<PlaneData> calculate_planes(const Domain::ModelObject& model_object)
{
    Domain::TriangleMesh ch = transformed_convex_hull(model_object);
    const Transform3f inst_matrix = model_object.instances.front()->get_matrix().cast<float>();
    // Let's prepare transformation of the normal vector from mesh to instance coordinates.
    const Eigen::Matrix3f normal_matrix = inst_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    
    const float minimal_side = 1.f; // mm

    std::vector<Patch> patches = calculate_patches(ch, inst_matrix, minimal_side);

    for (Patch& patch : patches) {
        patch.trafo_to_horizontal = get_transform_to_horizontal(patch, normal_matrix);
        transform_vertices(patch.vertices, patch.trafo_to_horizontal);
        remove_inner_points(patch.vertices);
    }

    patches.erase(std::remove_if(patches.begin(), patches.end(),
                      [](const Patch& p){ return should_be_discarded(p.vertices); }),
                  patches.end());

    for (Patch& patch : patches) {
        shrink_polygon(patch.vertices);
        for (auto& b : patch.vertices) {
            // Raise a bit above the object surface to avoid flickering:
            b(2) += 0.1f;
        }
        // Transform back to 3D (and also back to mesh coordinates)
        transform_vertices(patch.vertices, inst_matrix.inverse() * patch.trafo_to_horizontal.inverse());
    }

    // This should be no longer needed...
    // We'll sort the planes by area and only keep the 254 largest ones (because of the picking pass limitations):
    //std::sort(planes.rbegin(), planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });
    //planes.resize(std::min((int)planes.size(), 254));

    return triangulate_and_convert_to_its(patches, inst_matrix);    
}

} // namespace Slic3r::App::Plater
