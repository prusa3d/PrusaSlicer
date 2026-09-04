#pragma once
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include "Slic3r/App/Yoga/PrinterSettingsButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::App::Yoga {
class Icon;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PhysicalPrinterSettingsButton :
    public Yoga::PrinterSettingsButton,
    public Biz::DataObserver<Biz::PhysicalPrinter::PhysicalPrinterConfig>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    PhysicalPrinterSettingsButton(
        size_t index,
        const Biz::PhysicalPrinter::PhysicalPrinterConfig& physical_printer,
        FnIndexClicked on_clicked,
        FnIndexClicked on_cog_clicked,
        FnIndexClicked on_bin_clicked
    );

    void update_button_text();

    void set_icon(Render::Icon icon);

    void set_bin_supported(bool is_supported);
    void set_visible_expand_icon(bool is_visible);
    std::function<void()>& on_bin();

    void update();

    std::string uuid() const;

    void set_compatible(bool compatible);

protected:
    void on_data_update() override;

    void update_btns_visibility() override;

private:
    void set_visible_bin(bool is_visible);

    FnIndexClicked m_on_clicked;
    FnIndexClicked m_on_cog_clicked;
    FnIndexClicked m_on_bin_clicked;

    Yoga::LayoutButton* m_bin_btn{nullptr};
    Yoga::Icon* m_expand_icon{nullptr};
    bool m_is_visible_bin{false};
    bool m_is_bin_supported{true};

    Yoga::Icon* m_attention_icon{nullptr};
};

} // namespace Slic3r::App
