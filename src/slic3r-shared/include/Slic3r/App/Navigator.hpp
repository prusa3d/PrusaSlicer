#pragma once

#include <optional>
#include <string>

#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"

#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/ModalDialog.hpp"
#include "Slic3r/App/IMsgBoxProvider.hpp"

#include <optional>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::FileOpen {
struct ArchiveFile;
struct ArchiveSelection;
struct OpenFilesOptions;
} // namespace Slic3r::Biz::FileOpen

namespace Slic3r::App {

class RenderModuleBase;

namespace Plater {
class PlaterRenderModule;
} // namespace Plater

namespace Preview {
class PreviewRenderModule;
} // namespace Preview

namespace Platform {
class AbstractRenderCanvas;
} // namespace Platform

namespace Yoga {
class Dialog;
} // namespace Yoga

namespace Render {
enum class ModuleType
{
    Undef,
    Plater,
    Preview,
};
} // namespace Render

class Navigator :
    public Biz::ISelectedProjectChangedListener,
    public IAppConfigChangedListener,
    public IMsgBoxProvider
{
public:
    ~Navigator();

    struct Callbacks
    {
        std::function<void()> render_module_switched;
        std::function<void(ModalDialog)> modal_dialog_changed;
    };

    Callbacks& callbacks();

    /// Makes the canvas reachable before the plater and preview modules exist.
    void set_canvas(Platform::AbstractRenderCanvas& canvas);

    void set_hide_sidebars(bool hide);

    /// False between set_canvas and on_init, while the app runs without the plater and preview.
    bool has_modules() const;

    void on_init(
        Plater::PlaterRenderModule& plater_module,
        Preview::PreviewRenderModule& preview_module,
        Platform::AbstractRenderCanvas& canvas,
        Biz::ProjectInteractor* project_interactor
    );

    void shutdown();

    void navigate_to_module_type(Render::ModuleType type);

    void on_selected_project_changed(size_t index) override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);

    void navigate_to_item(const Domain::ConfigItem* config_item);

    void request_search();

    void open_effective_values_dialog(std::optional<std::string> key);

    void set_modal_dialog(ModalDialog dialog);
    ModalDialog current_modal_dialog() const;
    bool is_any_modal_dialog_opened() const;

    bool object_list_collapsed() const;
    void set_object_list_collapsed(bool collapsed);

    void pick_color(
        const std::string& title,
        const Domain::ColorRGB& input_color = Domain::ColorRGB{0.f, 0.f, 0.f},
        FnColorPicked fn_color_picked       = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptCancelButtons
    ) override;

    void input_text(
        const std::string& title,
        const std::string& text,
        const std::string& input_text   = std::string{},
        FnAcceptedText fn_accepted_text = nullptr,
        FnCancelled fn_cancelled        = nullptr,
        DialogButtons buttons           = AcceptCancelButtons,
        bool multiline                  = false
    ) override;

    void yes_no_cancel(
        const std::string& title,
        const std::string& text,
        FnAccepted fn_accepted   = nullptr,
        FnRejected fn_rejected   = nullptr,
        FnCancelled fn_cancelled = nullptr,
        DialogButtons buttons    = AcceptRejectCancelButtons
    ) override;

    void input_number(
        const std::string& title,
        const std::string& text,
        const std::string& prompt,
        int value,
        int min,
        int max,
        FnAcceptedNumber fn_accepted_number = nullptr,
        FnCancelled fn_cancelled            = nullptr,
        DialogButtons buttons               = AcceptCancelButtons
    ) override;

    /**
     * @return false when another modal dialog is opened, the dialog is not shown then.
     */
    bool confirm_open_files(
        std::vector<Biz::FileOpen::ArchiveFile> files,
        Biz::FileOpen::OpenFilesOptions options,
        std::function<void(std::optional<Biz::FileOpen::ArchiveSelection>)> done
    );

    bool has_fullscreen() const;
    bool is_fullscreen() const;
    void set_fullscreen(bool fullscreen);
    void close_application();

    void on_app_config_changed(const std::string& key) override;

private:
    void set_render_module_type(Render::ModuleType type);

    RenderModuleBase* current_render_module();

private:
    struct ProjectContext
    {
        Render::ModuleType type{Render::ModuleType::Plater};
        Yoga::Dialog* opened_dialog{nullptr}; ///< Dialog is considered exclusive
    };

    using ProjectContexts    = Biz::ProjectScoped<ProjectContext>;
    using ProjectContextsPtr = std::unique_ptr<ProjectContexts>;

    ProjectContextsPtr m_project_contexts;
    Callbacks m_callbacks;

    Plater::PlaterRenderModule* m_plater_module{nullptr};
    Preview::PreviewRenderModule* m_preview_module{nullptr};
    Platform::AbstractRenderCanvas* m_canvas{nullptr};
    bool m_object_list_collapsed{false};
    ModalDialog m_current_modal_dialog{ModalDialog::None};
};

} // namespace Slic3r::App
