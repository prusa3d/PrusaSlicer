#pragma once

#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"

#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/ModalDialog.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

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

class Navigator : public Biz::ISelectedProjectChangedListener, public IAppConfigChangedListener
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

    /// False between set_canvas and on_init, while the app runs without the plater and preview.
    bool has_modules() const;

    void on_init(
        Plater::PlaterRenderModule& plater_module,
        Preview::PreviewRenderModule& preview_module,
        Platform::AbstractRenderCanvas& canvas,
        Biz::ProjectInteractor* project_interactor
    );

    void navigate_to_module_type(Render::ModuleType type);

    void on_selected_project_changed(size_t index) override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);

    void navigate_to_item(const Domain::ConfigItem* config_item);

    void request_search();

    void open_invalid_data_dialog();

    void set_modal_dialog(ModalDialog dialog);
    ModalDialog current_modal_dialog() const;
    bool is_any_modal_dialog_opened() const;

    bool object_list_collapsed() const;
    void set_object_list_collapsed(bool collapsed);

    bool has_fullscreen() const;
    bool is_fullscreen() const;
    void set_fullscreen(bool fullscreen);
    void close_application();

    void on_app_config_changed(const std::string& key) override;

private:
    void set_render_module_type(Render::ModuleType type);

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
