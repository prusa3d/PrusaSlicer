#include "Slic3r/App/Preview/SidebarPreviewActionButtons.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/ResultExport/ExportActions.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Preview {

std::unique_ptr<LayoutButton> SidebarPreviewActionButtons::get_primary_button()
{
    auto result{std::make_unique<LayoutButton>("Plater", Render::Icon::None, "Back to Plater")};
    result->set_label_font_type(Render::ImguiFontType::Bold);
    result->set_background_color(Platform::Color::AccentSecondary);
    result->set_min_height(button_height);
    result->set_flex_grow(1);
    result->callbacks().action = [this]() { navigate_to_other(); };
    return result;
}

SidebarPreviewActionButtons::SidebarPreviewActionButtons(Navigator* render_module_navigator) :
    SidebarActionButtons("SidebarPreviewActionButtons", Render::ModuleType::Preview, render_module_navigator)
{}

SidebarPreviewActionButtons::~SidebarPreviewActionButtons()
{
    m_project_interactor->status_cache().remove_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor->scene_interactor()
        .remove_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor->removable_drive_service().remove_status_listener(this);
}

void SidebarPreviewActionButtons::on_init(Biz::ProjectInteractor* project_interactor)
{
    m_project_interactor = project_interactor;
    m_project_interactor->status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor->scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        this
    );
    m_project_interactor->removable_drive_service().add_status_listener(this);

    init_physical_printer_ui();

    auto layout_bottom = m_buttons_layout->emplace_back<Item>();
    layout_bottom->set_orientation(Orientation::Horizontal);
    layout_bottom->set_gap(10_fpx);

    auto navigation_button = get_navigation_button();
    m_navigation_button = navigation_button.get();
    m_navigation_button->set_visible(false);
    layout_bottom->append(std::move(navigation_button));

    auto primary_button = get_primary_button();
    m_primary_button = primary_button.get();
    layout_bottom->append(std::move(primary_button));

    update_buttons();
}

void SidebarPreviewActionButtons::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection& bed_selection
)
{
    update_buttons();
}

void SidebarPreviewActionButtons::on_status_cache_status_code_changed(const Domain::SlicingId slicing_id)
{
    const Domain::SlicingId current_id{m_project_interactor->selected_bed_slicing_id()};
    if (current_id != slicing_id) {
        return;
    }
    update_buttons();
}

void SidebarPreviewActionButtons::on_removable_drive_status_changed(
    const boost::filesystem::path&,
    Biz::RemovableDrive::RemovableDriveStatus s
)
{
    update_buttons();
}

void SidebarPreviewActionButtons::update_buttons()
{
    const std::string export_tooltip{Biz::_u8L("Export gcode to a file")};

    const Domain::SlicingId slicing_id{m_project_interactor->selected_bed_slicing_id()};
    const auto optional_status{m_project_interactor->status_cache().get_status(slicing_id)};
    if (!optional_status) {
        return;
    }

    const Biz::Slicing::Status status{*optional_status};

    m_primary_button->set_background_color(Platform::Color::AccentPrimary);
    m_primary_button->set_label_color(m_theme->color_imgui(Platform::Color::Text));
    m_primary_button->set_background_color_border(
        m_theme->color_imgui(Platform::Color::Transparent)
    );
    m_primary_button->set_background_border_width(0.f);
    m_primary_button->callbacks().action = []() {};
    m_navigation_button->set_visible(true);

    using Biz::Slicing::StatusCode;
    switch (status.code) {
    case StatusCode::Running: {
        m_primary_button->set_label(Biz::_u8L("Cancel"));
        m_primary_button->set_enabled(true);
        m_primary_button->callbacks().action = [this, slicing_id]()
        {
            m_project_interactor->slicing_interactor().stop_slicing_bed(slicing_id);
            const auto auto_reslice{AppServices::instance().app_config().get<bool>("auto_reslice")};
            if (auto_reslice) {
                navigate_to_other();
            }
        };
    } break;
    case StatusCode::InvalidData: {
        ASSERT(!status.errors.empty());
        m_primary_button->set_label(Biz::_u8L("Invalid settings"));
        m_primary_button->set_background_color(Platform::Color::WindowBgAlternate);
        m_primary_button->set_label_color(m_theme->color_imgui(Platform::Color::Error));
        m_primary_button->set_background_color_border(m_theme->color_imgui(Platform::Color::Error));
        m_primary_button->set_background_border_width(2.f);
        m_primary_button->set_enabled(true);

        m_primary_button->callbacks().action = [this]()
        {
            if (m_render_module_navigator) {
                m_render_module_navigator->open_invalid_data_dialog();
            }
        };
    } break;
    case StatusCode::Finished: {
        m_primary_button->set_enabled(true);
        if (m_project_interactor->physical_printer_interactor().is_filesystem_export_selected()) {
            const auto& phys_printer = m_project_interactor->physical_printer_interactor().selected_physical_printer_data();
            const auto* payload = std::get_if<Slic3r::Biz::PhysicalPrinter::FileSystemExport>(&phys_printer.payload);
            ASSERT(payload);
            m_primary_button->set_label(Biz::_u8L("Export"));
            m_primary_button->set_tooltip(export_tooltip);
            m_primary_button->callbacks().action = payload->prefer_removable ? 
                ExportActions::export_gcode_to_flash(*m_project_interactor) :
                ExportActions::export_gcode(*m_project_interactor);
        } else if (m_project_interactor->physical_printer_interactor().is_connect_upload_selected()) {
            m_primary_button->set_label(Biz::_u8L("Send to Connect"));
            m_primary_button->set_tooltip("Send to Connect");
            m_primary_button->callbacks().action = ExportActions::send_gcode_to_connect(*m_project_interactor);
        } else if (m_project_interactor->physical_printer_interactor().is_printer_upload_selected()) {
            m_primary_button->set_label(Biz::_u8L("Send to Printer"));
            m_primary_button->set_tooltip(Biz::_u8L("Send to Printer"));
            m_primary_button->callbacks().action = ExportActions::upload_gcode_to_print_host(*m_project_interactor);
        } else {
            PANIC("Unreachable!");
        }
    } break;
    case StatusCode::Modified: {
        m_primary_button->set_label(Biz::_u8L("Slice"));
        m_primary_button->set_enabled(true);
        m_primary_button->set_tooltip(Biz::_u8L("Slice"));
        m_primary_button->callbacks().action = [this, slicing_id]()
        { m_project_interactor->slicing_interactor().slice_bed(slicing_id); };
    } break;
    default: {
        m_primary_button->set_label("Slice");
        m_primary_button->set_tooltip("Slice");
        m_primary_button->set_enabled(false);
    } break;
    }
}

} // namespace Slic3r::App::Preview
