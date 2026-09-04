#pragma once

#include "Slic3r/Biz/ResultExport/ExportNameParser.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"

#include <boost/filesystem/path.hpp>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::ExportPathSelect {

void validate_bgcode_extension(
    const boost::filesystem::path& file_path,
    bool bgcode_allowed,
    const std::function<void(const boost::filesystem::path&)>& on_proceed,
    const std::function<void()>& on_retry);

/**
 * @brief Shows modal dialog "Save file as". Returns true if user confirmed selection.
 */
void
show_export_modal_dialog(
    const Biz::ProjectInteractor& project_interactor,
    bool default_path_at_removable,
    const std::function<void(bool result, const std::vector<boost::filesystem::path>& file_paths)>& callback,
    const std::string& wildcards_overide = std::string()
);

void
show_upload_modal_dialog(
    const Biz::ProjectInteractor& project_interactor,
    const std::vector<Biz::PrintHost::PrintHostAfterUploadAction>& post_actions,
    const std::function<void(const std::string&, Biz::PrintHost::PrintHostAfterUploadAction)>& callback
);

Biz::ExportNameParser::ExportNameData get_export_name_data(const Biz::ProjectInteractor& project_interactor);

} // namespace Slic3r::App::ExportPathSelect
