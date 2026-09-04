#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App {

class AddPrinterPanel : public Yoga::Item, public Biz::IListObserver<Biz::Preset::PresetItem>
{
public:
    struct Printer
    {
        std::string preset_item_id;
        Domain::Preset::HwPrinterConfig default_config;
        Biz::Preset::PresetInteractor::ToolItems tools;
        Biz::Preset::PresetInteractor::SheetItems sheets;
    };

    struct PrinterFamily;

    AddPrinterPanel(Biz::ProjectInteractor& project_interactor,
                     std::function<void()> close,
                     std::function<void(const Printer&)> add_printer);
    ~AddPrinterPanel() override;

    void on_reset() override {
        reload();
    }

private:
    Biz::ProjectInteractor& m_project_interactor;
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>> m_sort_filter;
    Biz::ObservableListSearcher<Biz::Preset::PresetItem> m_searcher;
    std::function<void(const Printer&)> m_add_printer;

    Yoga::Item* m_printers{nullptr};
    Yoga::Item* m_detail{nullptr};

    Yoga::ItemPtr create_printer_family(const PrinterFamily& printer_family);

    void reload();
};
} // namespace Slic3r::App
