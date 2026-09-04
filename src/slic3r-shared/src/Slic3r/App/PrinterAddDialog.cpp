#include "Slic3r/App/PrinterAddDialog.hpp"

#include "Slic3r/App/AddPrinterPanel.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrinterAddDialog::PrinterAddDialog(Biz::ProjectInteractor& project_interactor) :
    Dialog({"Add printer"}, "PrinterAddDialog"),
    m_project_interactor(project_interactor)
{
    content_item()->set_width(100_ww);
    content_item()->set_height(100_wh);
    content_item()->set_max_width(800_fpx);
    content_item()->set_max_height(600_fpx);
    content_item()->set_padding(0);
    m_add_printer_panel = content()->emplace_back<AddPrinterPanel>(
        m_project_interactor,
        nullptr,
        [this](const AddPrinterPanel::Printer& printer)
        {
            AppSettingsAdvanced& settings =
                AppServices::instance().app_config().app_settings_advanced();

            settings.printer_favorite_presets.insert(printer.preset_item_id);
            if (m_callbacks.printer_added) {
                m_callbacks.printer_added();
                close();
            }
        }
    );
    m_add_printer_panel->set_flex_grow(1);
}

PrinterAddDialog::Callbacks& PrinterAddDialog::callbacks()
{
    return m_callbacks;
}
} // namespace Slic3r::App
