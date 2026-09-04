#pragma once

#include <vector>
#include <unordered_map>
#include <boost/variant/variant.hpp>

#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Render/Shader.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/GraphicsSettings.hpp"

#include "Slic3r/Domain/Types.hpp"



namespace Slic3r::App::Scene {

struct NodePickResult
{
    Node* node;
    RaycastResult cast;
};

struct ConstNodePickResult
{
    const Node* node;
    RaycastResult cast;
};

struct SceneColors {
    Domain::ColorRGBA bg_top_color_default;
    Domain::ColorRGBA bg_bottom_color_default;
    Domain::ColorRGBA bg_top_color_error;
    Domain::ColorRGBA bg_bottom_color_error;
};

using NodePickResults = std::vector<NodePickResult>;
using ConstNodePickResults = std::vector<ConstNodePickResult>;

class ISceneRenderCustomizer
{
public:
    virtual ~ISceneRenderCustomizer() = default;

    virtual void on_render_begin(Render::CommandBuffer& cmd_buf) {}
    virtual void on_layer_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_idx) {}
    virtual void on_opaque_pass_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) {}
    virtual void on_opaque_pass_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) {}
    virtual void on_transparent_pass_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) {}
    virtual void on_transparent_pass_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) {}
    virtual void on_layer_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_idx) {}
    virtual void on_render_end(Render::CommandBuffer& cmd_buf) {}
};

class MinimalSceneRenderCustomizer : public ISceneRenderCustomizer
{
public:
    void on_render_begin(Render::CommandBuffer& cmd_buf) override;
    void on_layer_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_idx) override {}
    void on_opaque_pass_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) override;
    void on_opaque_pass_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) override {}
    void on_transparent_pass_begin(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) override;
    void on_transparent_pass_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_index) override;
    void on_layer_end(Render::CommandBuffer& cmd_buf, RenderLayerId layer_idx) override {}
    void on_render_end(Render::CommandBuffer& cmd_buf) override {}
};

/**
 * @brief Scenegraph entrypoint
 *
 * Encapsulate scene tree made of Node and provides:
 * - rendering of 3D object, see render()
 * - rendering of 2D GUI overlay, see render_imgui()
 * - camera used for rendering (camera()) and its trackball manipulator (camera_trackball())
 * - ray picking, see pick_at()
 * - also provides common resource caching manager for rendering geometry (geometry_manager()),
 *   and in-memory geometry (triangle_mesh_manager())
 * .
 */
class Scene final :
    public WithListeners<ISceneChangedListener, IThumbnailRenderListener>,
    public IGraphicsSettingsChangedListener,
    public INodeChangedListener
{
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Scene(Scene&&) = default;

    /**
     * @name Access to root
     * @{
     */
    Node& root() { return m_root; }
    const Node& root() const { return m_root; }
    /** @} */

    /**
     * @name Node look-up by ID
     * @{
     */
    /**
     * @brief Quick mutable node look up by id.
     *
     * Gets node by its id.
     *
     * @param id Node id (see Node::id(), Node::set_id())
     * @return Pointer to node with given id or `nullptr` if no such node present in scene.
     */
    Node* node(size_t id)
    {
        auto it = m_nodes_by_id.find(id);
        return it == m_nodes_by_id.end() ? nullptr : it->second;
    }

    /**
     * @brief Quick immutable node look up by id.
     *
     * Gets node by its id.
     *
     * @param id Node id (see Node::id(), Node::set_id())
     * @return Constant pointer to node with given id or `nullptr` if no such node present in scene.
     */
    const Node* node(size_t id) const
    {
        auto it = m_nodes_by_id.find(id);
        return it == m_nodes_by_id.end() ? nullptr : it->second;
    }
    /** @} */

    /**
     * @name Camera and its trackball controller
     * @{
     */
    Camera& camera() { return m_camera; }
    const Camera& camera() const { return m_camera; }

    const CameraTrackballController& camera_trackball() const { return m_camera_trackball; }
    CameraTrackballController& camera_trackball() { return m_camera_trackball; }
    /** @} */

    /**
     * @name Associated resource managers
     * @{
     */
    Render::GeometryManager<std::string>& geometry_manager() { return m_geometry_manager; }
    TriangleMeshManager<std::string>& triangle_mesh_manager() { return m_trimesh_manager; }
    /** @} */

    /**
     * @name Rendering
     * @{
     */
    void render(Render::Device& device, Render::CommandBuffer& cmd_buffer, ISceneRenderCustomizer* customizer = &ms_default_customizer,
        Camera* camera_override = nullptr) const;
    void render_imgui(const Render::ScreenInfo& screen_info) const;
    /** @} */

    /**
     * @name Thumbnail render notification
     * @{
     */
    void notify_thumbnail_render_begin();
    void notify_thumbnail_render_end();
    /** @} */

    /**
     * @name Pick all nodes under cursor.
     * @{
     */
    /**
     * @brief Pick immutable nodes under mouse cursor.
     *
     * @param mouse_x, mouse_y Mouse cursor position (in logical coords)
     * @param [out] results List of nodes under cursor sorted by distance from camera (nearest first).
     * @param [out] out_ray If passed as non-`nullptr`, the ray associated with mouse cursor is set.
     * @return True if there is any hit, otherwise false.
     */
    bool pick_at(
        float mouse_x, float mouse_y, ConstNodePickResults& results, Ray* out_ray = nullptr
    ) const;

    /**
     * @brief Pick mutable nodes under mouse cursor.
     *
     * @param mouse_x, mouse_y Mouse cursor position (in logical coords)
     * @param [out] results List of nodes under cursor sorted by distance from camera (nearest first).
     * @param [out] out_ray If passed as non-`nullptr`, the ray associated with mouse cursor is set.
     * @return True if there is any hit, otherwise false.
     */
    bool pick_at(float mouse_x, float mouse_y, NodePickResults& results, Ray* out_ray = nullptr);
    /** @} */

    /**
     * @name Modify hierarchy
     * @{
     */
    /**
     * @brief Add node under root or given parent if specified.
     *
     * @note Scene takes over ownership of the @p node. The @p node gets deleted if its parent
     * or whole is deleted.
     *
     * @param node Node to be added as child.
     * @param parent Optional parent node, if not specified, @p node is added under scene root.
     */
    void add_child(Node* node, Node* parent = nullptr);

    /**
     * @brief Remove @p node (and its children) from its parent and destroy it.
     *
     * @return True if @p node was removed from its parent and destroyed otherwise false (i.e. not
     * found in its parent).
     */
    bool remove_child(Node* node);

    /**
     * @brief Remove and destroy **immediate** child node (or children nodes) satisfying @p predicate.
     *
     * @note Unlike detach_children() this method will destroy all children satisfying
     * @p predicate.
     *
     * @param predicate Predicate function to test node if it is supposed to be removed.
     * @param parent Optional parent node, if not specified the scene root is searched instead.
     * @return True if some nodes was destroyed otherwise false.
     */
    bool remove_children(const Node::NodePredicate& predicate, Node* parent = nullptr);

    /**
     * @brief Detach child node (or multiple nodes) satisfying @p predicate.
     *
     * @note Unlike remove_children() this method will return list of detached children without
     * destroying them.
     *
     * @note This function is not recursive.
     *
     * @param predicate Predicate function to test node if it is supposed to be detached.
     * @param parent Optional parent node, if not specified the scene root is searched instead.
     * @return List of detached nodes as `std::unique_ptr`, i.e. handing over ownership to the
     * method caller.
     */
    Node::NodeOwningList detach_children(
        const Node::NodePredicate& predicate, Node* parent = nullptr
    );

    /** @} */

    bool background_enabled() const { return m_background_enabled; }
    void set_background_enabled(bool enabled) { m_background_enabled = enabled; }

    bool use_background_error_color() const { return m_use_background_error_color; }
    void set_use_background_error_color(bool use) { m_use_background_error_color = use; }

    /**
     * @name Graphics settings-related methods
     * @{
     */
    static const GraphicsSettings& graphics_settings() { return s_graphics_settings; }
    static void add_graphics_settings_listener(IGraphicsSettingsChangedListener* listener) { 
        s_graphics_settings.add_listener<Scene::IGraphicsSettingsChangedListener>(listener);
    }
    static void set_shading_type(ShadingType shading_type) { s_graphics_settings.set_shading_type(shading_type); }
    /** @} */

    /**
     * @name Implementation of IGraphicsSettingsChangedListener public interface
     * @{
     */
    void on_shading_type_changed(ShadingType shading_type) override;
    /** @} */

    /**
     * @name Implementation of INodeChangedListener public interface
     * @{
     */
    void on_node_changed(Node* node) override;
    /** @} */

    /**
     * @name Shadows-related methods
     * @{
     */
    static void set_shadowsmap_size(int size) { s_graphics_settings.set_shadowsmap_size(size); }
    static void set_default_shadows_intensity() { s_graphics_settings.set_default_shadows_intensity(); }
    static void set_shadows_intensity(float intensity) { s_graphics_settings.set_shadows_intensity(intensity); }
    static void set_bed_model_cast_shadow(bool cast) { s_graphics_settings.set_bed_model_cast_shadow(cast); }
    /** @} */

    /**
     * @name Ambient occlusion-related methods
     * @{
     */
    static void set_ao_intensity(float intensity) { s_graphics_settings.set_ao_intensity(intensity); }
    static void set_ao_kernel_size(size_t size) { s_graphics_settings.set_ao_kernel_size(size); }
    static void set_ao_noise_size(size_t size) { s_graphics_settings.set_ao_noise_size(size); }
    static void set_ao_radius(float radius) { s_graphics_settings.set_ao_radius(radius); }
    static void set_ao_bias(float bias) { s_graphics_settings.set_ao_bias(bias); }
    static void set_ao_z_threshold(float z_threshold) { s_graphics_settings.set_ao_z_threshold(z_threshold); }
    static void set_ao_blur_filter_size(size_t size) { s_graphics_settings.set_ao_blur_filter_size(size); }

    static void set_default_ao_intensity() { s_graphics_settings.set_default_ao_intensity(); }
    static void set_default_ao_kernel_size() { s_graphics_settings.set_default_ao_kernel_size(); }
    static void set_default_ao_noise_size() { s_graphics_settings.set_default_ao_noise_size(); }
    static void set_default_ao_radius() { s_graphics_settings.set_default_ao_radius(); }
    static void set_default_ao_bias() { s_graphics_settings.set_default_ao_bias(); }
    static void set_default_ao_z_threshold() { s_graphics_settings.set_default_ao_z_threshold(); }
    static void set_default_ao_blur_filter_size() { s_graphics_settings.set_default_ao_blur_filter_size(); }
    /** @} */

    /**
     * @name Physically based rendering-related methods
     * @{
     */
    static void set_pbr_intensity(float intensity) { s_graphics_settings.set_pbr_intensity(intensity); }
    static void set_default_pbr_intensity() { s_graphics_settings.set_default_pbr_intensity(); }
    /** @} */

    static void set_scene_colors(const SceneColors& scene_colors) { s_scene_colors = scene_colors; }

    /**
     * @name Lighing-related methods
     * @{
     */

    const Lighting& lights() const { return m_lighting; }
    void set_lights(const Lighting& lights)
    {
        m_lighting = lights;
        validate_lights(m_lighting.lights);
    }
    void set_default_lights()
    {
        m_lighting.ambient_intensity = DEFAULT_LIGHT_AMBIENT;
        m_lighting.lights = DEFAULT_LIGHTS;
        validate_lights(m_lighting.lights);
    }
    void validate_lights(Lights& lights);

    /** @} */

    void log_nodes() const;

private:
    void register_node(Node* n);
    void unregister_node(Node* n);

    using NodeMaterial = std::pair<const Node*, Render::Material>;
    using NodeMaterials = std::vector<NodeMaterial>;
    NodeMaterials collect_nodes_with_material(const Node::NodePredicate& predicate) const;

    void init_screen_quad(Render::Device& device) const;
    void generate_ao_kernel(Render::Device& device) const;
    void generate_ao_noise(Render::Device& device) const;

    void render_background(Render::Device& device, Render::CommandBuffer& cmd_buffer) const;
    void render_shadowsmap_pass(Render::Device& device, const Camera& camera, ISceneRenderCustomizer* customizer) const;
    void render_shadows_receivers_pass(Render::Device& device, const Camera& camera, Render::CommandBuffer& cmd_buffer,
        ISceneRenderCustomizer* customizer) const;
    void render_no_shadows_pass(const Camera& camera, Render::CommandBuffer& cmd_buffer, ISceneRenderCustomizer* customizer) const;
    void render_ao_gbuffer_pass(Render::Device& device, const Camera& camera, ISceneRenderCustomizer* customizer,
        const Render::Rect& viewport, PBRParamsList& pbr_params_list) const;
    void render_ao_texture_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const;
    void render_ao_texture_hblur_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const;
    void render_ao_texture_vblur_pass(Render::Device& device, const Camera& camera, const Render::Rect& viewport) const;
    void render_ao_lighting_pass(Render::Device& device, const Camera& camera, Render::CommandBuffer& cmd_buffer,
        const Render::Rect& viewport, const PBRParamsList& pbr_params_list) const;

private:
    using NodeIdLookUp = std::unordered_map<size_t, Node*>;

    Node m_root;
    Camera m_camera;
    CameraTrackballController m_camera_trackball;
    NodeIdLookUp m_nodes_by_id;
    Render::GeometryManager<std::string> m_geometry_manager{"scene_geometry"};
    TriangleMeshManager<std::string> m_trimesh_manager{"scene_mesh"};

    Lighting m_lighting;
    bool m_background_enabled{true};
    bool m_use_background_error_color{false};

    mutable Render::Geometry* m_screen_quad{nullptr};

    static GraphicsSettings s_graphics_settings;
    static MinimalSceneRenderCustomizer ms_default_customizer;
    static SceneColors s_scene_colors;
};

} // namespace Slic3r::App::Scene
