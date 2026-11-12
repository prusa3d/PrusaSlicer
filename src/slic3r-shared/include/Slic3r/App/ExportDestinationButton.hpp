#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/Biz/ExportDestination/ExportDestination.hpp"
#include "Slic3r/Biz/ExportDestination/ExportDestinationInteractor.hpp"
#include "Slic3r/Biz/ExportDestination/IExportDestinationChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include <utility>
#include <vector>

namespace Slic3r::App::Yoga {
class Menu;
class MenuItem;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ExportDestinationButton :
    public Yoga::LayoutButton,
    public Biz::ExportDestination::IExportDestinationChangedListener
{
public:
    explicit ExportDestinationButton(Biz::ExportDestination::ExportDestinationInteractor& interactor);

    void on_export_destination_changed() override;

private:
    void refresh();
    void open_menu();

    Biz::ListenerScope<
        Biz::ExportDestination::IExportDestinationChangedListener,
        Biz::ExportDestination::ExportDestinationInteractor,
        ExportDestinationButton>
        m_listener_scope;

    Biz::ExportDestination::ExportDestinationInteractor& m_interactor;
    Yoga::Menu* m_menu{nullptr};
    std::vector<std::pair<Biz::ExportDestination::ExportDestination, Yoga::MenuItem*>> m_menu_items;
};

} // namespace Slic3r::App
