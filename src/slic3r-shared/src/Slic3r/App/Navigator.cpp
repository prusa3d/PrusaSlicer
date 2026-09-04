#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/App/Plater/PlaterRenderModule.hpp"
#include "Slic3r/App/Preview/PreviewRenderModule.hpp"
#include "Slic3r/App/Platform/AbstractRenderCanvas.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/Log.hpp"

namespace Slic3r::App {

Navigator::~Navigator() {}

Navigator::Callbacks& Navigator::callbacks()
{
    return m_callbacks;
}

void Navigator::set_canvas(Platform::AbstractRenderCanvas& canvas)
{
    m_canvas = &canvas;
}

void Navigator::on_init(
    Plater::PlaterRenderModule& plater_module,
    Preview::PreviewRenderModule& preview_module,
    Platform::AbstractRenderCanvas& canvas,
    Biz::ProjectInteractor* project_interactor
)
{
    m_plater_module  = &plater_module;
    m_preview_module = &preview_module;
    m_plater_module->set_navigator(this);
    m_preview_module->set_navigator(this);
    m_canvas           = &canvas;
    m_project_contexts = std::make_unique<ProjectContexts>(*project_interactor);

    project_interactor->add_listener<ISelectedProjectChangedListener>(this);
    AppServices::instance().app_config_interactor().add_listener<IAppConfigChangedListener>(this);
}

bool Navigator::has_modules() const
{
    return m_project_contexts != nullptr;
}

void Navigator::navigate_to_module_type(Render::ModuleType type)
{
    if (!has_modules()) {
        return;
    }
    ProjectContext& context = m_project_contexts->selected();
    context.opened_dialog   = nullptr;
    set_render_module_type(type);
}

void Navigator::set_render_module_type(Render::ModuleType type)
{
    ProjectContext& context               = m_project_contexts->selected();
    const bool force_render_modele_switch = context.type != type;
    context.type                          = type;

    if (type == Render::ModuleType::Plater) {
        m_canvas->set_next_render_module(m_plater_module);
    } else if (type == Render::ModuleType::Preview) {
        m_canvas->set_next_render_module(m_preview_module);
    }

    if (force_render_modele_switch && m_callbacks.render_module_switched) {
        m_callbacks.render_module_switched();
    }
}

void Navigator::on_selected_project_changed(size_t index)
{
    if (!has_modules()) {
        return;
    }
    ProjectContext& context = m_project_contexts->selected();
    set_render_module_type(context.type);
    set_opened_dialog(context.opened_dialog);
}

void Navigator::set_opened_dialog(Yoga::Dialog* opened_dialog)
{
    if (!has_modules()) {
        return;
    }
    ProjectContext& context = m_project_contexts->selected();
    context.opened_dialog   = opened_dialog;
    switch (context.type) {
    case Render::ModuleType::Plater:
        m_plater_module->set_opened_dialog(opened_dialog);
        break;
    case Render::ModuleType::Preview:
        m_preview_module->set_opened_dialog(opened_dialog);
        break;
    case Render::ModuleType::Undef:
        break;
    }
}

void Navigator::navigate_to_item(const Domain::ConfigItem* config_item)
{
    ASSERT(config_item);

    if (!has_modules()) {
        return;
    }
    set_render_module_type(Render::ModuleType::Plater);
    m_plater_module->navigate_to_item(config_item);
}

void Navigator::request_search()
{
    if (!has_modules()) {
        return;
    }
    set_render_module_type(Render::ModuleType::Plater);
    m_plater_module->open_search();
}

void Navigator::open_invalid_data_dialog()
{
    ProjectContext& context = m_project_contexts->selected();
    switch (context.type) {
    case Render::ModuleType::Plater:
        m_plater_module->open_invalid_data_dialog();
        break;
    case Render::ModuleType::Preview:
        m_preview_module->open_invalid_data_dialog();
        break;
    case Render::ModuleType::Undef:
        break;
    }
}

void Navigator::set_modal_dialog(ModalDialog modal_dialog)
{
    if (!has_modules()) {
        return;
    }
    ProjectContext& context = m_project_contexts->selected();
    switch (context.type) {
    case Render::ModuleType::Plater:
        m_plater_module->set_modal_dialog(modal_dialog);
        break;
    case Render::ModuleType::Preview:
        m_preview_module->set_modal_dialog(modal_dialog);
        break;
    case Render::ModuleType::Undef:
        break;
    }

    if (m_callbacks.modal_dialog_changed) {
        m_callbacks.modal_dialog_changed(modal_dialog);
    }
}

ModalDialog Navigator::current_modal_dialog() const
{
    return m_current_modal_dialog;
}

bool Navigator::is_any_modal_dialog_opened() const
{
    return m_current_modal_dialog != ModalDialog::None;
}

bool Navigator::object_list_collapsed() const
{
    return m_object_list_collapsed;
}

void Navigator::set_object_list_collapsed(bool collapsed)
{
    if (!has_modules()) {
        return;
    }
    if (m_object_list_collapsed != collapsed) {
        m_object_list_collapsed = collapsed;
        m_plater_module->set_object_list_collapsed(collapsed);
        m_preview_module->set_object_list_collapsed(collapsed);
    }
}

bool Navigator::has_fullscreen() const
{
    return m_canvas->has_fullscreen();
}

bool Navigator::is_fullscreen() const
{
    return m_canvas->is_fullscreen();
}

void Navigator::set_fullscreen(bool fullscreen)
{
    m_canvas->set_fullscreen(fullscreen);
    if (!has_modules()) {
        return;
    }
    m_plater_module->command_binding_manager().update_ui_items();
    m_preview_module->command_binding_manager().update_ui_items();
}

void Navigator::close_application()
{
    m_canvas->close_application();
}

void Navigator::on_app_config_changed(const std::string& key)
{
    if (!has_modules()) {
        return;
    }
    m_plater_module->command_binding_manager().update_ui_items();
    m_preview_module->command_binding_manager().update_ui_items();
}

} // namespace Slic3r::App
