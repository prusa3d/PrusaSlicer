#include "Workflows.hpp"

#include <exception>
#include <fstream>

#include "boost/nowide/fstream.hpp"
#include "boost/filesystem.hpp"
#include "boost/system/error_code.hpp"
#include "boost/log/trivial.hpp"
#include "nlohmann/json.hpp"

#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace sla {

namespace {
    // Major version of workflows file that this slicer is compatible with.
    // Both newer and older updates are ignored.
    const unsigned compatible_version_major = 1;
}

static std::optional<Semver> get_version_from_workflows_file(const std::string& filename)
{
    std::optional<Semver> out;
    if (boost::nowide::ifstream f(filename); f) {
        try {
            nlohmann::json data = nlohmann::json::parse(f, nullptr, false);
            boost::optional<Semver> version = Semver::parse(data.at("version").get<std::string>());
            if (version)
                out = std::make_optional(*version);
        } catch (...) {
            // Do nothing.
        }
    }
    return out;
}



// The point of the ctr is to ensure that datadir contains compatible workflows.json and load
// the content into m_data (while checking that it has a "version" field which can be parsed).
// If anything fails, m_data is nullptr.
WorkflowManager::WorkflowManager()
{
    // Make sure that the file exists in datadir, if not, copy it from resources dir.
    const std::string filename_datadir = data_dir() + "/workflows.json";
    const std::string filename_resdir = resources_dir() + "/workflows/workflows.json";

    // Read versions of both files.
    std::optional<Semver> version_datadir = get_version_from_workflows_file(filename_datadir);
    std::optional<Semver> version_resdir = get_version_from_workflows_file(filename_resdir);

    if (! version_resdir || version_resdir->maj() != compatible_version_major) {
        // This should never happen.
        BOOST_LOG_TRIVIAL(error) << "Bundled workflows file is missing, corrupted or not compatible. Bailing out.";
        return;
    }

    // Make sure that currently used file in datadir is compatible and not older than what we have in resources.
    if (version_datadir) {
        bool rem = false;
        if (*version_datadir < *version_resdir) {
            BOOST_LOG_TRIVIAL(trace) << "Current workflows.json is older than what is bundled in resources (currently "
                << *version_datadir << ", bundled is " << *version_resdir << ". Reinitializing from scratch using the "
                " bundled workflows file.";
            rem = true;
        }
        else if (version_datadir->maj() != compatible_version_major) {
            BOOST_LOG_TRIVIAL(trace) << "Current workflows.json is not compatible with this application version (got"
                << *version_datadir << ", need " << compatible_version_major << ".x.x. Reinitializing from "
                "scratch using the bundled workflows file.";
            rem = true;
        }
        if (rem) {
            boost::system::error_code ec;
            boost::filesystem::remove(filename_datadir, ec);
            if (boost::filesystem::exists(filename_datadir, ec) || ec) {
                BOOST_LOG_TRIVIAL(error) << "Unable to delete workflows.json. SLA workflows will not be available.";
                return;
            }
        }
    }

    // If the file does not exist in datadir, copy it from resources.
    boost::system::error_code ec;
    if (! boost::filesystem::exists(filename_datadir, ec)) {
        BOOST_LOG_TRIVIAL(trace) << "workflows.json file not found in datadir, copying from resources...";
        if (boost::filesystem::exists(filename_resdir, ec)) {
            boost::filesystem::copy_file(filename_resdir, filename_datadir, ec);
            version_datadir = version_resdir;
        }
    }
    if (! boost::filesystem::exists(filename_datadir, ec)) {
        BOOST_LOG_TRIVIAL(error) << "ERROR: Unable to copy workflows.json from resources. SLA Workflows will not be available.";
        return;
    }

    // If we got here, we should have m_current_version filled in, and the respective file in datadir.
    // All that remains is to actually load the data:
    if (boost::nowide::ifstream f(filename_datadir); f) {
        try {
            m_data = std::make_unique<nlohmann::json>(nlohmann::json::parse(f, nullptr, false));
            boost::optional<Semver> version = Semver::parse(m_data->at("version").get<std::string>());
            if (version && *version == *version_datadir) {
                // This is the happy path.
                return;
            }
        } catch (...) {
            // Do nothing.
        }
    }
    BOOST_LOG_TRIVIAL(error) << "Unexpected error when loading workflows.json file. SLA workflows will not he available.";
}

    
    
WorkflowManager::~WorkflowManager()
{
    if (m_thread.joinable())
        m_thread.join();
}



std::vector<Workflow> WorkflowManager::workflows_for_material_sorted(const std::string &material_uuid) const
{
    std::vector<Workflow> out;
    out.push_back({"", "(no workflow)", true});

    if (! m_data || material_uuid.empty() || m_data->is_discarded()
      || !m_data->contains("materials") || !(*m_data)["materials"].is_object()
      || !m_data->contains("workflows") || !(*m_data)["workflows"].is_object())
        return out;

    const nlohmann::json& materials = (*m_data)["materials"];
    if (!materials.contains(material_uuid))
        return out;

    const nlohmann::json& material_data = materials[material_uuid];
    std::string default_workflow_uuid;
    if (material_data.contains("default_workflow") && material_data["default_workflow"].is_string())
        default_workflow_uuid = material_data["default_workflow"].get<std::string>();

    if (!material_data.contains("slx_workflows") || !material_data["slx_workflows"].is_array())
        return out;

    const nlohmann::json& all_workflows = (*m_data)["workflows"];
    for (const auto& workflow_uuid_json : material_data["slx_workflows"]) {
        if (! workflow_uuid_json.is_string())
            continue;
        std::string workflow_uuid = workflow_uuid_json.get<std::string>();
        if (all_workflows.contains(workflow_uuid)) {
            const nlohmann::json& workflow_data = all_workflows[workflow_uuid];
            if (workflow_data.contains("name") && workflow_data["name"].is_string())
                out.push_back({workflow_uuid, workflow_data["name"].get<std::string>(), workflow_uuid == default_workflow_uuid});
        }
    }

    std::sort(out.begin() + 1, out.end(), [](const auto& a , const auto& b) { return a.name < b.name; });
    return out;
}



static std::optional<std::string> get_workflows_file_to_download(const std::string& metadata, Semver current_version)
{
    nlohmann::json mdata;
    try {
        mdata = nlohmann::json::parse(metadata);
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Unable to parse downloaded workflows metadata file.";
        return std::nullopt;
    }

    std::vector<std::pair<Semver, std::string>> list;
    
    try {
        nlohmann::json versions_obj = mdata.at("versions");
        for (auto& [key, value] : versions_obj.items()) {
            Semver version(key);
            if (version.maj() == compatible_version_major) {
                std::string file_url = value.at("file").get<std::string>();
                list.emplace_back(version, file_url);
            }
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Unable to parse downloaded workflows metadata file.";
        return std::nullopt;
    }

    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    if (list.empty()) {
        BOOST_LOG_TRIVIAL(error) << "There is no compatible SLA workflows version available.";
        return std::nullopt;
    }
    if (list.back().first <= current_version)
        return std::nullopt;

    BOOST_LOG_TRIVIAL(trace) << "SLA workflows update is available (" << current_version << " -> " << list.back().first << ")";
    return list.back().second;
}



static void fetch_workflows_file_online_sync(
    Semver current_version,
    std::function<std::optional<std::string>(const std::string&)> http_get_file_as_string_fn,
    std::function<void(Semver)> after_update_cb
)
{
    std::optional<std::string> metadata = http_get_file_as_string_fn("https://slx-storage.prusa3d.com/metadata.json");
    if (metadata) {
        std::optional<std::string> filename_to_get = get_workflows_file_to_download(*metadata, current_version);
        if (filename_to_get) {
            std::optional<std::string> file_data = http_get_file_as_string_fn(*filename_to_get);
            if (file_data) {
                // Check that the data are reasonable:
                Semver new_version;
                try {
                    new_version = *Semver::parse(nlohmann::json::parse(*file_data).at("version").get<std::string>());
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Downloaded worflows file seems to be corrupted: " << *filename_to_get;
                    return;
                }
                std::string filename_datadir = data_dir() + "/workflows.json";
                boost::system::error_code ec;
                if (boost::filesystem::exists(filename_datadir, ec) && !ec) {
                    boost::filesystem::remove(filename_datadir, ec);
                    if (ec)
                        BOOST_LOG_TRIVIAL(error) << "Failed to delete file: " << filename_datadir << " : " << ec.message();
                }
                if (boost::nowide::ofstream out(filename_datadir, std::ios::binary); out) {
                    out << *file_data;
                    after_update_cb(new_version);
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "Failed to create workflows.json file: " << filename_datadir;
                }
            }
        }
    }
}



void WorkflowManager::fetch_workflows_file_online_in_background(
    std::function<std::optional<std::string>(const std::string&)> http_get_file_as_string_fn,
    std::function<void(Semver)> after_update_cb
)
{
    if (! m_data)
        return;

    if (m_thread.joinable())
        m_thread.join();

    Semver current_version = *Semver::parse(m_data->at("version").get<std::string>());
    m_thread = std::move(std::thread(
        fetch_workflows_file_online_sync,
        current_version,
        http_get_file_as_string_fn,
        after_update_cb)
    );
}

} // namespace sla
} // namespace Slic3r
