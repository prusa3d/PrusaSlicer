#pragma once
#include "Slic3r/App/Scene/Transform.hpp"
#include "Slic3r/App/Scene/IRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/IImguiRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/IRaycastNodeComponent.hpp"
#include "Slic3r/App/Scene/INodeTransformModifier.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/NodeVisitorTypes.hpp"
#include "Slic3r/App/Scene/ScreenSpaceSizedTransformModifier.hpp"

#include <boost/any.hpp>

namespace Slic3r::App::Scene {

class Node;

class INodeChangedListener
{
public:
    virtual ~INodeChangedListener() = default;

    virtual void on_node_changed(Node* node) = 0;
};

/**
 * @brief Scenegraph node with transformation and set of node components.
 *
 * @details
 *
 * This is sort of low-level object, for more comfort use
 * - Scene as entry point to scenegraph,
 * - @ref NodeVisitor.hpp "node visitors" to visit and transform (sub-)graph
 * - NodeBuilder to create sub-scenegraph
 * .
 *
 * In terms of tree hierarchy node contains:
 * - parent link: see @ref Node::parent() const
 * - list of children: see Node::children() const
 * - use Scene::add_child() and Scene::remove_children() to modify node hierarchy
 * .
 * In terms of transformation node contains:
 * - local transform (stored, see local_transform() )
 * - world_transform (computed if dirty, see world_transform() )
 * .
 * And these are components the can get attached:
 * - to render 3D object in scene use IRenderNodeComponent or its implementation MeshRenderNodeComponent,
 *   see render_component(), set_render_component(), has_render_component()
 * - to override visual material of 3D object use Material,
 *   see material_override(), set_material_override(), has_material_override()
 * - for object picking use IRaycastNodeComponent or its AABBMesh implementation AabbRaycastNodeComponent,
 *   see raycast_component(), set_raycast_component(), has_raycast_component()
 * - to render 2D GUI overlay use IImguiRenderNodeComponent
 *   see imgui_render_component(), set_imgui_render_component(), has_imgui_render_component()
 * - tag with extra information helping identify purpose of specific node (e.g. if picked via Scene::pick_at()),
 *   see has_tag_of_type(), set_tag(), tag()
 * .
 */
class Node final
{
public:
    using NodeOwningList = std::vector<std::unique_ptr<Node>>;
    using NodeList = std::vector<Node*>;
    using ConstNodeList = std::vector<const Node*>;
    using NodePredicate = std::function<bool(const Node*)>;
    using NodeModifyingPredicate = std::function<bool(Node*)>;

    Node();

    /**
     * @name Identification
     * Simple unique node identifier (automatically assigned on creation).
     * Is always greater than zero, so zero can be used as unset or invalid ID.
     * @{
     */
    size_t id() const { return m_id; }
    void set_id(size_t id) { m_id = id; }
    /** @} */

    /**
     * @name Hierarchy
     * Parent-child hierarchy
     * @{
     */
    /**
     * @brief Nodes parent pointer
     * @return Node parent or `nullptr` if the node is root itself.
     */
    const Node* parent() const { return m_parent; }

    /**
     * @brief Nodes parent pointer
     * @return Node parent of `nullptr` if root itself.
     */
    Node* parent() { return m_parent; }

    /**
     * @brief Read-only children
     *
     * Use Node::query() or @ref NodeVisitor.hpp "node visitors"
     *
     * @return Constant vector of `unique_ptr<Node>`
     */
    const NodeOwningList& children() const { return m_children; }

    /** @} */

    /**
     * @name Transform
     * Node local and world transform
     * @{
     */
    /**
     * @brief Local transformation
     * @return
     */
    const Transform& local_transform() const { return m_local_xform; }
    /**
     * @brief World transformation
     *
     * If dirty the transformation gets recomputed.
     *
     * @return
     */
    const Transform& world_transform() const;

    /**
     * @brief Sets local transformation
     *
     * Sets local transformation and marks world_transform() dirty for this node and all its children.
     *
     * @param t
     */
    void set_local_transform(const Transform& t)
    {
        m_local_xform = t;
        mark_world_transform_dirty();
        if (m_node_changed_listener != nullptr)
            m_node_changed_listener->on_node_changed(this);
    }

    /**
     * @brief Sets world transformation
     * @param t
     */
    void set_world_transform(const Transform& t)
    {
        if (m_parent)
        {
            auto inv_parent_world = m_parent->world_transform().inverse();
            set_local_transform(t * inv_parent_world);
        } else
            set_local_transform(t);
    }
    /** @} */

    /**
     * @name Enabled
     * Node enable state
     * @{
     */
    /**
     * @brief Is node enabled.
     *
     * Only enabled nodes can appear in query(const NodePredicate&, NodeList&, bool),
     * query(const ConstNodePredicate&, ConstNodeList&, bool), and @ref NodeVisitor.hpp "node visitors"
     *
     * @return True if enabled
     */
    bool enabled() const { return m_enabled; }

    /**
     * @brief Set node enabled flag
     * @param enabled
     */
    void set_enabled(bool enabled)
    {
        if (m_enabled != enabled) {
            m_enabled = enabled;
            if (m_node_changed_listener != nullptr)
                m_node_changed_listener->on_node_changed(this);
        }
    }
    /** @} */

    /**
     * @name Transform Modifier
     * World Transformation modifier
     * @{
     */
    const INodeTransformModifier* transform_modifier() const { return m_transform_modifier.get(); }
    INodeTransformModifier* transform_modifier() { return m_transform_modifier.get(); }
    void set_transform_modifier(std::unique_ptr<INodeTransformModifier>&& modifier)
    {
        m_transform_modifier = std::move(modifier);
        if (m_node_changed_listener != nullptr)
            m_node_changed_listener->on_node_changed(this);
    }
    /**@}*/

    /**
     * @name Query all
     * Query node and its children
     * @{
     */
    /**
     * @brief Query this node and its children (only enabled by default).
     */
    void query(const NodePredicate& predicate, NodeList& found_nodes, bool ignore_enabled = false);

    /**
     * @brief Query this node and its children (only enabled by default).
     */
    void query(const NodePredicate& predicate, ConstNodeList& found_nodes, bool ignore_enabled = false) const;

    /**@}*/

    /**
     * @name Query first
     * @{
     */

    /**
     * @brief Query this node and its children for first match (only enabled by default).
     */
    const Node* query_first(const NodePredicate& predicate, bool ignore_enabled = false) const;

    /**
     * @brief Query this node and its children for first match (only enabled by default).
     */
    Node* query_first(const NodePredicate& predicate, bool ignore_enabled = false);

    /** @} */

    /**
     * @name Render Component
     * Rendering 3D object
     * @{
     */
    bool has_render_component() const { return bool(m_render_component);}
    void set_render_component(IRenderNodeComponent* component);
    void set_render_component(std::unique_ptr<IRenderNodeComponent>&& component) { set_render_component(component.release()); }
    const IRenderNodeComponent* render_component() const { return m_render_component.get(); }
    IRenderNodeComponent* render_component() { return m_render_component.get(); }
    /**@}*/

    /**
     * @name Material Override
     * Override 3D object material
     * @{
     */
    bool has_material_override() const { return bool(m_material_override); }
    void set_material_override(const Render::Material& material) { m_material_override = std::make_unique<Render::Material>(material); }
    void remove_material_override() { if (m_material_override) m_material_override.reset(); }
    const Render::Material* material_override() const { return m_material_override.get(); }
    /**@}*/

    /**
     * @name ImGUI Render Component
     * Rendering 2D GUI overlay
     * @{
     */
    bool has_imgui_render_component() const { return bool(m_imgui_render_component);}
    void set_imgui_render_component(IImguiRenderNodeComponent* component);
    void set_imgui_render_component(std::unique_ptr<IImguiRenderNodeComponent>&& component) { set_imgui_render_component(component.release()); }
    const IImguiRenderNodeComponent* imgui_render_component() const { return m_imgui_render_component.get(); }
    /**@}*/

    /**
     * @name Raycast Component
     * Raycast hit test
     * @{
     */
    bool has_raycast_component() const { return bool(m_raycast_component); }
    void set_raycast_component(std::unique_ptr<IRaycastNodeComponent>&& component) { m_raycast_component = std::move(component); }
    void set_raycast_component(IRaycastNodeComponent* component)
    {
        m_raycast_component.reset(component);
        if (m_node_changed_listener != nullptr)
            m_node_changed_listener->on_node_changed(this);
    }
    const IRaycastNodeComponent* raycast_component() const { return m_raycast_component.get(); }
    // returns true if this node or any of its children has a raycast component
    bool contains_raycast_component() const;
    /**@}*/

    /**
     * @name Tag Component
     * Metadata tag
     * @{
     */
    template <typename T>
    bool has_tag_of_type() const { return m_tag.type() == typeid(T); }
    void set_tag(const boost::any& tag) { m_tag = tag; }
    boost::any tag() const { return m_tag; }
    template <typename T>
    const T* tag_of_type() const { return boost::any_cast<T>(&m_tag); }
    template <typename T>
    T* tag_of_type() { return boost::any_cast<T>(&m_tag); }
    /**@}*/

    /**
     * @name Debug name
     * Node name to be used for debugging to distinguish between individual nodes in tree debug window.
     * @{
     */
    void set_debug_name(std::string_view str)
    { m_debug_name = str; }
    const std::string& debug_name() const
    { return m_debug_name; }
    /**@}*/

    size_t level() const
    {
        size_t level = 0;
        const auto* n = m_parent;
        while (n != nullptr) {
            level++;
            n = n->parent();
        }
        return level;
    }

private:
    friend class Scene;

    // Use Scene::add_child instead
    void add_child(Node* n)
    {
        if (n->m_parent == this)
            // already added
            return;

        n->m_parent = this;
        n->mark_world_transform_dirty();
        m_children.emplace_back(n);
    }

    // Use Scene::remove_children instead
    bool remove_children(const NodeModifyingPredicate& predicate)
    {
        auto it = std::remove_if(
            m_children.begin(), m_children.end(),
            [predicate](NodeOwningList::reference node) {
                Node* node_ptr = node.get();
                return predicate(node_ptr);
            }
        );
        if (it == m_children.end())
            return false;
        m_children.erase(it, m_children.end());
        return true;
    }

    NodeOwningList detach_children(const NodeModifyingPredicate& predicate)
    {
        NodeOwningList ret;
        // Move all nodes to detach to upper (right) side of vector
        auto it = std::stable_partition(
            m_children.begin(), m_children.end(),
            [&](const auto& n) { return !predicate(n.get()); }
        );

        std::move(it, m_children.end(), std::back_inserter(ret));
        m_children.erase(it, m_children.end());
        return ret;
    }


    void mark_world_transform_dirty() const;
    //void mark_children_world_transform_dirty() const;

    void set_node_changed_listener(INodeChangedListener* listener) { m_node_changed_listener = listener; }


    friend void visit(const Node&, const ConstNodeVisitor &, bool);
    friend void visit(Node&, const NodeVisitor &, bool);
    friend void visit_conditional(const Node& node, const ConstNodeConditionalVisitor& visitor, bool);
    friend void visit_conditional(Node& node, const NodeConditionalVisitor& visitor, bool);
private:
    friend void ScreenSpaceSizedTransformModifier::camera_updated(const Camera& cam);

    size_t m_id{0};
    Node* m_parent{nullptr};
    NodeOwningList m_children;

    INodeChangedListener* m_node_changed_listener{ nullptr };

    Transform m_local_xform{Transform::Identity()};
    mutable Transform m_world_xform{Transform::Identity()};
    mutable bool m_world_xform_dirty{false};

    bool m_enabled{true};

    std::unique_ptr<IRenderNodeComponent> m_render_component;
    std::unique_ptr<Render::Material> m_material_override;
    std::unique_ptr<IImguiRenderNodeComponent> m_imgui_render_component;
    std::unique_ptr<IRaycastNodeComponent> m_raycast_component;
    std::unique_ptr<INodeTransformModifier> m_transform_modifier;

    boost::any m_tag;

    std::string m_debug_name;
};

} // namespace Slic3r::App::Scene
