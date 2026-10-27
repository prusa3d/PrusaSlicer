#include "Slic3r/Biz/MatDb/MatDbInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"

#include <boost/filesystem/path.hpp>

#include <algorithm>

#ifndef MATDB_PRESET_MATCH_DEBUG
#define MATDB_PRESET_MATCH_DEBUG 0
#endif

#if MATDB_PRESET_MATCH_DEBUG
#define MATDB_PRESET_MATCH_LOG(...) SPDLOG_DEBUG(__VA_ARGS__)
#else
#define MATDB_PRESET_MATCH_LOG(...)
#endif

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
    m_project_interactor(project_interactor),
    m_preset_interactor(project_interactor.preset_interactor()),
    m_projects(std::make_unique<MatDbInteractorProjectContexts>(project_interactor))
{
    m_preset_interactor.add_listener<IPresetChangedListener>(this);
}

MatDbInteractor::~MatDbInteractor()
{
    m_preset_interactor.remove_listener<IPresetChangedListener>(this);
}

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
    MatDbClient::Query query = {q};
    if (type.has_value()) {
        query.types.push_back(type.value());
    }
    m_query = {query, material_index};
    run_query();
}

void MatDbInteractor::select_material_preset(
    size_t material_index,
    const Domain::MatDb::Material& mat,
    const std::string& preset_id
)
{
    m_preset_interactor.select_material_preset(material_index, preset_id, true, mat.uuid);

    // The material from MDB carries its own color, which should replace the current one.
    // The reconcile step triggered by the preset change only fills empty slots, so the color
    // has to be reset explicitly. Note that ProjectSettingsInteractor is constructed after
    // this object, so it must not be accessed from the constructor.
    m_project_interactor.project_settings_interactor().reset_color_to_auto(
        m_project_interactor.selected_project_id(),
        m_project_interactor.selected_config_container_id(),
        static_cast<int>(material_index)
    );
}

std::optional<std::string> MatDbInteractor::get_color(const std::string& uuid) const
{
    const auto mat = m_client.get_by_uuid(uuid);
    if (!mat.has_value() || !mat.value().primary_color.has_value()) {
        return std::nullopt;
    }
    const auto& color = mat.value().primary_color.value().color_rgba;
    ASSERT(color.size() == 7 || color.size() == 9);
    return std::string{color.substr(0, 7)};
}

std::optional<Domain::MatDb::Material> MatDbInteractor::get_material_by_uuid(
    const std::string& uuid
) const
{
    return m_client.get_by_uuid(uuid);
}

const Domain::Preset::SelectedPreset& MatDbInteractor::selected_preset() const
{
    auto cc_id = m_projects->selected().selected_config_container_id;
    auto* cc = m_workbench.project(m_projects->selected_project_id()).find_config_container(cc_id);
    ASSERT(cc != nullptr);
    return cc->selected_preset();
}

void MatDbInteractor::
    run_query_if_active(Domain::SelectionId project_id, Domain::SelectionId config_container_id)
{
    if (project_id == m_projects->selected_project_id()
        && m_projects->project(project_id).selected_config_container_id == config_container_id)
    {
        run_query();
    }
}

void MatDbInteractor::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (type == Preset::PresetItemType::PrintPreset) {
        run_query_if_active(project_id, config_container_id);
    }
}

void MatDbInteractor::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    m_projects->project(project_id).selected_config_container_id = config_container_id;
    run_query_if_active(project_id, config_container_id);
}

void MatDbInteractor::on_preset_bundles_loaded()
{
    run_query();
}

void MatDbInteractor::run_query()
{
    if (!m_query.has_value()) {
        return;
    }

    const size_t slot_count = selected_preset().materials.size();
    if (slot_count == 0) {
        return;
    }
    m_query->slot_index = std::min(m_query->slot_index, slot_count - 1);

    const auto& query = m_query->query;
    const auto material_index = m_query->slot_index;
    auto result = m_client.query(query, 64, 0);
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
        MATDB_PRESET_MATCH_LOG("=================================================");
        MATDB_PRESET_MATCH_LOG("Resolve presets for MatDB Item {}", mat.slug);
        PresetMatches ret;
        for (const auto& [preset, mr] : mpv.match_rules()) {
            if (auto rank = mr.match(mat); rank > 0) {
                MATDB_PRESET_MATCH_LOG(
                    "Match ranked {} with {} ({}:{}:{})",
                    rank,
                    preset.short_name(),
                    preset.last_node_location.file,
                    preset.last_node_location.line,
                    preset.last_node_location.column
                );
                MATDB_PRESET_MATCH_LOG("Rule: {}", to_string(mr));
                ret.emplace_back(preset.id, std::string{preset.short_name()}, rank);
            }
        }
        MATDB_PRESET_MATCH_LOG("----Sorted results-------------------------------");
        std::ranges::sort(ret, std::ranges::greater(), [](const auto& v) { return v.rank; });
#if MATDB_PRESET_MATCH_DEBUG
        for (const auto& r : ret) {
            MATDB_PRESET_MATCH_LOG("{}: {}", r.rank, r.preset_name);
        }
#endif
        MATDB_PRESET_MATCH_LOG("=================================================");
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

} // namespace Slic3r::Biz::MatDb
