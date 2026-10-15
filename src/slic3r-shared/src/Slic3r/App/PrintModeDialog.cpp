#include "Slic3r/App/PrintModeDialog.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/MacroOverrides.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintModeDialog::PrintModeDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Dialog({Biz::_u8L("Print mode")}, "PrintModeDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_macro_overrides_listener_scope(project_interactor.preset_interactor(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this)
{
    content_item()->set_width(300_fpx);
    content_item()->set_height(200_fpx);

    m_vase_mode_btn = content()->emplace_back<LayoutButton>(Biz::_u8L("Vase mode"));
    m_vase_mode_btn->set_flex_shrink(0);
    m_vase_mode_btn->set_checkable(true);
    m_vase_mode_btn->callbacks().action = [this] { toggle_vase_mode(); };

    m_vase_mode_btn->set_label_size(60_fpx, 60_fpx);
    m_vase_mode_btn->set_text_wrap_mode(Yoga::Text::WrapMode::WrapElide);
    m_vase_mode_btn->set_label_align({AlignH::Center, AlignV::Center});

    refresh_vase_mode_button();
}

void PrintModeDialog::on_macro_overrides_changed()
{
    refresh_vase_mode_button();
}

void PrintModeDialog::on_config_container_selection_changed(
    Domain::SelectionId /*project_id*/,
    Domain::SelectionId /*config_container_id*/
)
{
    refresh_vase_mode_button();
}

void PrintModeDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void PrintModeDialog::toggle_vase_mode()
{
    std::optional<Domain::MacroOverridesFDM> overrides;
    if (!is_mode_active()) {
        overrides = Domain::make_vase_mode_overrides();
    }
    m_project_interactor.preset_interactor().set_macro_overrides(
        Domain::vase_mode_layer(), std::move(overrides)
    );
}

bool PrintModeDialog::is_mode_active() const
{
    const Domain::Preset::SelectedPreset& selected_preset{
        m_project_interactor.preset_interactor().selected_printer_preset()};
    const auto* fdm_overrides{
        std::get_if<Domain::MacroOverridesMapFDM>(&selected_preset.macro_overrides)};
    return fdm_overrides != nullptr
        && fdm_overrides->contains(Domain::vase_mode_layer());
}

void PrintModeDialog::refresh_vase_mode_button()
{
    const Domain::Preset::SelectedPreset& selected_preset{
        m_project_interactor.preset_interactor().selected_printer_preset()};

    const bool is_fdm{
        std::holds_alternative<Domain::MacroOverridesMapFDM>(selected_preset.macro_overrides)};
    m_vase_mode_btn->set_visible(is_fdm);
    m_vase_mode_btn->set_checked(is_mode_active());

    if (mode_active_changed) {
        mode_active_changed(is_mode_active());
    }
}

} // namespace Slic3r::App
