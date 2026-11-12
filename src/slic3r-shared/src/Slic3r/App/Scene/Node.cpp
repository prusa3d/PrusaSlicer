#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/IdGenerator.hpp"

#include <memory>

#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Scene {

static size_t next_id() {
    static IdGenerator<size_t> id_generator(0);
    return id_generator.next_id();
}

Node::Node() : m_id(next_id()) {}

// NOLINTBEGIN(misc-no-recursion): Mark recursion in query() as resolved
void Node::query(const NodePredicate& predicate, NodeList& found_nodes, bool ignore_enabled)
{
    if (m_enabled || ignore_enabled) {
        if (predicate(this))
            found_nodes.push_back(this);
        for (auto& child : m_children) {
            child->query(predicate, found_nodes, ignore_enabled);
        }
    }
}

void Node::query(const NodePredicate& predicate, ConstNodeList& found_nodes, bool ignore_enabled) const
{
    if (m_enabled || ignore_enabled) {
        if (predicate(this))
            found_nodes.push_back(this);
        for (auto& child : m_children) {
            child->query(predicate, found_nodes, ignore_enabled);
        }
    }
}

const Node* Node::query_first(const NodePredicate& predicate, bool ignore_enabled) const
{
    if (m_enabled || ignore_enabled) {
        if (predicate(this))
            return this;
        for (auto& child : m_children) {
            if (auto* ret = child->query_first(predicate, ignore_enabled))
                return ret;
        }
    }
    return nullptr;
}

Node *Node::query_first(const NodePredicate &predicate, bool ignore_enabled)
{
    if (m_enabled || ignore_enabled) {
        if (predicate(this))
            return this;
        for (auto &child: m_children) {
            if (auto *ret = child->query_first(predicate, ignore_enabled))
                return ret;
        }
    }
    return nullptr;
}


const Transform& Node::world_transform() const
{
    if (m_world_xform_dirty) {
        if (m_parent) {
            m_world_xform = m_parent->world_transform() * m_local_xform;
        } else {
            m_world_xform = m_local_xform;
        }
        if (m_transform_modifier)
            m_transform_modifier->modify_world_transform(m_world_xform);
        m_world_xform_dirty = false;
    }
    return m_world_xform;
}

// NOLINTEND(misc-no-recursion)


void Node::set_render_component(IRenderNodeComponent* component)
{
    m_render_component.reset(component);
}

void Node::set_imgui_render_component(IImguiRenderNodeComponent* component)
{
    m_imgui_render_component.reset(component);
}

void Node::mark_world_transform_dirty() const
{
    visit_conditional(*this, [](const Node& n) -> bool {
        if (n.m_world_xform_dirty)
            return false;
        n.m_world_xform_dirty = true;
        return true;
    }, true);
}

bool Node::contains_raycast_component() const
{
    if (has_raycast_component())
        return true;
    return std::any_of(m_children.begin(), m_children.end(), [](NodeOwningList::const_reference n) {
        return n->contains_raycast_component();
    });
}

} // namespace Slic3r::App::Scene
