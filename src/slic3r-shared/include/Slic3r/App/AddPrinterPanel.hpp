#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ScrollArea;
class Icon;
class StackLayout;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PhysicalPrinterForm;

class AddPrinterPanel : public Yoga::Item, public Biz::Preset::IPresetChangedListener
{
public:
    struct PrinterEntry
    {
        Domain::Preset::HwPrinterConfig printer_config;
        std::string preset_name;
    };

    struct PrinterFamily
    {
        std::string label;
        std::string base_model;
        std::vector<PrinterEntry> printers;
    };

    AddPrinterPanel(
        Biz::ProjectInteractor& project_interactor,
        std::function<void()> close,
        std::function<void(const Biz::PhysicalPrinter::PhysicalPrinterConfig&)> add_printer
    );

    void on_preset_bundles_loaded() override
    {
        reload();
    }

private:
    Biz::ProjectInteractor& m_project_interactor;
    std::function<void(const Biz::PhysicalPrinter::PhysicalPrinterConfig&)> m_add_printer;
    std::string m_search_text;
    std::optional<std::string> m_selected_vendor;
    std::string m_vendor_name;
    std::vector<PrinterFamily> m_printer_families;

    Yoga::ButtonGroup m_vendor_button_group;
    std::map<Yoga::AbstractButton*, std::string> m_button_vendors;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        AddPrinterPanel>
        m_preset_changed_listener_scope;

    Yoga::StackLayout* m_stack_layout{nullptr};

    // Page Search
    Yoga::ScrollArea* m_left_bar{nullptr};
    Yoga::Item* m_printers{nullptr};
    Yoga::Item* m_detail{nullptr};

    // Page Details
    Yoga::Icon* m_printer_image{nullptr};
    Yoga::Text* m_printer_name{nullptr};
    PhysicalPrinterForm* m_physical_printer_form{nullptr};

    // Keep alive the thumbnail textures. Reloading them takes too much time.
    std::queue<std::shared_ptr<Render::Texture>> m_texture_cache;
    std::vector<Yoga::Icon*> m_shown_thumbnails;

    Yoga::ItemPtr create_printer_family(
        const PrinterFamily& printer_family,
        const std::vector<const PrinterEntry*>& printers
    );

    void reload();
    void reload_vendor_buttons(const Domain::Preset::Bundle& preset_bundle);
    void rebuild_printer_families();
    void rebuild_printer_view();
    void clear_printer_items();
    void emplace_vendor_button(const std::string& name, const std::string& id);

    void create_page_search();
    void create_page_details();
};
} // namespace Slic3r::App
