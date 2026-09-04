#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class AddPrinterPanel;

class PrinterAddDialog : public Yoga::Dialog
{
public:
    struct Callbacks
    {
        std::function<void()> printer_added{nullptr};
    };

    explicit PrinterAddDialog(Biz::ProjectInteractor& project_interactor);

    Callbacks& callbacks();

private:
    Biz::ProjectInteractor& m_project_interactor;

    Callbacks m_callbacks;

    AddPrinterPanel* m_add_printer_panel{nullptr};
};

} // namespace Slic3r::App
