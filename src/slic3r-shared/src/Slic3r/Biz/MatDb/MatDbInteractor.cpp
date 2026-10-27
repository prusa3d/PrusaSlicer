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
struct QueryState
{
    MatDbClient::Query query;
    size_t slot_index;
    bool only_favorites;
};

struct MatDbInteractorProjectContext
{
    Domain::SelectionId selected_config_container_id{Domain::INVALID_ID};
    std::optional<QueryState> query_state;
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

void MatDbInteractor::query(
    size_t material_index,
    const std::string& q,
    const std::optional<std::string>& type,
    bool only_favorites
)
{
    MatDbClient::Query query = {q};
    if (type.has_value()) {
        query.types.push_back(type.value());
    }
    m_projects->selected().query_state = {query, material_index, only_favorites};
    run_query(m_projects->selected_project_id());
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
        run_query(project_id);
    }
}

void MatDbInteractor::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (type == Preset::PresetItemType::PrintPreset
        || type == Preset::PresetItemType::MaterialPreset)
    {
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
    run_query(m_projects->selected_project_id());
}

void MatDbInteractor::on_connect_favorite_materials_changed()
{
    const auto& query = m_projects->selected().query_state;
    if (query.has_value() && query->only_favorites) {
        run_query(m_projects->selected_project_id());
        return;
    }
    for (size_t i = 0; i < m_result.size(); ++i) {
        const bool favorite = is_favorite(m_result.at(i).material);
        if (m_result.at(i).is_favorite != favorite) {
            MatDbItem item   = m_result.at(i);
            item.is_favorite = favorite;
            m_result.set(std::move(item), i);
        }
    }
}

bool MatDbInteractor::is_favorite(const Domain::MatDb::Material& mat) const
{
    return m_project_interactor.connect_favorite_material_service().is_favorite(mat.uuid);
}

void MatDbInteractor::run_query(Domain::SelectionId project_id)
{
    auto& project_query = m_projects->project(project_id).query_state;
    if (!project_query.has_value()) {
        m_result.reset({});
        return;
    }

    auto& query = project_query->query;
    const auto material_index = project_query->slot_index;
    if (project_query->only_favorites) {
        query.uuids = m_project_interactor.connect_favorite_material_service()
                          .favorite_material_uuids();
    } else {
        query.uuids.reset();
    }
    auto result = m_client.query(query, 64, 0);
    bool has_selected = false;
    if (const std::string selected_uuid{selected_preset().material_matdb_uuid(material_index)};
        !selected_uuid.empty())
    {
        std::erase_if(result, [&selected_uuid](const auto& mat) { return mat.uuid == selected_uuid; });
        if (auto selected = m_client.get_by_uuid(selected_uuid)) {
            result.insert(result.begin(), std::move(*selected));
            has_selected = true;
        }
    }
    std::vector<MatDbItem> items;
    auto resolve_preset =
        [this, project_id, material_index](const Domain::MatDb::Material& mat) -> PresetMatches
    {
        const auto& sp = selected_preset();
        auto mpv       = m_preset_interactor.get_material_presets_view(
            project_id,
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
                ret.emplace_back(
                    preset.id,
                    std::string{preset.short_name()},
                    rank,
                    Domain::Preset::get_feature<bool>(preset.features, "generic").value_or(false)
                );
            }
        }
        MATDB_PRESET_MATCH_LOG("----Sorted results-------------------------------");
        std::ranges::stable_sort(ret, std::ranges::greater(), [](const auto& v) { return v.rank; });
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
                [this, &resolve_preset](const Domain::MatDb::Material& mat)
                { return MatDbItem{mat, resolve_preset(mat), is_favorite(mat)}; }
            ),
        std::back_inserter(items)
    );
    if (has_selected) {
        items.front().is_selected = true;
    }
    m_result.reset(std::move(items));

}

} // namespace Slic3r::Biz::MatDb
