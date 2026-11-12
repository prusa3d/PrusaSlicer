#pragma once

#include <functional>

namespace Slic3r::App::Scene {

class Node;

using ConstNodeVisitor = std::function<void(const Node&)>;
using NodeVisitor = std::function<void(Node&)>;

using ConstNodeConditionalVisitor = std::function<bool(const Node&)>;
using NodeConditionalVisitor = std::function<bool(Node&)>;

using ConstNodePredicate = std::function<bool(const Node&)>;

template <typename T>
using ConstNodeConditionalTransform = std::function<bool(const Node&, T&)>;
template <typename T>
using NodeConditionalTransform = std::function<bool(Node&, T&)>;


}
