#include "Slic3r/Biz/MatDb/MatDbInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::MatDb {

struct MatDbInteractorProjectContext
{
    Domain::SelectionId selected_config_container_id{Domain::INVALID_ID};
};

struct MatDbInteractorProjectContexts : ProjectScoped<MatDbInteractorProjectContext>
{
    using ProjectScoped::ProjectScoped;
};

MatDbInteractor::MatDbInteractor(
    Domain::Workbench& workbench,
    ProjectInteractor& project_interactor
) :
    m_workbench(workbench),
    m_preset_interactor(project_interactor.preset_interactor()),
    m_projects(std::make_unique<MatDbInteractorProjectContexts>(project_interactor))
{
    project_interactor.add_listener<ISelectedConfigContainerChangedListener>(this);
}

MatDbInteractor::~MatDbInteractor() = default;

void MatDbInteractor::init_client()
{
    namespace fs = boost::filesystem;
    for (const auto& parent_dir : {data_dir(), resources_dir()}) {
        auto p = fs::path{parent_dir} / "matdb" / "matdb.zip";
        if (m_client.open(p.string())) {
            break;
        }
    }
}

const std::set<std::string>& MatDbInteractor::types() const
{
    return m_client.types();
}

void MatDbInteractor::query(size_t material_index, const std::string& q, const std::optional<std::string>& type)
{
    auto result = m_client.query({q}, 64, 0);
    std::vector<MatDbItem> items;
    auto resolve_preset = [this, material_index](const Domain::MatDb::Material& mat) -> PresetMatches
    {
        const auto& sp = selected_preset();
        auto mpv       = m_preset_interactor.get_material_presets_view(
            m_projects->selected_project_id(),
            sp.hw_config.id,
            sp.printer.id,
            sp.print.id,
            material_index
        );
        PresetMatches ret;
        for (const auto& [preset, mr] : mpv.match_rules()) {
            if (auto rank = mr.match(mat); rank > 0) {
                ret.emplace_back(preset.id, std::string{preset.short_name()}, rank);
            }
        }
        std::ranges::sort(ret, std::ranges::greater(), [](const auto& v) { return v.rank; });
        return ret;
    };
    std::ranges::copy(
        result
            | std::views::transform(
                [&resolve_preset](const Domain::MatDb::Material& mat)
                { return MatDbItem{mat, resolve_preset(mat)}; }
            ),
        std::back_inserter(items)
    );
    m_result.reset(std::move(items));
}

void MatDbInteractor::select_material_preset(
    size_t material_index,
    const Domain::MatDb::Material& mat,
    const std::string& preset_id
)
{
    m_preset_interactor.select_material_preset(material_index, preset_id, true);
    // TODO: write metadata from mat to selected preset
}

void MatDbInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    m_projects->project(project_id).selected_config_container_id = container_id;
}

const Domain::Preset::SelectedPreset& MatDbInteractor::selected_preset() const
{
    auto cc_id = m_projects->selected().selected_config_container_id;
    auto* cc = m_workbench.project(m_projects->selected_project_id()).find_config_container(cc_id);
    ASSERT(cc != nullptr);
    return cc->selected_preset();
}


} // namespace Slic3r::Biz::MatDb
