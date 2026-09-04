#include "Slic3r/App/PreferencesDialog.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Navigator.hpp"

#include <fmt/format.h>

namespace Slic3r::App {

PreferencesDialog::PreferencesDialog(
    AppConfigInteractor& app_config_interactor,
    Navigator& navigator
) :
    ConfigSettingsDialog(app_config_interactor, navigator, "PreferencesDialog")
{
    content_item()->set_modal(true);

    Tab* tab = append_tab(Biz::_u8L("Preferences"));
    m_config_tabs.emplace_back(
        std::make_unique<ConfigTab>(
            &app_config_interactor.app_config_cbi(),
            tab,
            app_config_interactor
        )
    );

    on_reset();
}

void PreferencesDialog::on_reset()
{
    for (size_t tab_index = 1; tab_index < m_tabs.size(); ++tab_index) {
        remove_tab(1);
    }
    m_tabs.erase(m_tabs.cbegin() + 1, m_tabs.cend());
}

void PreferencesDialog::close_action()
{
    m_navigator.set_modal_dialog(ModalDialog::None);
}

} // namespace Slic3r::App
