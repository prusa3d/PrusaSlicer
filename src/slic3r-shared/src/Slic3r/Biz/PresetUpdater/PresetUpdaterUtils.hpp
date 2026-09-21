#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"

#include <vector>
#include <string>
#include <map>
#include <set>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterIndex;
class PresetUpdaterProcessStatus;
class AbstractPresetUpdaterRepository;
typedef std::vector<const AbstractPresetUpdaterRepository*> SharedRepositoryVector;

boost::filesystem::path local_presets_path();
boost::filesystem::path local_vendor_path(
    const std::string& repo_id,
    const std::string& vendor_id,
    const std::string& suffix = ""
);

using FileOpResult = tl::expected<void, std::string>;

FileOpResult safe_move(const boost::filesystem::path& source, const boost::filesystem::path& target);

bool move_directory_contents(
    const boost::filesystem::path& source_dir,
    const boost::filesystem::path& dest_dir,
    PresetUpdaterProcessStatus* process_status,
    const char* caller_name
);

bool copy_file_wrapper(
    const boost::filesystem::path& source,
    const boost::filesystem::path& target,
    PresetUpdaterProcessStatus* process_status
);

std::vector<PresetUpdaterIndex> load_vendors_db(const boost::filesystem::path& archive_path);
std::map<std::string, std::string> read_version_manifest(
    const boost::filesystem::path& path,
    PresetUpdaterProcessStatus* process_status
);

/**
 * @param known_repo_ids ids of every repository in the manifest, selected or not. Installed
 * presets whose source is not among them cannot be reconfigured and are skipped.
 */
PresetUpdaterReconfigurationList check_forced_reconfigurations(
    const std::set<std::string>& known_repo_ids,
    PresetUpdaterProcessStatus* process_status
);
PresetUpdaterReconfigurationList check_reconfigurations(
    const SharedRepositoryVector& repos,
    PresetUpdaterProcessStatus* process_status
);

/**
 * @param repos the sources the reconfigurations came from. Vendor data is downloaded here rather
 * than during a check, so a reconfiguration whose source is gone cannot be performed.
 * @param online_allowed when false, only what is already on this computer can be installed.
 */
void perform_reconfigurations(
    const PresetUpdaterReconfigurationList& reconfigurations,
    ReconfigurationType types_to_perform,
    const SharedRepositoryVector& repos,
    bool online_allowed,
    PresetUpdaterProcessStatus* process_status
);

void cleanup_update_sync(PresetUpdaterProcessStatus* process_status);

} // namespace Slic3r::Biz::PresetUpdater
