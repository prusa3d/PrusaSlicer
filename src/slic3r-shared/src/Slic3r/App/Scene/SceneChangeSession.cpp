#include "Slic3r/App/Scene/SceneChangeSession.hpp"

namespace Slic3r::App::Scene {

void AddNodeChange::roll_back(Scene& scene)
{
    auto* node = scene.node(m_node_id);
    if (node == nullptr)
        return;
    Node* original_parent = nullptr;
    if (m_original_parent_id) {
        original_parent = scene.node(m_original_parent_id);
        if (original_parent == nullptr)
            // This is moment where we have parent that was already removed
            // So we are done here
            return;
    }
    const Node::NodePredicate& predicate = [&](const auto* n) { return n->id() == m_node_id; };
    if (original_parent == nullptr)
        scene.remove_children(predicate, node->parent());
    else {
        auto children = scene.detach_children(predicate);
        for (auto& n : children)
            scene.add_child(n.release(), original_parent);
    }
}

void AddMaterialOverrideChange::roll_back(Scene& scene)
{
    Node* node = scene.node(m_changed_node_id);
    if (node != nullptr) {
        if (m_original_material)
            node->set_material_override(*m_original_material);
        else
            node->remove_material_override();
    }
}


void SceneChangeSession::roll_back()
{
    for(auto& c : m_changes)
        c->roll_back(m_scene);
    m_changes.clear();
}

void SceneChangeSession::roll_back_node(Node* n)
{
    const size_t node_id = n->id();
    // Move all changes to roll back on the right side of it
    auto it = std::partition(m_changes.begin(), m_changes.end(), [node_id](const auto& c) {
        return c->node_id() != node_id;
    });
    // Roll back all changes right to it
    std::for_each(it, m_changes.end(), [this](auto& c) { c->roll_back(m_scene); });
    // Erase all rolled back changes
    m_changes.erase(it, m_changes.end());
}

SceneChangeSession::NodeChangeBuilder& SceneChangeSession::NodeChangeBuilder::add_child(Node* child)
{
    m_session.m_scene.add_child(child, &m_node);
    const Node* p = child->parent();
    m_session.m_changes.push_back(
        std::make_unique<AddNodeChange>(m_node.id(), child->id(), p ? p->id() : 0)
    );
    return *this;
}
SceneChangeSession::NodeChangeBuilder& SceneChangeSession::NodeChangeBuilder::set_material_override(
    const Render::Material& m
)
{
    m_session.m_changes.push_back(
        std::make_unique<AddMaterialOverrideChange>(m_node.id(), m_node.material_override())
    );
    m_node.set_material_override(m);
    return *this;
}

}
