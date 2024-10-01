///|/ Copyright (c) Prusa Research 2022 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BranchingTree.hpp"

#include <optional>
#include <algorithm>
#include <cstddef>

#include "PointCloud.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/BoundingBox.hpp"

struct indexed_triangle_set;

namespace Slic3r { namespace branchingtree {

void build_tree(PointCloud &nodes, Builder &builder)
{
    constexpr size_t initK = 5;

    auto ptsqueue = nodes.start_queue();
    auto &properties = nodes.properties();

    struct NodeDistance
    {
        size_t node_id       = Node::ID_NONE;
        float  dst_branching = NaNf;
        float  dst_euql      = NaNf;
    };
    auto distances = reserve_vector<NodeDistance>(initK);
    double prev_dist_max = 0.;
    size_t K = initK;
    bool   routed = true;
    size_t node_id = Node::ID_NONE;

    while ((!ptsqueue.empty() && builder.is_valid()) || !routed) {
        if (routed) {
            node_id = ptsqueue.top();
            ptsqueue.pop();
        }

        Node node = nodes.get(node_id);
        nodes.mark_unreachable(node_id);

        distances.clear();
        distances.reserve(K);

        float dmax = 0.;
        nodes.foreach_reachable(
            node.pos,
            [&distances, &dmax](size_t id, float dst_branching, float dst_euql) {
                distances.emplace_back(NodeDistance{id, dst_branching, dst_euql});
                dmax = std::max(dmax, dst_euql);
            }, K, prev_dist_max);

        std::sort(distances.begin(), distances.end(),
                  [](auto &a, auto &b) { return a.dst_branching < b.dst_branching; });

        if (distances.empty()) {
            builder.report_unroutable(node);
            K = initK;
            prev_dist_max = 0.;
            routed = true;

            continue;
        }

        prev_dist_max = dmax;
        K *= 2;

        auto closest_it = distances.begin();
        routed = false;
        while (closest_it != distances.end() && !routed && builder.is_valid()) {
            size_t closest_node_id = closest_it->node_id;
            Node closest_node = nodes.get(closest_node_id);

            auto type = nodes.get_type(closest_node_id);
            float w = nodes.get(node_id).weight + closest_it->dst_branching;
            closest_node.Rmin = std::max(node.Rmin, closest_node.Rmin);

            switch (type) {
            case BED: {
                closest_node.weight = w;
                double max_br_len   = nodes.properties().max_branch_length();
                if (closest_it->dst_branching > max_br_len) {
                    std::optional<Vec3f> avo = builder.suggest_avoidance(node, max_br_len);
                    if (!avo)
                        break;

                    Node new_node {*avo, node.Rmin};
                    new_node.weight = nodes.get(node_id).weight + (node.pos - *avo).norm();
                    new_node.left   = node.id;
                    if ((routed = builder.add_bridge(node, new_node))) {
                        size_t new_idx = nodes.insert_junction(new_node);
                        ptsqueue.push(new_idx);
                    }
                } else if ((routed = builder.add_ground_bridge(node, closest_node))) {
                    closest_node.left = closest_node.right = node_id;
                    nodes.get(closest_node_id) = closest_node;
                    nodes.mark_unreachable(closest_node_id);
                }

                break;
            }
            case MESH: {
                closest_node.weight = w;
                if ((routed = builder.add_mesh_bridge(node, closest_node))) {
                    closest_node.left = closest_node.right = node_id;
                    nodes.get(closest_node_id) = closest_node;
                    nodes.mark_unreachable(closest_node_id);
                }

                break;
            }
            case LEAF:
            case JUNCTION: {
                auto max_slope = float(properties.max_slope());

                if (auto mergept = find_merge_pt(node.pos, closest_node.pos, max_slope)) {

                    float mergedist_closest = (*mergept - closest_node.pos).norm();
                    float mergedist_node = (*mergept - node.pos).norm();
                    float Wnode = nodes.get(node_id).weight;
                    float Wclosest = nodes.get(closest_node_id).weight;
                    float Wsum = std::max(Wnode, Wclosest);
                    float distsum = std::max(mergedist_closest, mergedist_node);
                    w = Wsum + distsum;

                    if (mergedist_closest > EPSILON && mergedist_node > EPSILON) {
                        Node mergenode{*mergept, closest_node.Rmin};
                        mergenode.weight = w;
                        mergenode.id = int(nodes.next_junction_id());

                        if ((routed = builder.add_merger(node, closest_node, mergenode))) {
                            mergenode.left = node_id;
                            mergenode.right = closest_node_id;
                            size_t new_idx = nodes.insert_junction(mergenode);
                            ptsqueue.push(new_idx);
                            size_t qid = nodes.get_queue_idx(closest_node_id);

                            if (qid != PointCloud::Unqueued)
                                ptsqueue.remove(nodes.get_queue_idx(closest_node_id));

                            nodes.mark_unreachable(closest_node_id);
                        }
                    } else if (closest_node.pos.z() < node.pos.z() &&
                               (closest_node.left == Node::ID_NONE ||
                                closest_node.right == Node::ID_NONE)) {
                        closest_node.weight = w;
                        if ((routed = builder.add_bridge(node, closest_node))) {
                            if (closest_node.left == Node::ID_NONE)
                                closest_node.left = node_id;
                            else if (closest_node.right == Node::ID_NONE)
                                closest_node.right = node_id;

                            nodes.get(closest_node_id) = closest_node;
                        }
                    }
                }

                break;
            }
            case NONE:;
            }

            ++closest_it;
        }

        if (routed) {
            prev_dist_max = 0.;
            K = initK;
        }
    }
}

void build_tree(const indexed_triangle_set &its,
                const std::vector<Node>    &support_roots,
                Builder                    &builder,
                const Properties           &properties)
{
    PointCloud nodes(its, support_roots, properties);

    build_tree(nodes, builder);
}

ExPolygon make_bed_poly(const indexed_triangle_set &its)
{
    auto bb = bounding_box(its);

    BoundingBox bbcrd{scaled(to_2d(bb.min)), scaled(to_2d(bb.max))};
    bbcrd.offset(scaled(10.));
    Point     min = bbcrd.min, max = bbcrd.max;
    ExPolygon ret = {{min.x(), min.y()},
                     {max.x(), min.y()},
                     {max.x(), max.y()},
                     {min.x(), max.y()}};

    return ret;
}

}} // namespace Slic3r::branchingtree
