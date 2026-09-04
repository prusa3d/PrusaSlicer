#pragma once

#include "Slic3r/App/Browser/AbstractUploadBrowserLogic.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Biz/Preset/IPresetDialogManager.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"
#include "Slic3r/Assert.hpp"
#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz {
class ProjectInteractor;

namespace Preset {
class PresetInteractor;
} // namespace Preset

} // namespace Slic3r::Biz

namespace Slic3r::Domain::Preset {
enum class PresetKind : uint8_t;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::App {

enum class FileDialogType
{
    Open,
    OpenMultiple,
    Save
};

class IDialogManager :
    public Biz::IMessageDialogProvider,
    public Slic3r::Biz::Preset::IPresetDialogManager
{
public:
    using FileCallback =
        std::function<void(bool result, const std::vector<boost::filesystem::path>& file_paths)>;

    using UploadCallback =
        std::function<void(bool result, const std::string filename)>;

    struct ButtonWithCallback {
        std::string text;
        std::function<void(const std::string& result)> callback;
    };

    IDialogManager()          = default;
    virtual ~IDialogManager() = default;

    virtual void show_file_dialog(
        FileDialogType dialog_type,
        const std::string& dialog_title,
        const boost::filesystem::path& default_folder,
        const std::string& default_file_name,
        const std::string& wildcards,
        const FileCallback& callback
    ) = 0;

    virtual std::string show_input_dialog(
        const std::string& title,
        const std::string& text,
        const std::string& default_value
    ) = 0;

    virtual void show_input_dialog_with_buttons(
        const std::string& title,
        const std::string& text,
        const std::string& default_value,
        const std::vector<ButtonWithCallback>& buttons
    ) = 0;

    virtual std::string show_combo_dialog(
        const std::string& title,
        const std::string& text,
        const std::vector<std::string>& values
    ) = 0;

    virtual void show_webview_dialog(
        std::unique_ptr<Browser::AbstractBrowserLogic>&& logic,
        Slic3r::Biz::ProjectInteractor* project_interactor
    ) = 0;

    virtual void show_upload_webview_dialog(
        std::unique_ptr<Browser::AbstractUploadBrowserLogic>&& logic,
        Slic3r::Biz::ProjectInteractor* project_interactor,
        const UploadCallback& callback
    ) = 0;

    virtual void show_diff_dialog(
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
        std::optional<Domain::Preset::PresetKind> kind = std::nullopt
    ) = 0;

    virtual std::string show_ramming_dialog(const std::string& ramming_parameters) = 0;

    virtual void open_in_browser(const std::string& link, int flag) = 0;
};

} // namespace Slic3r::App
