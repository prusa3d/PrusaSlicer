#pragma once
#include "Slic3r/Biz/IMdb.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include "Slic3r/Biz/ConnectPrinter/ConnectFavoriteMaterialService.hpp"
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
    bool is_generic{false};
};

using PresetMatches = std::vector<PresetMatch>;

struct MatDbItem
{
    Domain::MatDb::Material material;
    PresetMatches preset_matches;
    bool is_favorite{false};
    bool is_selected{false};
};

struct MatDbInteractorProjectContexts;

class MatDbInteractor final :
    public Preset::IPresetChangedListener,
    public ConnectPrinter::IConnectFavoriteMaterialsListener,
    public IMdb
{
public:
    MatDbInteractor(Domain::Workbench& workbench, ProjectInteractor& project_interactor);
    ~MatDbInteractor() final;

    void init_client();

    const std::set<std::string>& types() const;
    void query(
        size_t material_index,
        const std::string& q,
        const std::optional<std::string>& type,
        bool only_favorites
    );

    void select_material_preset(
        size_t material_index,
        const Domain::MatDb::Material& mat,
        const std::string& preset_id
    );

    /**
     * @brief Selects the MDB material with the given uuid in the slot, together with the
     *        preset the material selection offers first for it.
     *
     * A slot that already holds the material keeps its preset when that is one offered for
     * the material. Unlike select_material_preset() this is not an action of the user: the
     * project is not marked as modified and the color of the slot is left to the caller.
     *
     * @return false when the material cannot be read from MDB or no preset matches it;
     *         nothing changes then.
     */
    bool select_material_by_uuid(size_t material_index, const std::string& uuid);

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
    void on_preset_bundles_reloaded() override;

    void on_connect_favorite_materials_changed() override;

private:
    const Domain::Preset::SelectedPreset& selected_preset() const;
    /**
     * @brief Presets matching the MDB material in the slot, the best match first.
     */
    PresetMatches resolve_presets(
        Domain::SelectionId project_id,
        size_t material_index,
        const Domain::MatDb::Material& mat
    ) const;
    void run_query(Domain::SelectionId project_id);
    bool is_favorite(const Domain::MatDb::Material& mat) const;

private:

    Domain::Workbench& m_workbench;
    ProjectInteractor& m_project_interactor;
    Preset::PresetInteractor& m_preset_interactor;
    std::unique_ptr<MatDbInteractorProjectContexts> m_projects;
    mutable MatDbClient m_client;
    ObservableList<MatDbItem> m_result;
};

}
