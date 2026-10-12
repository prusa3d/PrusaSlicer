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

void Navigator::set_hide_sidebars(bool hide)
{
    // When sidebars are hidden normal dialogs are not available only model ones.
    // This settings persists between RenderModules/Projects and is stored in AppConfig

    AppServices::instance().app_config_interactor().set_item_value(
        "hide_sidebars",
        Domain::ConfigValue{hide}
    );

    for (ProjectContext& context : m_project_contexts->projects()) {
        context.opened_dialog = nullptr;
    }

    m_plater_module->set_sidebars_visible(!hide);
    m_preview_module->set_sidebars_visible(!hide);

    if (hide) {
        m_plater_module->set_opened_dialog(nullptr);
        m_preview_module->set_opened_dialog(nullptr);
    }
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

RenderModuleBase* Navigator::current_render_module()
{
    ProjectContext& context = m_project_contexts->selected();
    switch (context.type) {
    case Render::ModuleType::Plater:
        return m_plater_module;
    case Render::ModuleType::Preview:
        return m_preview_module;
    case Render::ModuleType::Undef:
        return nullptr;
    }

    PANIC("Project context.type is unhandled!");
    return nullptr;
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
    current_render_module()->set_opened_dialog(opened_dialog);
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

void Navigator::set_modal_dialog(ModalDialog modal_dialog)
{
    if (!has_modules()) {
        return;
    }
    current_render_module()->set_modal_dialog(modal_dialog);
    m_current_modal_dialog = modal_dialog;

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

void Navigator::pick_color(
    const std::string& title,
    const Domain::ColorRGB& input_color,
    FnColorPicked fn_color_picked,
    FnCancelled fn_cancelled,
    DialogButtons buttons
)
{
    current_render_module()
        ->msg_box_handler()
        ->pick_color(title, input_color, fn_color_picked, fn_cancelled, buttons);
    set_modal_dialog(ModalDialog::MsgBox);
}

void Navigator::input_text(
    const std::string& title,
    const std::string& text,
    const std::string& input_text,
    FnAcceptedText fn_accepted_text,
    FnCancelled fn_cancelled,
    DialogButtons buttons,
    bool multiline
)
{
    current_render_module()
        ->msg_box_handler()
        ->input_text(title, text, input_text, fn_accepted_text, fn_cancelled, buttons, multiline);
    set_modal_dialog(ModalDialog::MsgBox);
}

void Navigator::yes_no_cancel(
    const std::string& title,
    const std::string& text,
    FnAccepted fn_accepted,
    FnRejected fn_rejected,
    FnCancelled fn_cancelled,
    DialogButtons buttons
)
{
    current_render_module()
        ->msg_box_handler()
        ->yes_no_cancel(title, text, fn_accepted, fn_rejected, fn_cancelled, buttons);
    set_modal_dialog(ModalDialog::MsgBox);
}

void Navigator::input_number(
    const std::string& title,
    const std::string& text,
    const std::string& prompt,
    int value,
    int min,
    int max,
    FnAcceptedNumber fn_accepted_number,
    FnCancelled fn_cancelled,
    DialogButtons buttons
)
{
    current_render_module()->msg_box_handler()->input_number(
        title,
        text,
        prompt,
        value,
        min,
        max,
        fn_accepted_number,
        fn_cancelled,
        buttons
    );
    set_modal_dialog(ModalDialog::MsgBox);
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
