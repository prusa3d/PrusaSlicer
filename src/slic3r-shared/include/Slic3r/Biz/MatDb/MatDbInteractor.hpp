#pragma once
#include "Slic3r/Biz/IMdb.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include "Slic3r/Biz/MatDb/MatDbClient.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}
namespace Slic3r::Biz::Preset {
class PresetInteractor;
}
namespace Slic3r::Biz::MatDb {

struct PresetMatch
{
    std::string preset_id;
    std::string preset_name;
    size_t rank{0};
};

using PresetMatches = std::vector<PresetMatch>;

struct MatDbItem
{
    Domain::MatDb::Material material;
    PresetMatches preset_matches;
};

struct MatDbInteractorProjectContexts;

class MatDbInteractor final :
    public Preset::IPresetChangedListener,
    public IMdb
{
public:
    MatDbInteractor(Domain::Workbench& workbench, ProjectInteractor& project_interactor);
    ~MatDbInteractor() final;

    void init_client();

    const std::set<std::string>& types() const;
    void query(size_t material_index, const std::string& q, const std::optional<std::string>& type);

    void select_material_preset(
        size_t material_index,
        const Domain::MatDb::Material& mat,
        const std::string& preset_id
    );


    ObservableList<MatDbItem>& result()
    {
        return m_result;
    }

    const ObservableList<MatDbItem>& result() const
    {
        return m_result;
    }

    std::optional<std::string> get_color(const std::string& uuid) const override;

    std::optional<Domain::MatDb::Material> get_material_by_uuid(const std::string& uuid) const;

    void
    run_query_if_active(Domain::SelectionId project_id, Domain::SelectionId config_container_id);

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;
    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;
    void on_preset_bundles_loaded() override;

private:
    const Domain::Preset::SelectedPreset& selected_preset() const;
    void run_query();

private:
    struct QueryState
    {
        MatDbClient::Query query;
        size_t slot_index;
    };

    Domain::Workbench& m_workbench;
    ProjectInteractor& m_project_interactor;
    Preset::PresetInteractor& m_preset_interactor;
    std::unique_ptr<MatDbInteractorProjectContexts> m_projects;
    mutable MatDbClient m_client;
    ObservableList<MatDbItem> m_result;
    std::optional<QueryState> m_query;
};

}
