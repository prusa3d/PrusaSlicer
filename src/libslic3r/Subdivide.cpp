///|/ Copyright (c) Prusa Research 2022 Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Subdivide.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "Point.hpp"

namespace Slic3r{

indexed_triangle_set its_subdivide(
    const indexed_triangle_set &its, float max_length)
{
    // same order as key order in Edge Divides
    struct VerticesSequence
    {
        size_t   start_index;
        bool     positive_order;
        VerticesSequence(size_t start_index, bool positive_order = true)
            : start_index(start_index), positive_order(positive_order){}
    };
    //                         vertex index small, big      vertex index from key.first to key.second
    using EdgeDivides = std::map<std::pair<size_t, size_t>, VerticesSequence>;
    struct Edges
    {
        Vec3f data[3];
        Vec3f lengths;
        Edges(const Vec3crd &indices, const std::vector<Vec3f> &vertices)
            : lengths(-1.f,-1.f,-1.f)
        {
            const Vec3f &v0 = vertices[indices[0]];
            const Vec3f &v1 = vertices[indices[1]];
            const Vec3f &v2 = vertices[indices[2]];
            data[0] = v0 - v1;
            data[1] = v1 - v2;
            data[2] = v2 - v0;
        }
        float abs_sum(const Vec3f &v)
        {
            return abs(v[0]) + abs(v[1]) + abs(v[2]);
        }
        bool is_dividable(const float& max_length) {
            Vec3f sum(abs_sum(data[0]), abs_sum(data[1]), abs_sum(data[2]));
            Vec3i biggest_index = (sum[0] > sum[1]) ?
                                      ((sum[0] > sum[2]) ?
                                           ((sum[2] > sum[1]) ?
                                                Vec3i(0, 2, 1) :
                                                Vec3i(0, 1, 2)) :
                                           Vec3i(2, 0, 1)) :
                                      ((sum[1] > sum[2]) ?
                                           ((sum[2] > sum[0]) ?
                                                Vec3i(1, 2, 0) :
                                                Vec3i(1, 0, 2)) :
                                           Vec3i(2, 1, 0));
            for (int i = 0; i < 3; i++) {
                int index = biggest_index[i];
                if (sum[index] <= max_length) return false;
                lengths[index] = data[index].norm();
                if (lengths[index] <= max_length) continue;

                // calculate rest of lengths
                for (int j = i + 1; j < 3; j++) {
                    index     = biggest_index[j];
                    lengths[index] = data[index].norm();
                }
                return true;
            }
            return false;
        }
    };
    struct TriangleLengths
    {
        Vec3crd indices;
        Vec3f l; // lengths
        TriangleLengths(const Vec3crd &indices, const Vec3f &lengths)
            : indices(indices), l(lengths)
        {}

        int get_divide_index(float max_length) {
            if (l[0] > l[1] && l[0] > l[2]) {
                if (l[0] > max_length) return 0;
            } else if (l[1] > l[2]) {
                if (l[1] > max_length) return 1;
            } else {
                if (l[2] > max_length) return 2;
            }
            return -1;
        }

        // divide triangle add new vertex to vertices
        std::pair<TriangleLengths, TriangleLengths> divide(
            int divide_index, float max_length,
            std::vector<Vec3f> &vertices,
            EdgeDivides &edge_divides)
        {
            // index to lengths and indices
            size_t i0 = divide_index;
            size_t i1 = (divide_index + 1) % 3;
            size_t vi0   = indices[i0];
            size_t vi1   = indices[i1];
            std::pair<size_t, size_t> key(vi0, vi1);
            bool key_swap = false;
            if (key.first > key.second) {
                std::swap(key.first, key.second);
                key_swap = true;
            }

            float length = l[divide_index];
            size_t count_edge_vertices  = static_cast<size_t>(floor(length / max_length));
            float count_edge_segments = static_cast<float>(count_edge_vertices + 1);

            auto it = edge_divides.find(key);
            if (it == edge_divides.end()) {
                // Create new vertices
                VerticesSequence new_vs(vertices.size());
                Vec3f vf = vertices[key.first]; // copy
                const Vec3f &vs = vertices[key.second];
                Vec3f dir = vs - vf;
                for (size_t i = 1; i <= count_edge_vertices; ++i) {
                    float ratio = i / count_edge_segments;
                    vertices.push_back(vf + dir * ratio);
                }
                bool     success;
                std::tie(it,success) = edge_divides.insert({key, new_vs});
                assert(success);
            }
            const VerticesSequence &vs = it->second;

            int index_offset = count_edge_vertices/2;
            size_t i2 = (divide_index + 2) % 3;
            if (count_edge_vertices % 2 == 0 && key_swap == (l[i1] < l[i2])) {
                --index_offset;
            }
            int sign = (vs.positive_order) ? 1 : -1;
            size_t new_index = vs.start_index + sign*index_offset;

            size_t vi2   = indices[i2];
            const Vec3f &v2 = vertices[vi2];
            Vec3f        new_edge = v2 - vertices[new_index];
            float        new_len  = new_edge.norm();

            float ratio = (1 + index_offset) / count_edge_segments;
            float len1 = l[i0] * ratio;
            float len2 = l[i0] - len1;
            if (key_swap) std::swap(len1, len2);

            Vec3crd indices1(vi0, new_index, vi2);
            Vec3f lengths1(len1, new_len, l[i2]);

            Vec3crd indices2(new_index, vi1, vi2);
            Vec3f lengths2(len2, l[i1], new_len);

            // append key for divided edge when neccesary
            if (index_offset > 0) {
                std::pair<size_t, size_t> new_key(key.first, new_index);
                bool new_key_swap = false;
                if (new_key.first > new_key.second) {
                    std::swap(new_key.first, new_key.second);
                    new_key_swap = true;
                }
                if (edge_divides.find(new_key) == edge_divides.end()) {
                    // insert new
                    edge_divides.insert({new_key, (new_key_swap) ?
                        VerticesSequence(new_index - sign, !vs.positive_order)
                        : vs});
                }
            }

            if (index_offset < int(count_edge_vertices)-1) {
                std::pair<size_t, size_t> new_key(new_index, key.second);
                bool new_key_swap = false;
                if (new_key.first > new_key.second) {
                    std::swap(new_key.first, new_key.second);
                    new_key_swap = true;
                }
                // bad order
                if (edge_divides.find(new_key) == edge_divides.end()) {
                    edge_divides.insert({new_key, (new_key_swap) ?
                        VerticesSequence(vs.start_index + sign*(count_edge_vertices-1), !vs.positive_order)
                        : VerticesSequence(new_index + sign, vs.positive_order)});
                }
            }

            return {TriangleLengths(indices1, lengths1),
                    TriangleLengths(indices2, lengths2)};
        }
    };
    indexed_triangle_set result;
    result.indices.reserve(its.indices.size());
    const std::vector<Vec3f> &vertices = its.vertices;
    result.vertices = vertices; // copy
    std::queue<TriangleLengths> tls;

    EdgeDivides edge_divides;
    for (const Vec3crd &indices : its.indices) {
        Edges edges(indices, vertices);
        // speed up only sum not sqrt is apply
        if (!edges.is_dividable(max_length)) {
             // small triangle
            result.indices.push_back(indices);
            continue;
        }
        TriangleLengths tl(indices, edges.lengths);
        do {
            int divide_index = tl.get_divide_index(max_length);
            if (divide_index < 0) {
                // no dividing
                result.indices.push_back(tl.indices);
                if (tls.empty()) break;
                tl = tls.front(); // copy
                tls.pop();
            } else {
                auto [tl1, tl2] = tl.divide(divide_index, max_length,
                                            result.vertices, edge_divides);
                tl = tl1;
                tls.push(tl2);
            }
        } while (true);
    }
    return result;
}

}
