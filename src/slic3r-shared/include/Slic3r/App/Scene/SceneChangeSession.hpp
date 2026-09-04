#pragma once

#include <vector>
#include <memory>

#include "Slic3r/App/Scene/Scene.hpp"

namespace Slic3r::App::Scene {

/**
 * @breif Interface to record single node change with ability to roll back the change later.
 *
 * Use NodeChangeBuilder returned from SceneChangeSession::change() method to record changes for
 * given node (passed as argument to SceneChangeSession::change().
 */
class AbstractSceneChange
{
public:
    explicit AbstractSceneChange(size_t changed_node_id) : m_changed_node_id(changed_node_id) {}
    virtual ~AbstractSceneChange() = default;

    /**
     * @brief Roll back recorded change in given scene.
     * @param scene A scene containing node the change was recorded for, to be used for roll back.
     */
    virtual void roll_back(Scene& scene) = 0;

    /**
     * @brief Gets ID of node the change belongs to (the change happens to)
     * @return ID of changed node.
     */
    size_t node_id() const { return m_changed_node_id; }

protected:
    size_t m_changed_node_id;
};


/**
 * @brief Added node change
 */
class AddNodeChange final : public AbstractSceneChange
{
public:
    AddNodeChange(size_t changed_parent_id, size_t node_id, size_t original_parent_id)
        : AbstractSceneChange(changed_parent_id)
        , m_node_id(node_id)
        , m_original_parent_id(original_parent_id)
    {}
    void roll_back(Scene& scene) override;

private:
    size_t m_node_id;
    size_t m_original_parent_id;
};

/**
 * @brief Changed material override
 */
class AddMaterialOverrideChange : public AbstractSceneChange
{
public:
    explicit AddMaterialOverrideChange(size_t node_id, const Render::Material* original = nullptr)
    : AbstractSceneChange(node_id)
    {
        if (original)
            m_original_material = std::make_unique<Render::Material>(*original);
    }

    void roll_back(Scene& scene) override;

private:
    std::unique_ptr<Render::Material> m_original_material;
};

/**
 * @brief Session containing changes in associated scene, that can be rolled back.
 */
class SceneChangeSession final
{
public:

    /**
     * @brief Builder to make and record changes to single node.
     */
    class NodeChangeBuilder
    {
    public:
        NodeChangeBuilder(SceneChangeSession& session, Node& node): m_session(session), m_node(node) {}

        /**
         * @brief Add specified node as child to node associated with this builder.
         * @param child Child node to be added
         * @return this node change builder instance.
         */
        NodeChangeBuilder& add_child(Node* child);

        /**
         * @brief Set material override to node associated with this builder.
         * @param m Material to be used as override
         * @return this node change builder instance.
         */
        NodeChangeBuilder& set_material_override(const Render::Material& m);

        /**
         * @brief Finish changes and get parent.
         *
         * This method just returns parent session as all changes are recorded to session
         * immediately. The purpose of this method is to allow seamless fluent API (aka builder API)
         * like this:
         * @code
         * Scene scene;
         * SceneChangeSession session(scene);
         *
         * Node* n1 = ...
         * Node* n2 = ...
         *
         * session
         *     .change(n1)
         *         .set_material(Material{}.set_uniform("uniform_color", ColorRGBA(1, 1, 1, 1))
         *     .done()
         *     .change(n2)
         *         .set_material(Material{}.set_uniform("uniform_color", ColorRGBA(1, 1, 1, 1)
         *     .done();
         *
         * @endcode
         * @return Parent SceneChangeSession
         */
        SceneChangeSession& done()
        {
            return m_session;
        }
    private:
        SceneChangeSession& m_session;
        Node& m_node;
    };


    explicit SceneChangeSession(Scene& scene) : m_scene(scene) {}

    /**
     * @brief Record (potentially multiple) changes to single node @p n
     * @param n Node to make changes to
     * @return NodeChangeBuilder instance to record all changes to node @p n with.
     */
    NodeChangeBuilder change(Node& n) { return {*this, n}; }

    /**
     * @name Roll back changes
     * @{
     */
    /**
     * @brief Roll back all recorded changes
     */
    void roll_back();

    /**
     * Roll back all changes related to node @p n
     * @param n Node to roll back changes to
     */
    void roll_back_node(Node* n);
    /** @} */

    /**
     * Get number of recorded changes for all nodes.
     * @return Numer of recorded changes
     */
    size_t size() const { return m_changes.size(); }

private:
    friend class NodeChangeBuilder;
    using SceneChangePtr = std::unique_ptr<AbstractSceneChange>;
    using SceneChanges = std::vector<SceneChangePtr>;

    Scene& m_scene;
    SceneChanges m_changes;
};

} // namespace Slic3r::App::Scene
