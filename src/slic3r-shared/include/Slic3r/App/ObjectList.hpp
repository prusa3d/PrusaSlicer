#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "MultiSelections.hpp"

#include <Slic3r/Domain/ElementRef.hpp>

#include <set>
#include <string>

namespace Slic3r::Domain {
class Model;
class ModelObject;
class ModelVolume;
struct BedInstance;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {
class SceneInteractor;
struct ObjectSelection;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Render {
class ImguiRender;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class IGizmoController;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {
struct BedThumbnailTexture;
using BedThumbnailTextures = std::vector<BedThumbnailTexture>;
} // namespace Slic3r::App::Plater

namespace Slic3r::App {
class ObjectListWindow;

class ObjectList : public Yoga::Item
{
public:
    enum class Mode
    {
        Plater,
        Preview,
    };

    struct Callbacks
    {
        std::function<void(Domain::Vec2f mouse_pos, Domain::SelectionId config_container_id)>
            show_context_menu;
    };

    Callbacks& callbacks();

    ObjectList(Biz::ProjectInteractor* project_interactor, ObjectList::Mode mode);
    void init(Biz::ProjectInteractor* project_interactor, Mode mode);

    void set_bed_instance_icons(const Plater::BedThumbnailTextures& icons);
    void set_gizmo_controller(Scene::IGizmoController* controller);

private:
    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void process_dragging_start();
    void update_selection_from_scene();
    bool render_list(Domain::Vec2f size);
    bool render_config_containers();
    void render_group_name(const std::string& name);
    void render_all_beds_node();
    bool render_out_of_beds();
    void render_drop_target_area();
    bool render_bed_node(
        const Domain::BedInstance* bed,
        size_t config_container_id,
        bool can_be_deleted
    );
    bool render_wipe_tower_node(const Domain::BedInstance* bed);
    bool render_object_node(
        const Domain::ModelObject* object,
        std::optional<size_t> config_container_id = std::nullopt,
        const Domain::BedInstance* bed            = nullptr,
        bool is_sla_config                        = false
    );
    bool render_connectors_node(const Domain::ModelObject* object, size_t bed_id);
    bool render_volumes(
        const Domain::ModelObject* object,
        size_t bed_id,
        bool is_sla_config,
        std::optional<size_t> config_container_id
    );
    void render_volume_node(
        const Domain::ModelVolume* volume,
        const Domain::ElementRef& sel_element,
        bool is_selected,
        bool is_sla_config,
        std::optional<size_t> config_container_id
    );
    bool render_instances_node(const Domain::ModelObject* object, const Domain::BedInstance* bed);
    bool
    render_instances(const Domain::ModelObject* object, const std::set<size_t>& instances_on_bed);
    void render_instance_node(const Domain::ModelObject* object, size_t inst_id, bool is_selected);
    void render_infos_node(const Domain::ModelObject* object, bool is_sla_config);

    void render_edited(const char* init_name, const Domain::ElementRef& sel_element);
    void render_printable_icon(const Domain::ElementRef& sel_element, bool is_printable);
    bool render_delete_button(const std::string& id);
    void render_extruder_marker(
        std::optional<size_t> config_container_id,
        const Domain::ModelObject* object = nullptr,
        const Domain::ModelVolume* volume = nullptr
    );
    void render_slicing_state_marker(size_t bed_instance_id);
    void render_infos_selectable(
        const std::set<Render::Icon>& infos,
        const Domain::ModelObject* object,
        bool force_render
    );

    bool tree_node(
        const char* str_id,
        ImGuiTreeNodeFlags flags,
        const std::string& label,
        bool add_overrides_marker = false,
        unsigned long long tex_id = 0,
        ImVec2 icon_size          = {0.0f, 0.0f}
    );

    void clear_all_ms();
    void invalidate_bed_selection();

    bool handle_selection(const Domain::ElementRef& id);
    void handle_dragging(const Domain::ElementRef& id);
    void force_select_whole_object(const Domain::ModelObject* object);

    void propagate_selection();
    void propagate_name_editing(const Domain::ElementRef& id, const std::string& new_name);
    void propagate_printable(const Domain::ElementRef& id, bool is_printable);
    void ask_extract_selected_instances();
    void extruder_clicked(const Domain::ElementRef& sel_element, bool is_bed);
    void show_layer_ranges(const Domain::ElementRef& id);
    void show_gizmo(const Domain::ElementRef& id, Render::Icon gizmo_id);
    void add_bed(size_t config_container_id);
    void remove_bed(size_t config_container_id, size_t bed_id);

    void set_horizontal_padding(float horizontal_padding);

    struct ProjectContext;
    ProjectContext& selected_project_context();
    const ProjectContext& selected_project_context() const;
    Yoga::Vec2f get_item_size() override;

private:
    struct ProjectContext
    {
        const Domain::Model* model{nullptr};
        MultiSelections instances_ms;
        MultiSelections volumes_ms;
        size_t edited_node_id{0};
        bool show_details{false};

        bool is_dragging{false};
        std::set<Domain::ElementRef> selected_items; // Track selected item IDs
        size_t selected_container_id{0};
        size_t selected_bed_instance_id{0};

        Plater::BedThumbnailTextures bed_instance_icons;
    };

    using ProjectContexts    = Biz::ProjectScoped<ProjectContext>;
    using ProjectContextsPtr = std::unique_ptr<ProjectContexts>;
    using DeferredActionList = std::list<std::function<void()>>;

    Biz::ProjectInteractor* m_project_interactor{nullptr};
    Biz::Scene::SceneInteractor* m_scene_interactor{nullptr};
    Scene::IGizmoController* m_gizmo_controller{nullptr};
    Mode m_mode{Mode::Plater};
    ProjectContextsPtr m_project_contexts;

    bool m_is_edit_name_input_hovered{false};
    float m_horizontal_padding{10.f};

    Domain::Vec2f m_inner_padding;
    ImGuiMultiSelectFlags m_multi_selection_flags;
    ImGuiTreeNodeFlags m_node_flags;
    ImGuiTableFlags m_table_flags;
    float m_state_column_width{0.f};
    int m_total_beds_cnt{1};
    float m_progress_column_width{0.f};
    DeferredActionList m_deferred_actions;

    Callbacks m_callbacks;
    Yoga::Vec2f m_size{0.f, 150.f};

    friend class ObjectListWindow;
};

} // namespace Slic3r::App
