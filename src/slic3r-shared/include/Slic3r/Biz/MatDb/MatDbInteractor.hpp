#pragma once
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include "Slic3r/Biz/MatDb/MatDbClient.hpp"
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


class MatDbInteractor final : public ISelectedConfigContainerChangedListener
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

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;
private:
    const Domain::Preset::SelectedPreset& selected_preset() const;

private:
    Domain::Workbench& m_workbench;
    Preset::PresetInteractor& m_preset_interactor;
    std::unique_ptr<MatDbInteractorProjectContexts> m_projects;
    MatDbClient m_client;
    ObservableList<MatDbItem> m_result;
    Domain::SelectionId m_selected_project{Domain::INVALID_ID};
};

}
