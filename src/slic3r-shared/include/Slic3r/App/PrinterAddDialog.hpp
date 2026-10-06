#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class AddPrinterPanel;
class Navigator;

class PrinterAddDialog : public Yoga::Dialog
{
public:
    explicit PrinterAddDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    AddPrinterPanel* m_add_printer_panel{nullptr};
};

} // namespace Slic3r::App
