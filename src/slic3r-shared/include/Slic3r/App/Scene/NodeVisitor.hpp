#pragma once

#include <stack>
#include "Slic3r/App/Scene/NodeVisitorTypes.hpp"
#include "Slic3r/App/Scene/Node.hpp"

/**
 * @file NodeVisitor.hpp
 *
 * @brief Node visitor functions.
 */

namespace Slic3r::App::Scene {

/**
 * @brief Visit const nodes in depth-first order.
 *
* By default visits enabled nodes or pass `ignore_enabled` as `true` to visit even disabled nodes).
* Once an disabled node is encountered, the propagation stops and its children are not visited even
* if these are enabled.
 *
 * @param node Root node to start the recursive visit
 * @param visitor Function of prototype `void(const Node&)` to be called on every node
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 */
void visit(const Node& node, const ConstNodeVisitor& visitor, bool ignore_enabled = false);

/**
 * @brief Visit non const nodes in depth-first order.
 *
 * By default visits enabled nodes or pass `ignore_enabled` as `true` to visit even disabled nodes).
 * Once an disabled node is encountered, the propagation stops and its children are not visited even
 * if these are enabled.
 *
 * @param node Root node to start the recursive visit
 * @param visitor Function of prototype `void(Node&)` to be called on every node
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 */
void visit(Node& node, const NodeVisitor& visitor, bool ignore_enabled = false);

/**
 * @brief Visit const nodes in depth-first order with possibility to quit the visit early before
 * visiting all nodes.
 * @param node Root node to start the visit with.
 * @param visitor A function called on visited note. If it returns `false` node's children are not
 * visited.
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 */
void visit_conditional(
    const Node& node, const ConstNodeConditionalVisitor& visitor, bool ignore_enabled = false
);

/**
 * @brief Visit non-const nodes in depth-first order with possibility to quit the visit early before
 * visiting all nodes.
 * @param node Root node to start the visit with.
 * @param visitor A function called on visited note. If it returns `false` node's children are not
 * visited.
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 */
void visit_conditional(
    Node& node, const NodeConditionalVisitor& visitor, bool ignore_enabled = false
);

/**
 * @brief Visit const nodes and transform them into list of Result.
 *
 * The visiting pattern is same as for  visit_conditional(), i.e. if the @p transform function
 * returns `false` for given visiting node, its children will be skipped.
 *
 * @tparam Result Type of result object the @p transform function produces for given node.
 * @param node A start node for visit
 * @param transform A function to transform given visiting node into @p Result
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 * @return Transformation output as list of `(node, result)` pairs.
 */
template<typename Result>
std::vector<std::pair<const Node*, Result>> visit_conditional_transform(
    const Node& node, const ConstNodeConditionalTransform<Result>& transform,
    bool ignore_enabled = false
)
{
    std::vector<std::pair<const Node*, Result>> ret;
    visit(node, [&ret, transform](const Node& n) {
        Result result;
        if (transform(n, result))
            ret.emplace_back(&n, result);
    }, ignore_enabled);
    return ret;
}

/**
 * @brief Visit const nodes and transform them into list of Result.
 *
 * The visiting pattern is same as for  visit_conditional(), i.e. if the @p transform function
 * returns `false` for given visiting node, its children will be skipped.
 *
 * @tparam Result Type of result object the @p transform function produces for given node.
 * @param node A start node for visit
 * @param transform A function to transform given visiting node into @p Result
 * @param ignore_enabled If set to `true`, even disabled nodes are visited, otherwise disabled nodes
 * are skipped.
 * @return Transformation output as list of `(node, result)` pairs.
 */
template<typename Result>
std::vector<std::pair<Node*, Result>> visit_conditional_transform(
    Node& node, const NodeConditionalTransform<Result>& transform,
    bool ignore_enabled = false
)
{
    std::vector<std::pair<Node*, Result>> ret;
    visit(node, [&ret, transform](Node& n) {
        Result result;
        if (transform(n, result))
            ret.emplace_back(&n, result);
    }, ignore_enabled);
    return ret;
}

}
