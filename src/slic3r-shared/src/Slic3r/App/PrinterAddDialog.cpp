#include "Slic3r/App/PrinterAddDialog.hpp"

#include "Slic3r/App/AddPrinterPanel.hpp"
#include "Slic3r/App/Navigator.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrinterAddDialog::PrinterAddDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog({"Add printer"}, "PrinterAddDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    content_item()->set_width(100_ww);
    content_item()->set_height(100_wh);
    content_item()->set_max_width(800_fpx);
    content_item()->set_max_height(600_fpx);
    content_item()->set_padding(0);
    m_add_printer_panel = content()->emplace_back<AddPrinterPanel>(
        m_project_interactor,
        nullptr,
        [this](const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer)
        {
            m_project_interactor.physical_printer_interactor().create_or_save_physical_printer(
                printer
            );
            m_navigator.set_opened_dialog(nullptr);
        }
    );
    m_add_printer_panel->set_flex_grow(1);
}
} // namespace Slic3r::App
