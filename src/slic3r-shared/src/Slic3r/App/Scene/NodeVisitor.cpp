#include "Slic3r/App/Scene/NodeVisitor.hpp"

namespace Slic3r::App::Scene {
void visit(const Node& node, const ConstNodeVisitor& visitor, bool ignore_enabled)
{
    std::stack<const Node*> stack;
    stack.push(&node);

    while (!stack.empty())
    {
        const Node* n = stack.top();
        stack.pop();
        if (n->enabled() || ignore_enabled) {
            visitor(*n);

            for (auto it = n->m_children.rbegin(); it != n->m_children.rend(); ++it)
                stack.push(it->get());
        }
    }

}

void visit(Node& node, const NodeVisitor& visitor, bool ignore_enabled)
{
    std::stack<Node*> stack;
    stack.push(&node);

    while (!stack.empty())
    {
        Node* n = stack.top();
        stack.pop();
        if (n->enabled() || ignore_enabled) {
            visitor(*n);

            for (auto it = n->m_children.rbegin(); it != n->m_children.rend(); ++it)
                stack.push(it->get());
        }
    }
}

void visit_conditional(const Node& node, const ConstNodeConditionalVisitor& visitor, bool ignore_enabled)
{
    std::stack<const Node*> stack;
    stack.push(&node);

    while (!stack.empty())
    {
        const Node* n = stack.top();
        stack.pop();
        if ((n->enabled() || ignore_enabled) && visitor(*n)) {
            for (auto it = n->m_children.rbegin(); it != n->m_children.rend(); ++it)
                stack.push(it->get());
        }
    }
}

void visit_conditional(Node& node, const NodeConditionalVisitor& visitor, bool ignore_enabled)
{
    std::stack<Node*> stack;
    stack.push(&node);

    while (!stack.empty())
    {
        Node* n = stack.top();
        stack.pop();
        if ((n->enabled() || ignore_enabled) && visitor(*n)) {
            for (auto it = n->m_children.rbegin(); it != n->m_children.rend(); ++it)
                stack.push(it->get());
        }
    }
}

}
