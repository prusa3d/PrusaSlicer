#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Yoga {
class ScrollArea;
}

namespace Slic3r::App {

class AddPrinterPanel : public Yoga::Item, public Biz::Preset::IPresetChangedListener
{
public:
    struct Printer
    {
        std::string preset_item_id;
        Domain::Preset::HwPrinterConfig default_config;
        Biz::Preset::PresetInteractor::ToolItems tools;
        Biz::Preset::PresetInteractor::SheetItems sheets;
    };

    struct PrinterEntry
    {
        Printer printer;
        std::string preset_name;
    };

    struct PrinterFamily
    {
        std::string label;
        std::string vendor_name;
        std::string base_model;
        std::vector<PrinterEntry> printers;
    };

    AddPrinterPanel(Biz::ProjectInteractor& project_interactor,
                     std::function<void()> close,
                     std::function<void(const Printer&)> add_printer);
    ~AddPrinterPanel() override;

    void on_preset_bundles_loaded() override
    {
        reload();
    }

private:
    Biz::ProjectInteractor& m_project_interactor;
    std::function<void(const Printer&)> m_add_printer;
    std::string m_search_text;
    std::optional<std::string> m_selected_vendor;
    std::vector<PrinterFamily> m_printer_families;

    Yoga::ButtonGroup m_vendor_button_group;
    std::map<Yoga::AbstractButton*, std::string> m_button_vendors;

    Yoga::ScrollArea* m_left_bar{nullptr};
    Yoga::Item* m_printers{nullptr};
    Yoga::Item* m_detail{nullptr};

    Yoga::ItemPtr create_printer_family(
        const PrinterFamily& printer_family,
        const std::vector<const PrinterEntry*>& printers
    );

    void reload();
    void reload_vendor_buttons(const Domain::Preset::Bundle& preset_bundle);
    void rebuild_printer_families();
    void append_vendor_printer_families(
        const Domain::Preset::Bundle& preset_bundle,
        const Domain::Preset::VendorBundle& vendor_bundle
    );
    void rebuild_printer_view();
    void clear_printer_items();
};
} // namespace Slic3r::App
