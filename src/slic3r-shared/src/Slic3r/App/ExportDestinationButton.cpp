#include "Slic3r/App/ExportDestinationButton.hpp"

#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

using Biz::ExportDestination::ExportDestination;

namespace {

std::string destination_label(ExportDestination destination)
{
    switch (destination) {
    // TRN Export destination: save G-code to a folder on this computer.
    case ExportDestination::LocalDrive: return Biz::_u8L("Local drive");
    // TRN Export destination: save G-code to a USB drive or SD card.
    case ExportDestination::RemovableDrive: return Biz::_u8L("Removable drive");
    // TRN Export destination: send G-code through Prusa Connect.
    case ExportDestination::Connect: return Biz::_u8L("Prusa Connect");
    // TRN Export destination: upload G-code directly to the selected printer.
    case ExportDestination::PrintHost: return Biz::_u8L("Printer host");
    }
    return {};
}

Render::Icon destination_icon(ExportDestination destination)
{
    switch (destination) {
    case ExportDestination::LocalDrive: return Render::Icon::ExportToLocal;
    case ExportDestination::RemovableDrive: return Render::Icon::ExportToSD;
    case ExportDestination::Connect: return Render::Icon::ConnectUpload;
    case ExportDestination::PrintHost: return Render::Icon::PhysicalPrinterIcon;
    }
    return Render::Icon::None;
}

} // namespace

ExportDestinationButton::ExportDestinationButton(Biz::ExportDestination::ExportDestinationInteractor& interactor) :
    Yoga::LayoutButton(destination_label(interactor.selected()), destination_icon(interactor.selected())),
    m_listener_scope(interactor, *this),
    m_interactor(interactor)
{
    m_menu = emplace_back<Yoga::Menu>("export_destination_menu", Yoga::Position::Bottom);
    for (ExportDestination destination : Biz::ExportDestination::ALL_EXPORT_DESTINATIONS) {
        Yoga::MenuItem* item = m_menu->append_item(destination_label(destination), destination_icon(destination));
        item->callbacks().action = [this, destination]() { m_interactor.select(destination); };
        m_menu_items.emplace_back(destination, item);
    }

    callbacks().action = [this]() { open_menu(); };
}

void ExportDestinationButton::on_export_destination_changed()
{
    refresh();
}

void ExportDestinationButton::refresh()
{
    set_label(destination_label(m_interactor.selected()));
    set_icon(destination_icon(m_interactor.selected()));
}

void ExportDestinationButton::open_menu()
{
    for (const auto& [destination, item] : m_menu_items) {
        item->set_enabled(m_interactor.can_select(destination));
    }
    m_menu->open();
}

} // namespace Slic3r::App
