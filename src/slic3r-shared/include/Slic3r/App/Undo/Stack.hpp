#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Undo/BedSelectionState.hpp"
#include "Slic3r/App/Undo/ChannelId.hpp"
#include "Slic3r/App/Undo/ToolState.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Undo {

struct SnapshotData
{
    std::string data;
    std::vector<ChannelId> used_channels;
};

struct Snapshot
{
    Snapshot()
    {
        static std::size_t id_source{};
        id = ++id_source;
    }

    std::vector<SnapshotData> serialized_data;
    Biz::UndoSnapshotType type{Biz::UndoSnapshotType::None};
    std::size_t id{};
};

class SerializedDataStack;

struct LoadedSnapshot
{
    Domain::Model model;
    Biz::Scene::ObjectSelection object_selection;
    Scene::ToolType selected_tool_gizmo;
    Domain::Project::ConfigContainerList config_containers;
    BedSelectionState bed_selection_state;
    ToolsState tools_state;
};

class Stack
{
public:
    Stack();
    Stack(Stack&&) noexcept;
    Stack& operator=(Stack&&) noexcept;
    ~Stack();

    void take_snapshot(
        const Domain::Model& model,
        const Biz::Scene::ObjectSelection& object_selection,
        Scene::ToolType selected_tool_gizmo,
        const Domain::Project::ConfigContainerList& config_containers,
        const BedSelectionState& bed_selection_state,
        const ToolsState& tools_state,
        Biz::UndoSnapshotType type
    );

    LoadedSnapshot load_and_select_snapshot(
        Domain::SelectionId project_id,
        const Snapshot& snapshot,
        Domain::BedContainer& bed_container,
        Biz::Preset::PresetInteractor& preset_interactor
    );

    const std::vector<Snapshot>& get_snapshots() const;

    std::optional<std::size_t> get_selected_index() const;

    std::function<void(const Stack&)> on_change{[](const Stack&) {}};

private:
    std::unique_ptr<SerializedDataStack> m_stack;
    std::size_t m_one_past_selected_index{};
};
} // namespace Slic3r::App::Undo
