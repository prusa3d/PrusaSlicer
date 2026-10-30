#pragma once
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include "Slic3r/App/PrinterSettingsButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include <optional>

namespace Slic3r::App::Yoga {
class Icon;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App {

class PhysicalPrinterSettingsButton :
    public PrinterSettingsButton,
    public Biz::DataObserver<Biz::PhysicalPrinter::PhysicalPrinterConfig>,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    PhysicalPrinterSettingsButton(
        size_t index,
        const Biz::PhysicalPrinter::PhysicalPrinterConfig& physical_printer,
        FnIndexClicked on_clicked,
        FnIndexClicked on_cog_clicked,
        FnIndexClicked on_bin_clicked,
        const Biz::Preset::PresetInteractor& preset_interactor,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor
    );

    void update_button_text();

    void set_icon(Render::Icon icon);

    void set_bin_supported(bool is_supported);
    void set_visible_expand_icon(bool is_visible);
    std::function<void()>& on_bin();

    void update();

    std::string uuid() const;

    void on_selected_physical_printer_changed() override;

    void on_view_will_be_removed() override;

protected:
    void on_data_update() override;

    void update_btns_visibility() override;

private:
    void set_visible_bin(bool is_visible);
    void set_printer_image();
    void update_tooltip();
    void update_selected();

    std::optional<Biz::ListenerScope<
        Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor,
        PhysicalPrinterSettingsButton>>
        m_physical_printer_changed_listener_scope;

    FnIndexClicked m_on_clicked;
    FnIndexClicked m_on_cog_clicked;
    FnIndexClicked m_on_bin_clicked;
    const Biz::Preset::PresetInteractor& m_preset_interactor;
    const Biz::PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;

    Yoga::LayoutButton* m_bin_btn{nullptr};
    Yoga::Icon* m_expand_icon{nullptr};
    bool m_is_visible_bin{false};
    bool m_is_bin_supported{true};

    Yoga::Icon* m_unresolved_model_icon{nullptr};
};

} // namespace Slic3r::App
