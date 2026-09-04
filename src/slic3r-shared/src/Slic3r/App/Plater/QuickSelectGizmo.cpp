#include "Slic3r/App/Plater/QuickSelectGizmo.hpp"

#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Plater/GizmoNodeTag.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include "Slic3r/Domain/Color.hpp"

#include <tracy/Tracy.hpp>

using Slic3r::Domain::SquareMatrix4f;
using Slic3r::Domain::ColorRGBA;
using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

RectangleSelection::RectangleSelection(const Render::ScreenInfo& screen_info, Render::Device& device, Scene::ISceneProvider& scene_provider,
    Biz::Scene::SceneInteractor& scene_interactor)
    : m_screen_info(screen_info)
    , m_device(device)
    , m_scene_provider(scene_provider)
    , m_scene_interactor(scene_interactor)
    , m_geometry(device)
    , m_picker(device)
{
    m_material = Render::Material{}
        .set_shader(m_device.context().shader_manager().shader("flat"));

    Render::GeometryBuilder<Render::VertexP3> builder;
    builder
        .add_vertex({ {0.0f, 0.0f, 0.0f} })
        .add_vertex({ {1.0f, 0.0f, 0.0f} })
        .add_vertex({ {1.0f, 1.0f, 0.0f} })
        .add_vertex({ {0.0f, 1.0f, 0.0f} })
        .add_draw_command({ Render::PrimitiveType::LineLoop, 0, 4, m_material });
    builder.update(m_geometry);
}

void RectangleSelection::activate(Type type, const MousePosition& initial_mouse_pos)
{
    m_active = true;
    m_already_processed = false;
    m_type = type;
    m_initial_mouse_pos = initial_mouse_pos;

    ColorRGBA color;
    switch (m_type)
    {
    case Type::Replace:
    case Type::Add:    { color = ColorRGBA(0.3f, 1.0f, 0.3f, 1.0f); break; }
    case Type::Remove: { color = ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f); break; }
    default:           { color = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f); break; }
    }

    m_material.set_uniform("uniform_color", color);
}

void RectangleSelection::update(const MousePosition& curr_mouse_pos)
{
    Domain::Vec2f initial_mouse_pos = {
        m_screen_info.mouse_to_screen(m_initial_mouse_pos[0]),
        m_screen_info.mouse_to_screen(m_initial_mouse_pos[1])
    };

    Domain::Vec2f final_mouse_pos = {
        m_screen_info.mouse_to_screen(curr_mouse_pos[0]),
        m_screen_info.mouse_to_screen(curr_mouse_pos[1])
    };

    Render::Rect rect{
        int(std::min(initial_mouse_pos.x(), final_mouse_pos.x())),
        int(std::min(initial_mouse_pos.y(), final_mouse_pos.y())),
        int(std::abs(final_mouse_pos.x() - initial_mouse_pos.x())),
        int(std::abs(final_mouse_pos.y() - initial_mouse_pos.y()))
    };

    float scr_w = m_screen_info.physical_width();
    float scr_h = m_screen_info.physical_height();

    DEBUG_ASSERT(scr_w > 0.0f && scr_h > 0.0f);

    float left   = 2.0f * (rect.x / scr_w - 0.5f);
    float right  = 2.0f * ((rect.x + rect.width) / scr_w - 0.5f);
    float top    = -2.0f * (rect.y / scr_h - 0.5f);
    float bottom = -2.0f * ((rect.y + rect.height) / scr_h - 0.5f);

    SquareMatrix4f vm = SquareMatrix4f::Identity();
    vm(0, 0) = right - left;
    vm(1, 1) = top - bottom;
    vm(0, 3) = left;
    vm(1, 3) = bottom;
    m_material.set_uniform("projection_view_model_matrix", vm);

    m_picker.pick(m_scene_provider.scene(), m_screen_info, rect);
    m_contained_nodes = m_picker.contained_nodes();
    m_defined = m_initial_mouse_pos != curr_mouse_pos;
}

template <typename T, typename F>
void unique(std::vector<T>& range, F less_than) {
    std::sort(
        range.begin(),
        range.end(),
        less_than
    );
    range.erase(
        std::unique(
            range.begin(),
            range.end(),
            [&](const T& a, const T& b) { return !less_than(a, b) && !less_than(b, a); }
        ),
        range.end()
    );
}

static Scene::Node::NodeList extract_instance_or_wipe_tower_nodes(const Scene::Node::NodeList& nodes)
{
    Scene::Node::NodeList ret;
    for (auto n : nodes) {
        const auto tag{n->tag_of_type<SceneNodeTag>()};
        if (!tag) {
            continue;
        }
        if (tag->is_wipe_tower()) {
            Scene::Node* node_to_append{n};
            while (true) {
                Scene::Node* new_parent_node{node_to_append->parent()};
                if (!new_parent_node) {
                    break;
                }
                SceneNodeTag* new_parent_node_tag{new_parent_node->tag_of_type<SceneNodeTag>()};
                if (!new_parent_node_tag
                    || new_parent_node_tag->wipe_tower_id != tag->wipe_tower_id)
                {
                    break;
                }
                node_to_append = new_parent_node;
            }
            ret.emplace_back(node_to_append);
        } else if (tag->volume_id != 0) {
            ret.emplace_back(n->parent());
        }
    }
    unique(ret, [](const Scene::Node* a, const Scene::Node* b) { return a->id() < b->id(); });
    return ret;
}

static void promote_wipe_tower_nodes(Scene::Node::NodeList& nodes, Scene::Scene& scene)
{
    std::vector<Domain::SlicingId> wipe_tower_ids;
    std::erase_if(nodes, [&](const Scene::Node* node){
        const auto tag{node->tag_of_type<SceneNodeTag>()};
        if (tag && tag->is_wipe_tower()) {
            wipe_tower_ids.push_back(tag->wipe_tower_id);
            return true;
        }
        return false;
    });

    Scene::Node::NodeList wipe_tower_nodes;
    for (Domain::SlicingId wipe_tower_id : wipe_tower_ids) {
        scene.root().query(
            [&](const Scene::Node* n)
            {
                auto tag{n->tag_of_type<SceneNodeTag>()};
                if (tag == nullptr) {
                    return false;
                }
                return tag->wipe_tower_id == wipe_tower_id;
            },
            wipe_tower_nodes
        );
    }

    unique(
        wipe_tower_nodes,
        [](const Scene::Node* a, const Scene::Node* b) { return a->id() < b->id(); }
    );
    nodes.insert(nodes.end(), wipe_tower_nodes.begin(), wipe_tower_nodes.end());
}

static bool are_all_nodes_from_same_instance(const Scene::Node::NodeList& nodes)
{
    if (nodes.empty())
        return false;

    const auto first_tag{nodes.front()->tag_of_type<SceneNodeTag>()};
    if (!first_tag || first_tag->volume_id == 0) {
        return false;
    }

    size_t inst_id = nodes.front()->parent()->id();
    return std::all_of(nodes.begin(), nodes.end(), [&](const Scene::Node* n) {
        return n->parent()->id() == inst_id;
    });
}

static bool contains_any_selectable_part(const Scene::Node::NodeList& nodes)
{
    if (nodes.empty()) {
        return false;
    }
    return std::any_of(
        nodes.begin(),
        nodes.end(),
        [&](const Scene::Node* n)
        {
            const auto tag{n->tag_of_type<SceneNodeTag>()};
            if (tag == nullptr) {
                return false;
            }
            return tag->volume_type == Domain::ModelVolumeType::MODEL_PART
                || tag->is_wipe_tower();
        }
    );
}

bool RectangleSelection::update_selection(SelectionHandler& selection_handler)
{
    DEBUG_ASSERT(m_active);
    if (!m_defined)
        return false;

    Scene::Node::NodeList nodes = m_contained_nodes;

    if (m_type == Type::Remove) {
        if (m_scene_interactor.object_selection().mode == Biz::Scene::SelectionMode::Instance)
            nodes = extract_instance_or_wipe_tower_nodes(nodes);
    }
    else if (m_type == Type::Replace) {
        if (std::any_of(
                nodes.begin(),
                nodes.end(),
                [&](const Scene::Node* n)
                {
                    auto tag{n->tag_of_type<SceneNodeTag>()};
                    if (!tag) {
                        return false;
                    }
                    return tag->volume_type == Domain::ModelVolumeType::MODEL_PART
                        || tag->is_wipe_tower();
                }
            ))
            nodes = extract_instance_or_wipe_tower_nodes(nodes);
        else if (nodes.size() != 1) {
            if (m_scene_interactor.object_selection().mode == Biz::Scene::SelectionMode::Volume &&
                !are_all_nodes_from_same_instance(nodes)) {
                nodes.clear();
                selection_handler.clear_selection();
            }
        }
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        if (m_type == Type::Remove)
            selection_handler.mark_unselected(*nodes[i]);
        else
            selection_handler.mark_selected(*nodes[i], m_type == Type::Replace && i == 0);
    }

    m_already_processed = true;
    return true;
}

void RectangleSelection::render(Render::CommandBuffer& cmd_buffer)
{
    if (!m_active || !m_defined || m_already_processed)
        return;
    cmd_buffer.bind_and_draw(m_geometry, m_material);
}

static void promote_hover_data_to_full_instances(Scene::Node::NodeList& nodes, Scene::Scene& scene)
{
    std::vector<std::size_t> instance_ids;
    std::erase_if(nodes, [&](const Scene::Node* node){
        const auto tag{node->tag_of_type<SceneNodeTag>()};
        if (tag && tag->instance_id != 0) {
            instance_ids.push_back(tag->instance_id);
            return true;
        }
        return false;
    });
    unique(instance_ids, std::less<std::size_t>{});

    Scene::Node::NodeList volume_nodes;
    for (std::size_t instance_id: instance_ids) {
        scene.root().query(
            [&](const Scene::Node* n)
            {
                auto tag{n->tag_of_type<SceneNodeTag>()};
                if (tag == nullptr) {
                    return false;
                }
                if (tag->volume_id == 0) {
                    return false;
                }
                return tag->instance_id == instance_id;
            },
            volume_nodes
        );
    }
    unique(
        volume_nodes,
        [](const Scene::Node* a, const Scene::Node* b) { return a->id() < b->id(); }
    );

    nodes.insert(nodes.end(), volume_nodes.begin(), volume_nodes.end());
}

static void refine_hover_data(
    HoverData& hover_data,
    bool modifier_pressed,
    const Biz::Scene::ObjectSelection& selection,
    Scene::Scene& scene
)
{
    DEBUG_ASSERT(hover_data.nodes.size() == 1);
    if (hover_data.nodes.size() != 1)
        return;

    promote_wipe_tower_nodes(hover_data.nodes, scene);

    const auto first_node = hover_data.nodes.front();
    const auto tag = first_node->tag_of_type<SceneNodeTag>();
    if (selection.mode == Biz::Scene::SelectionMode::Volume && !selection.empty()) {
        Domain::ElementRef ref{ tag->object_id, tag->instance_id, 0 };
        if (std::any_of(selection.elements.begin(), selection.elements.end(),
            [&](const Domain::ElementRef& r) { return r.is_part_of(ref); })) {
            if (tag->volume_type == Domain::ModelVolumeType::MODEL_PART)
                promote_hover_data_to_full_instances(hover_data.nodes, scene);
        }
        else
            hover_data.nodes = Scene::Node::NodeList();
    }
    else if (selection.mode == Biz::Scene::SelectionMode::Instance && !selection.empty()) {
        if (tag->volume_type == Domain::ModelVolumeType::MODEL_PART || modifier_pressed)
            promote_hover_data_to_full_instances(hover_data.nodes, scene);
    }
    else if (tag->volume_type == Domain::ModelVolumeType::MODEL_PART ||
        (selection.mode == Biz::Scene::SelectionMode::Instance && hover_data.type == HoverType::Unselect))
        promote_hover_data_to_full_instances(hover_data.nodes, scene);
}

static void refine_rectangle_hover_data(
    HoverData& hover_data,
    const Biz::Scene::ObjectSelection& selection,
    RectangleSelection::Type type,
    Scene::Scene& scene
)
{
    if (type == RectangleSelection::Type::Remove && !selection.empty()) {
        // filters out unselected nodes
        Scene::Node::NodeList nodes;
        nodes.reserve(hover_data.nodes.size());
        for (auto n : hover_data.nodes) {
            const auto tag = n->tag_of_type<SceneNodeTag>();
            if (selection.is_selected(
                    Domain::ElementRef(tag->object_id, tag->instance_id, tag->volume_id)
                )
                || selection.is_selected(Domain::ElementRef(tag->wipe_tower_id)))
            {
                nodes.emplace_back(n);
            }
        }
        hover_data.nodes = nodes;
    }

    if (hover_data.nodes.empty())
        return;

    bool promote_to_full_instance = false;

    if (selection.mode == Biz::Scene::SelectionMode::Volume && !selection.empty()) {
        size_t inst_id = selection.elements.front().instance_id;
        if (type == RectangleSelection::Type::Remove) {
            Scene::Node::NodeList nodes;
            nodes.reserve(hover_data.nodes.size());
            for (auto n : hover_data.nodes) {
                if (n->tag_of_type<SceneNodeTag>()->instance_id == inst_id)
                    nodes.emplace_back(n);
            }
            hover_data.nodes = nodes;
            return;
        }
        else {
            if (std::any_of(
                    hover_data.nodes.begin(),
                    hover_data.nodes.end(),
                    [&](const Scene::Node* n)
                    { return n->tag_of_type<SceneNodeTag>()->instance_id != inst_id; }
                ))
            {
                hover_data.nodes = Scene::Node::NodeList();
                return;
            }

            if (contains_any_selectable_part(hover_data.nodes))
                promote_to_full_instance = true;
            else
                return;
        }
    }
    else
        promote_to_full_instance = type == RectangleSelection::Type::Add ||
                                   type == RectangleSelection::Type::Remove ||
                                   contains_any_selectable_part(hover_data.nodes);

    promote_wipe_tower_nodes(hover_data.nodes, scene);

    if (promote_to_full_instance)
        promote_hover_data_to_full_instances(hover_data.nodes, scene);
    else if (!are_all_nodes_from_same_instance(hover_data.nodes))
        hover_data.nodes = Scene::Node::NodeList();
}

static bool contains_gizmo_nodes(const Scene::NodePickResults& pick_results)
{
    auto gizmo_it = std::find_if(pick_results.begin(), pick_results.end(),
        [](const auto& item) {
            const Scene::Node& n = *item.node;
            return n.has_tag_of_type<TranslationGizmoNodeTag>() ||
                   n.has_tag_of_type<RotationGizmoNodeTag>() ||
                   n.has_tag_of_type<ScaleGizmoNodeTag>();
        }
    );
    return gizmo_it != pick_results.end();
}

static Scene::NodePickResults::const_iterator find_potentially_selected_node(
    const Scene::NodePickResults& results
)
{
    return std::ranges::find_if(
        results,
        [&](const auto& item)
        {
            const Scene::Node& n = *item.node;
            return n.has_tag_of_type<SceneNodeTag>();
        }
    );
}

QuickSelectGizmo::~QuickSelectGizmo()
{
    clear_timers();
}

Scene::GizmoActivationState QuickSelectGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ZoneScoped;

    using namespace std::chrono_literals;
    const auto& evt = ctx.mouse_event();
    auto type = evt.type();

    const auto& selection = m_scene_interactor.object_selection();

    bool shift_down = Platform::shift_down(evt.key_modifiers());
    bool ctrl_down  = Platform::ctrl_down(evt.key_modifiers());
    bool alt_down   = Platform::alt_down(evt.key_modifiers());

    constexpr static Clock::duration max_click_duration = Clock::duration {500ms};

    const auto it = find_potentially_selected_node(ctx.pick_results());

    // replace the following line with
    // bool modifier_pressed = ctrl_down;
    // if you want to use [Ctrl] instead of [Shift]
    bool modifier_pressed   = shift_down;
    bool already_selected   = false;
    const SceneNodeTag* tag = nullptr;
    if (it != ctx.pick_results().end()) {
        tag = it->node->tag_of_type<SceneNodeTag>();
        if (tag) {
            already_selected =
                selection.is_selected({tag->object_id, tag->instance_id, tag->volume_id})
                || selection.is_selected({tag->object_id, tag->instance_id, 0});
        }
    }

    auto& timer_queue = Biz::Platform::PlatformServices::instance().timer_queue();

    if (type == Platform::MouseEvent::Type::ButtonDown) {
        if (evt.button() != Platform::MouseButton::Left) {
            if (evt.button() == Platform::MouseButton::Right && !already_selected) {
                // continue process it as a LeftClick -> we need to select this node before context menu showing
            } else {
                m_processing = false;
                return Scene::GizmoActivationState::Inactive;
            }
        }

        m_pending_single_click = true;
        m_pending_click_node   = it == ctx.pick_results().end() ? Domain::INVALID_ID : it->node->id();

        RectangleSelection::Type rect_sel_type = (shift_down && ctrl_down) ?
            RectangleSelection::Type::Add :
            shift_down ? RectangleSelection::Type::Replace :
            alt_down   ? RectangleSelection::Type::Remove :
                         RectangleSelection::Type::Undefined;
        if (rect_sel_type == RectangleSelection::Type::Add && selection.empty())
            rect_sel_type = RectangleSelection::Type::Replace;

        if (rect_sel_type != RectangleSelection::Type::Undefined)
            m_rectangle_selection.activate(rect_sel_type, {evt.x(), evt.y()});

        if (m_rectangle_selection.is_active() && !m_rectangle_selection.is_already_processed())
            return Scene::GizmoActivationState::Probing;
        else {
            m_click_start = Clock::now();
            m_processing  = true;
        }

        return (only_active && m_hover_data.nodes.empty()) ? Scene::GizmoActivationState::Active :
                                                             Scene::GizmoActivationState::Probing;
    } else if (type == Platform::MouseEvent::Type::DoubleClick) {
        if (evt.button() != Platform::MouseButton::Left) {
            // process just left DoubleClick
            m_processing = false;
            return Scene::GizmoActivationState::Inactive;
        }

        m_pending_single_click = false;
        m_pending_click_node   = Domain::INVALID_ID;

        // Clear timers with postponed processing of ButtonUp events
        // They are no longer needed
        clear_timers();

        // complete click
        if (selection.empty() && it == ctx.pick_results().end()) {
            return Scene::GizmoActivationState::Inactive;
        }
        on_double_click(it == ctx.pick_results().end() ? nullptr : it->node, modifier_pressed);
        return Scene::GizmoActivationState::Done;
    }

    if (!m_rectangle_selection.is_active()) {
        if (m_processing && Clock::now() - m_click_start >= max_click_duration) {
            m_processing = false;
            clear_timers();
            SPDLOG_INFO("QuickSelectGizmo activation timed out");
        }
    }

    if (type == Platform::MouseEvent::Type::Move) {
        if (m_rectangle_selection.is_active() && !m_rectangle_selection.is_already_processed()) {
            m_rectangle_selection.update({evt.x(), evt.y()});
            HoverData hover_data{
                shift_down ? HoverType::Select : HoverType::Unselect,
                m_rectangle_selection.contained_nodes()
            };
            refine_rectangle_hover_data(
                hover_data,
                selection,
                m_rectangle_selection.type(),
                m_scene_provider.scene()
            );
            m_rectangle_selection.set_contained_nodes(hover_data.nodes);
            invoke_hover_changed(hover_data);
            return Scene::GizmoActivationState::Active;
        } else {
            if (contains_gizmo_nodes(ctx.pick_results())) {
                HoverData hover_data = {HoverType::Select, Scene::Node::NodeList()};
                invoke_hover_changed(hover_data);
                return Scene::GizmoActivationState::Inactive;
            }

            HoverType type =
                (modifier_pressed && already_selected) ? HoverType::Unselect : HoverType::Select;
            Scene::Node::NodeList nodes;
            if (tag != nullptr)
                nodes = Scene::Node::NodeList{it->node};

            HoverData hover_data = {type, nodes};
            if (!hover_data.nodes.empty())
                refine_hover_data(
                    hover_data,
                    modifier_pressed,
                    selection,
                    m_scene_provider.scene()
                );
            invoke_hover_changed(hover_data);
            return Scene::GizmoActivationState::Inactive;
        }
    } else if (type == Platform::MouseEvent::Type::ButtonUp) {
        // First, deactivate rectangle selection if it is active
        // to avoid drawing the rectangle on Move after ButtonUp
        if (m_rectangle_selection.is_active()) {
            if (m_rectangle_selection.is_already_processed()) {
                m_rectangle_selection.deactivate();
                return Scene::GizmoActivationState::Done;
            }

            bool res = m_rectangle_selection.update_selection(m_selection_handler);
            m_rectangle_selection.deactivate();
            if (res)
                return Scene::GizmoActivationState::Done;
        }

        // Process ButtonUp
        if (m_pending_single_click) {
            std::optional<size_t> node_id{std::nullopt};
            if (m_pending_click_node != Domain::INVALID_ID) {
                node_id = m_pending_click_node;
                m_pending_click_node = Domain::INVALID_ID;
            }
            // This is the first ButtonUp after ButtonDown, before a possible DoubleClick
            // We need to postpone its processing slightly, because a DoubleClick may follow
            // There may be cases where, using a modifier, multiple objects are selected quickly
            // That is why a vector of timers is used instead of a single timer
            m_up_timer_ids.push_back(timer_queue.set_timer(
                std::chrono::milliseconds(150),
                [this, node_id, is_modifier_pressed = modifier_pressed]()
                {
                    on_mouse_up(node_id, is_modifier_pressed);
                    Biz::Platform::PlatformServices::instance()
                        .render_request_handler()
                        .request_render();
                }
            ));
        }
    } else if (evt.type() == Platform::MouseEvent::Type::Leave) {
        if (m_rectangle_selection.is_active())
            return Scene::GizmoActivationState::Active;
    }
    return Scene::GizmoActivationState::Inactive;
}

void QuickSelectGizmo::camera_updated(const Scene::Camera& cam)
{
    auto results = m_gizmo_controller.repick();
    Scene::Node::NodeList nodes;
    if (const auto it = find_potentially_selected_node(results);
        it != results.end() && !contains_gizmo_nodes(results))
    {
        nodes.push_back(it->node);
    }
    invoke_hover_changed(HoverData{HoverType::Select, nodes});
}

void QuickSelectGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    Render::ScopedDebugGroup event_gizmo_manager("Quick Select Gizmo", cmd_buffer);
    m_rectangle_selection.render(cmd_buffer);
}

void QuickSelectGizmo::on_keyboard(Scene::GizmoKeyEventContext& ctx)
{
    const Platform::KeyboardEvent& evt = ctx.keyboard_event();
    if (evt.is_repeat())
        return;

    Platform::KeyCode code = evt.code();
    // replace the following line with
    // bool is_modifier_key = Platform::is_ctrl(code);
    // if you want to use [Ctrl] instead of [Shift]
    bool is_modifier_key = Platform::is_shift(code);
    if (!is_modifier_key && !Platform::is_alt(code))
        return;

    if (m_rectangle_selection.is_active()) {
        if (evt.type() == Platform::KeyboardEvent::Type::KeyUp)
            m_rectangle_selection.update_selection(m_selection_handler);
    }
    else {
        if (is_modifier_key) {
            HoverData hover_data = m_hover_data;
            bool modifier_pressed = evt.type() == Platform::KeyboardEvent::Type::KeyDown;
            hover_data.type = modifier_pressed ? HoverType::Unselect : HoverType::Select;
            if (hover_data.nodes.size() == 1)
                refine_hover_data(hover_data, modifier_pressed, m_scene_interactor.object_selection(), m_scene_provider.scene());
            if (hover_data.type == HoverType::Unselect && !hover_data.nodes.empty()) {
                // override type to comply with old PrusaSlicer behavior
                const auto tag = hover_data.nodes.front()->tag_of_type<SceneNodeTag>();
                if (!m_scene_interactor.object_selection().is_selected({ tag->object_id, tag->instance_id, tag->volume_id }))
                    hover_data.type = HoverType::Select;
            }
            invoke_hover_changed(hover_data);
        }
    }
}

void QuickSelectGizmo::on_cycle_prepare()
{
    m_processing = false;
    m_rectangle_selection.deactivate();
}

void QuickSelectGizmo::invoke_hover_changed(const HoverData& hover_data)
{
    if (m_hover_data != hover_data) {
        m_hover_data = hover_data;
        invoke_listeners<IHoverChangedListener>(
            [&](IHoverChangedListener* l) { l->on_hover_changed(m_hover_data); }
        );
    }
}

void QuickSelectGizmo::on_mouse_up(std::optional<size_t> node_id, bool modifier_pressed)
{
    const auto& selection = m_scene_interactor.object_selection();
    if (!node_id) {
        if (!selection.empty() && !modifier_pressed) {
            m_selection_handler.clear_selection();
        }
        return;
    }

    Scene::Node* node = m_scene_provider.scene().node(node_id.value());
    if (!node) {
        // this node doesn't exist for this time
        return;
    }

    if (selection.empty()) {
        m_selection_handler.mark_selected(*node);
    } else {
        if (modifier_pressed) {
            const SceneNodeTag* tag = node->tag_of_type<SceneNodeTag>();
            bool already_selected =
                tag && selection.is_selected({tag->object_id, tag->instance_id, 0});

            if (already_selected)
                m_selection_handler.mark_unselected(*node);
            else
                m_selection_handler.mark_selected(*node, false);
        } else {
            m_selection_handler.mark_selected(*node);
        }
    }
}

void QuickSelectGizmo::on_double_click(Scene::Node* node, bool modifier_pressed)
{
    const auto& selection = m_scene_interactor.object_selection();
    if (!node) {
        if (!selection.empty() || !modifier_pressed) {
            m_selection_handler.clear_selection();
        }
        return;
    }

    if (selection.empty()) {
        m_selection_handler.mark_selected(*node, true, false, true);
        return;
    }
    bool already_selected = false;
    if (const SceneNodeTag* tag = node->tag_of_type<SceneNodeTag>()) {
        already_selected =
            selection.is_selected({tag->object_id, tag->instance_id, tag->volume_id});
    }

    bool can_modify_multi_part_selection = (selection.mode == Biz::Scene::SelectionMode::Volume
                                            && can_be_added_to_object_selection(*node, selection))
        || (selection.mode == Biz::Scene::SelectionMode::Instance
            && selection.elements.size() == 1
            && already_selected);

    if (modifier_pressed && can_modify_multi_part_selection) {
        if (already_selected)
            m_selection_handler.mark_unselected(*node, true);
        else
            m_selection_handler.mark_selected(*node, false, false, true);
        return;
    }

    m_selection_handler.clear_selection();
    m_selection_handler.mark_selected(*node, true, false, true);
}

void QuickSelectGizmo::clear_timers()
{
    auto& timer_queue = Biz::Platform::PlatformServices::instance().timer_queue();
    for (auto& id : m_up_timer_ids) {
        if (timer_queue.is_timer_running(id)) {
            timer_queue.cancel_timer(id);
        }
    }
    m_up_timer_ids.clear();
    m_pending_click_node = Domain::INVALID_ID;
}

} // namespace Slic3r::App::Plater
