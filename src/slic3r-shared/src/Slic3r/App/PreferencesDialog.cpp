#include "Slic3r/App/PreferencesDialog.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"

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
    m_config_tabs.front()->observable_categorizer->set_filter_fn(
        [this](const Biz::ConfigItemContext& data)
        {
            const Domain::ConfigItemDef::Category category = data.config_item->def().category;
            if (category == Domain::ConfigItemDef::Category::Hidden) {
                return false;
            } else if (category == Domain::ConfigItemDef::Category::AppConfig_Mouse3D) {
                return m_mouse_3d_page_visible;
            }

            return true;
        }
    );

    on_reset();
}

void PreferencesDialog::on_about_to_show()
{
    const AppServices& app_services = AppServices::instance();
    const bool mouse_3d_connected =
        app_services.has_mouse_3d_controller() && app_services.mouse_3d_controller().is_connected();
    if (mouse_3d_connected == m_mouse_3d_page_visible) {
        return;
    }

    m_mouse_3d_page_visible = mouse_3d_connected;

    const ConfigTabPtr& config_tab = m_config_tabs.front();
    const size_t shown_page_index  = config_tab->tab->pages_stack_layout->current_index();
    const bool mouse_3d_page_shown =
        config_tab->observable_categorizer->at(shown_page_index).config_item->def().category
        == Domain::ConfigItemDef::Category::AppConfig_Mouse3D;
    config_tab->observable_categorizer->invalidate();
    if (mouse_3d_page_shown) {
        config_tab->tab->page_list_view->item_at(0)->callbacks().action();
    }
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
