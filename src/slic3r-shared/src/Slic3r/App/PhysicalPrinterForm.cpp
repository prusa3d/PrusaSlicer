#include "Slic3r/App/PhysicalPrinterForm.hpp"

#include "Slic3r/Domain/ConfigPhysical.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"

#include <fmt/format.h>

#include <memory>
#include <variant>
#include <vector>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::PhysicalPrinter;

namespace Slic3r::App {

namespace {

constexpr Unit STATUS_LINE_HEIGHT = 1.3_rem;
constexpr Unit ICON_BUTTON_SIZE   = 24_fpx;

constexpr Domain::PrintHostType HOST_TYPES[] = {
    Domain::PrusaLink,
    Domain::SL1Host,
    Domain::OctoPrint,
    Domain::Moonraker,
    Domain::Duet,
    Domain::FlashAir,
    Domain::AstroBox,
    Domain::Repetier,
    Domain::MKS,
};

int host_type_to_index(Domain::PrintHostType type)
{
    for (int i = 0; i < static_cast<int>(std::size(HOST_TYPES)); ++i) {
        if (HOST_TYPES[i] == type) {
            return i;
        }
    }
    return 0;
}

Domain::PrintHostType host_type_from_index(int index)
{
    if (index < 0 || index >= static_cast<int>(std::size(HOST_TYPES))) {
        return Domain::PrusaLink;
    }
    return HOST_TYPES[index];
}

std::vector<Domain::PrintHostAuthType> supported_auth_types(Domain::PrintHostType host_type)
{
    using Auth = Domain::PrintHostAuthType;

    switch (host_type) {
    case Domain::PrusaLink:
    case Domain::PrusaLinkStorage:
    case Domain::SL1Host:
        return {Auth::ApiKey, Auth::Digest};
    case Domain::Moonraker:
        return {Auth::None, Auth::ApiKey, Auth::Digest};
    case Domain::FlashAir:
    case Domain::MKS:
        return {Auth::None, Auth::ApiKey};
    case Domain::OctoPrint:
    case Domain::Duet:
    case Domain::AstroBox:
    case Domain::Repetier:
        return {Auth::ApiKey};
    }
    return {Auth::ApiKey};
}

std::string auth_type_label(Domain::PrintHostAuthType type)
{
    switch (type) {
    case Domain::PrintHostAuthType::None:
        return _u8L("None");
    case Domain::PrintHostAuthType::ApiKey:
        return _u8L("API key");
    case Domain::PrintHostAuthType::Digest:
        return _u8L("HTTP digest");
    }
    return {};
}

} // namespace

PhysicalPrinterForm::PhysicalPrinterForm(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor),
    m_physical_printer_interactor(project_interactor.physical_printer_interactor()),
    m_sheet_observable_list(std::make_shared<Biz::Preset::SheetConfigItemObservableList>()),
    m_tool_observable_list(std::make_shared<Biz::Preset::ToolConfigItemCompoundObservableList>())
{
    set_object_name("PhysicalPrinterForm");

    build_form();
}

PhysicalPrinterForm::~PhysicalPrinterForm()
{
    m_nozzle_list_view->set_source_list(nullptr, true);
    m_physical_printer_interactor.cancel_edited_printer_connection_test();
}

void PhysicalPrinterForm::set_printer_config(
    const Biz::PhysicalPrinter::PhysicalPrinterConfig& config
)
{
    m_config = config;

    clear_test_status();
    update_test_button_state();

    if (!std::holds_alternative<FileSystemExport>(config.payload)
        && !std::holds_alternative<PrinterUpload>(config.payload))
    {
        return;
    }

    m_loading = true;

    Biz::Preset::PresetInteractor::SheetItems sheet_items;
    std::vector<Biz::Preset::ToolConfigItemObservableList> result;

    if (m_config.hw_config.has_value()) {
        sheet_items =
            m_project_interactor.preset_interactor().get_sheet_items(m_config.hw_config.value());
        m_sheet_observable_list->items().set_items(sheet_items.sheet_defs);
        if (!sheet_items.sheet_defs.empty()) { // SLA doesn't yet have sheets specified
            m_sheet_observable_list->set_selected_index(sheet_items.selected_sheet);
        }

        Biz::Preset::PresetInteractor::ToolItems tool_items =
            m_project_interactor.preset_interactor().get_tool_items(m_config.hw_config.value());

        const std::vector<Domain::Preset::HwToolConfigDef>& items{tool_items.tool_defs};


        result.reserve(tool_items.selected_tools.size());
        for (std::size_t i{}; i < tool_items.selected_tools.size(); ++i) {
            const std::size_t& selected_index{tool_items.selected_tools[i]};

            Biz::Preset::ToolConfigItemObservableList& item = result.emplace_back();
            item.items().set_items(items);
            item.set_selected_index(selected_index);
        }
    }
    m_tool_observable_list->set_items(std::move(result));

    m_name->set_text(config.name);
    m_host->set_text(config.host);

    if (const PrinterUpload* up = std::get_if<PrinterUpload>(&config.payload)) {
        load_connection_fields(*up);
    }

    m_loading = false;

    update_field_visibility();
    update_test_button_state();
    update_save_button_state();
}

PhysicalPrinterForm::Callbacks& PhysicalPrinterForm::callbacks()
{
    return m_callbacks;
}

void PhysicalPrinterForm::build_form()
{
    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_padding(Paddings{15_fpx, 0, 20_fpx, 10_fpx});

    auto test = [this]
    {
        if (!m_loading) {
            test_edits();
            update_test_button_state();
        }
    };

    auto make_row = [&](Item* container, const std::string& label) -> std::tuple<Item*, Item*>
    {
        Item* group = container->emplace_back<Item>();
        group->set_orientation(Orientation::Vertical);
        group->set_gap(5_fpx);
        group->set_flex_shrink(0);

        Text* text = group->emplace_back<Text>(label);
        text->set_font_type(Render::ImguiFontType::Bold);
        text->set_width_percent(100);
        text->set_wrap_mode(Text::WrapMode::WrapElide);

        Item* row = group->emplace_back<Item>();
        row->set_gap(5_fpx);
        return {group, row};
    };

    auto add_input = [&](Item* container,
                         Item*& group_out,
                         const std::string& label,
                         const std::string& tooltip = {}) -> InputTextField*
    {
        auto [group, row]     = make_row(container, label);
        InputTextField* input = row->emplace_back<InputTextField>();
        input->set_flex_grow(1);
        if (!tooltip.empty()) {
            input->set_tooltip(tooltip);
        }
        input->callbacks().text_edited = test;
        group_out                      = group;
        return input;
    };

    Item* unused_group = nullptr;

    m_name = add_input(
        this,
        unused_group,
        _u8L("Name"),
        _u8L("User given name to distinguish configurations.")
    );
    m_name->set_flex_shrink(0);
    m_name->callbacks().text_changed = [this] { update_save_button_state(); };

    emplace_back<Text>(_u8L("Sheet"), Render::ImguiFontType::Bold);
    m_combo_sheets =
        emplace_back<Yoga::ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>>();
    m_combo_sheets->set_get_name_fn(
        [](const Domain::Preset::HwSheetConfigDef* data) -> std::string { return data->name; }
    );
    m_combo_sheets->set_source_list(m_sheet_observable_list.get());
    m_combo_sheets->set_flex_shrink(0);

    // TODO: @zaraka what is point of this callback, the hw_config we create in this form is not yet
    // set to SelectedPreset, but this call updates SelectedPreset's hw_config's sheet
    // m_combo_sheets->callbacks().selection_changed = [this](int sheet_index)
    // {
    //     if (sheet_index >= 0) {
    //         m_project_interactor.preset_interactor().select_printer_sheet(
    //             m_project_interactor.preset_interactor().sheet_items().items().at(sheet_index).id,
    //             true
    //         );
    //     }
    // };

    Text* label = emplace_back<Text>(_u8L("Nozzles"), Render::ImguiFontType::Bold);
    label->set_margin(Margins(0, 10_fpx, 0, 0));
    label->set_flex_shrink(0);

    m_nozzle_list_view =
        emplace_back<NozzleListView>(NozzleListView::ViewFactoryType{m_tool_observable_list.get()});
    m_nozzle_list_view->set_flex_shrink(0);
    m_nozzle_list_view->set_orientation(Orientation::Vertical);
    m_nozzle_list_view->set_gap(5_fpx);
    m_nozzle_list_view->set_source_list(m_tool_observable_list.get());

    m_add_connection_button = emplace_back<LayoutButton>(
        Biz::_u8L("Add Connection"),
        Render::Icon::Plus,
        Biz::_u8L("Add network connection to this printer.")
    );
    m_add_connection_button->callbacks().action = [this]
    {
        m_config.payload = PrinterUpload{};
        load_connection_fields(std::get<PrinterUpload>(m_config.payload));
        update_field_visibility();
        update_test_button_state();
    };
    m_add_connection_button->set_flex_shrink(0);

    m_connection_box = emplace_back<Rectangle>();
    m_connection_box->set_fill(m_theme->color_imgui(Platform::Color::Transparent));
    m_connection_box->set_border_color(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    m_connection_box->set_border_width(2);
    m_connection_box->set_rounding(5);
    m_connection_box->set_orientation(Orientation::Vertical);
    m_connection_box->set_gap(5_fpx);
    m_connection_box->set_padding(10_fpx);
    m_connection_box->set_flex_shrink(0);

    Item* header = m_connection_box->emplace_back<Item>();
    header->set_orientation(Orientation::Horizontal);
    header->set_gap(5_fpx);
    header->set_flex_shrink(0);

    Text* header_label = header->emplace_back<Text>(Biz::_u8L("Connection"));
    header_label->set_font_type(Render::ImguiFontType::Bold);
    header_label->set_align({AlignH::Left, AlignV::Center});

    m_remove_connection_button = header->emplace_back<LayoutButton>(
        Biz::_u8L("Remove"),
        Render::Icon::Minus,
        Biz::_u8L("Remove network connection from this printer")
    );
    m_remove_connection_button->callbacks().action = [this]
    {
        m_physical_printer_interactor.cancel_edited_printer_connection_test();
        m_config.payload = FileSystemExport{};
        clear_test_status();
        update_field_visibility();
        update_test_button_state();
    };
    m_remove_connection_button->set_flex_shrink(0);

    // Host type combo
    {
        auto [group, row]               = make_row(m_connection_box, _u8L("Host type"));
        std::vector<std::string> labels = {
            "PrusaLink",
            "SL1",
            "OctoPrint",
            "Moonraker",
            "Duet",
            "FlashAir",
            "AstroBox",
            "Repetier",
            "MKS"
        };
        m_host_type = row->emplace_back<ComboBox>(std::move(labels));
        m_host_type->set_flex_grow(1);
        m_host_type->tooltip().set_text(_u8L(
            "PrusaSlicer can upload G-code files to a printer host.\n"
            "This field must contain the kind of the host."
        ));
        m_host_type->callbacks().selection_changed = [this, test](int)
        {
            // Available auth types depend on the host type.
            rebuild_auth_type_options(current_auth_type());
            update_field_visibility();
            test();
        };
    }

    m_host = add_input(
        m_connection_box,
        m_host_group,
        _u8L("Hostname, IP or URL"),
        _u8L(
            "PrusaSlicer can upload G-code files to a printer host.\n"
            "This field should contain the hostname, IP address or URL\n"
            "of the printer host instance.\n"
            "Print host behind HAProxy with basic auth enabled can be accessed\n"
            "by putting the user name and password into the URL\n"
            "in the following format: https://username:password@your-octopi-address/"
        )
    );

    // Authorization type combo (options depend on the host type; populated on load)
    {
        auto [group, row] = make_row(m_connection_box, _u8L("Authorization type"));
        m_auth_group      = group;
        m_auth_type       = m_auth_group->emplace_back<ComboBox>(std::vector<std::string>{});
        m_auth_type->set_flex_grow(1);
        m_auth_type->callbacks().selection_changed = [this, test](int)
        {
            update_field_visibility();
            test();
        };
    }

    m_api_key = add_input(
        m_connection_box,
        m_api_key_group,
        _u8L("API key / password"),
        _u8L(
            "PrusaSlicer can upload G-code files to a printer host.\n"
            "This field should contain the API key or the password\n"
            "required for authentication."
        )
    );
    m_user     = add_input(m_connection_box, m_user_group, _u8L("User"));
    m_password = add_input(m_connection_box, m_password_group, _u8L("Password"));
    m_password->set_input_flags(ImGuiInputTextFlags_Password);
    m_port = add_input(
        m_connection_box,
        m_port_group,
        _u8L("Port"),
        _u8L("Port number. Optional parameter.")
    );
    m_port->set_input_flags(ImGuiInputTextFlags_CharsDecimal);
    m_port->set_validator(std::make_unique<IntValidator>(0, 65535));
    {
        auto [ca_file_row_group, row] = make_row(m_connection_box, _u8L("HTTPS CA file"));
        m_ca_group                    = ca_file_row_group;
        m_ca_file                     = row->emplace_back<InputTextField>();
        m_ca_file->set_flex_grow(1);
        m_ca_file->set_tooltip(_u8L(
            "Custom CA certificate file can be specified for HTTPS OctoPrint connections,\n"
            "in crt/pem format.\n"
            "If left blank, the default OS CA certificate repository is used."

        ));

        m_ca_file->callbacks().text_edited = test;

        m_ca_file_browse_button = row->emplace_back<LayoutButton>(
            std::string{},
            Render::Icon::TobBarLoad,
            // TRN Tooltip of the button that opens a file dialog for the HTTPS CA certificate.
            _u8L("Browse for a CA certificate file.")
        );
        m_ca_file_browse_button->set_min_width(ICON_BUTTON_SIZE);
        m_ca_file_browse_button->set_min_height(ICON_BUTTON_SIZE);
        m_ca_file_browse_button->callbacks().action = [this] { browse_ca_file(); };

        ca_file_row_group->set_visible(Network::IHttp::ca_file_supported());
    }

    // Boolean option: label on the left column, bare toggle on the right.
    {
        auto [group, row] =
            make_row(m_connection_box, _u8L("Ignore HTTPS certificate revocation checks"));
        m_ssl_ignore_revoke = row->emplace_back<ToggleButton>(
            std::string{},
            _u8L(
                "Ignore HTTPS certificate revocation checks in case of missing\n"
                "or offline distribution points.\n"
                "One may want to enable this option for self-signed certificates\n"
                "if connection fails."
            )
        );
        m_ssl_ignore_revoke->callbacks().checked_changed = [test](bool) { test(); };
#ifndef _WIN32
        // The revocation check option is only meaningful on Windows.
        group->set_visible(false);
#endif
    }

    m_test_status_row = emplace_back<Item>();
    m_test_status_row->set_orientation(Orientation::Vertical);
    m_test_status_row->set_visible(false);

    m_test_status_row->emplace_back<Separator>(Orientation::Horizontal);

    Item* status_text_area = m_test_status_row->emplace_back<Item>();
    status_text_area->set_orientation(Orientation::Vertical);
    status_text_area->set_padding(10_fpx);

    m_test_status = status_text_area->emplace_back<Text>(std::string{});
    m_test_status->set_wrap_mode(Text::WrapMode::WrapElide);
    m_test_status->set_font_size(1_rem);
    m_test_status->set_height(STATUS_LINE_HEIGHT);

    m_test_status_detail = status_text_area->emplace_back<Text>(std::string{});
    m_test_status_detail->set_wrap_mode(Text::WrapMode::WrapElide);
    m_test_status_detail->set_font_size(1_rem);
    m_test_status_detail->set_height(STATUS_LINE_HEIGHT * 3.f);
    m_test_status_detail->set_visible(false);

    m_test_status_row->emplace_back<Separator>(Orientation::Horizontal);

    Item* footer = emplace_back<Item>();
    footer->set_justify_content(YGJustifyFlexEnd);
    footer->set_align_items(YGAlignCenter);
    footer->set_gap(5_fpx);

    // TRN Button that verifies the connection to the configured print host.
    m_test_button = footer->emplace_back<LayoutButton>(_u8L("Test"));
    m_test_button->set_content_padding({10_fpx, 4_fpx});
    m_test_button->callbacks().action = [this]
    {
        m_test_button->set_enabled(false);
        // TRN Status shown in the printer dialog while the connection test is running.
        set_test_status(_u8L("Testing connection..."), {}, Platform::Color::Text);
        m_physical_printer_interactor.test_printer_connection(
            edited_config(),
            [this](PrintHostTestResult result) { on_connection_test_finished(std::move(result)); }
        );
    };

    m_save_button = footer->emplace_back<LayoutButton>(
        _u8L("Save"),
        Render::Icon::TobBarSave,
        Biz::_u8L("Save this printer configuration")
    );
    m_save_button->set_content_padding({10_fpx, 4_fpx});
    m_save_button->set_content_align_items(YGAlignCenter);
    // Icon has no intrinsic size, without it the icon collapses next to the label
    m_save_button->icon_object()->set_width(14_fpx);
    m_save_button->icon_object()->set_height(14_fpx);
    m_save_button->set_background_color(Platform::Color::AccentPrimary);
    m_save_button->set_label_font_type(Render::ImguiFontType::Bold);
    m_save_button->callbacks().action = [this] { commit_edits(); };
    update_save_button_state();
}

void PhysicalPrinterForm::test_edits()
{
    if (!has_valid_name()) {
        set_test_status(
            _u8L("Printer name cannot be empty"),
            std::string{},
            Platform::Color::Error
        );
    }

    // Todo evaluate preset check
}

void PhysicalPrinterForm::commit_edits()
{
    if (!has_valid_name()) {
        update_save_button_state();
        return;
    }

    m_physical_printer_interactor.cancel_edited_printer_connection_test();
    clear_test_status();
    update_test_button_state();

    m_config = edited_config(true);

    if (m_callbacks.printer_saved) {
        m_callbacks.printer_saved(m_config);
    }
}

PhysicalPrinterConfig PhysicalPrinterForm::edited_config(bool update_hw_config) const
{
    PhysicalPrinterConfig config = m_config;

    // update hw settings
    if (update_hw_config && config.hw_config.has_value())
    {
        const auto& vendor_bundle =
            m_project_interactor.workbench().preset_bundle().vendor_bundles.at(
                config.hw_config->vendor_id
            );

        ASSERT(m_tool_observable_list->size() == config.hw_config->tools.size());
        ASSERT(m_tool_observable_list->size() == config.hw_config->tool_count);
        for (size_t i = 0; i < m_tool_observable_list->size(); i++) {
            const auto& tool_list          = m_tool_observable_list->at(i);
            const auto& selected_toold_def = tool_list.items().at(tool_list.selected_index());
            if (config.hw_config->tools.at(i).id != selected_toold_def.id) {
                config.hw_config->tools[i] =
                    Preset::from_def(vendor_bundle.vendor_data, selected_toold_def);
            }
        }

        if (config.hw_config.value().technology == Domain::PrinterTechnology::FFF) {
            const auto& selected_sheet_def =
                m_sheet_observable_list->items().at(m_sheet_observable_list->selected_index());
            if (config.hw_config->sheet.id == selected_sheet_def.id) {
                config.hw_config->sheet =
                    Preset::from_def(vendor_bundle.vendor_data, selected_sheet_def);
            }
        }
        config.hw_config->name = Domain::Preset::
            suggest_name(config.hw_config.value(), vendor_bundle.vendor_data, false);
        config.hw_config->short_name =
            Domain::Preset::suggest_name(config.hw_config.value(), vendor_bundle.vendor_data, true);
    }

    PrinterUpload* up = std::get_if<PrinterUpload>(&config.payload);
    if (up) {
        up->type                   = host_type_from_index(m_host_type->current_index());
        up->auth_type              = current_auth_type();
        up->api_key                = m_api_key->text();
        up->username               = m_user->text();
        up->password               = m_password->text();
        up->port                   = m_port->text();
        up->ca_file                = m_ca_file->text();
        up->ssl_revoke_best_effort = m_ssl_ignore_revoke->checked();
    }

    config.name = m_name->text();
    config.host = m_host->text();

    return config;
}

void PhysicalPrinterForm::update_field_visibility()
{
    const Domain::PrintHostAuthType auth = current_auth_type();

    const bool is_local     = std::holds_alternative<FileSystemExport>(m_config.payload);
    const bool show_api_key = auth == Domain::PrintHostAuthType::ApiKey;
    const bool show_digest  = auth == Domain::PrintHostAuthType::Digest;

    m_connection_box->set_visible(!is_local);
    m_api_key_group->set_visible(show_api_key);
    m_user_group->set_visible(show_digest);
    m_password_group->set_visible(show_digest);
    m_add_connection_button->set_visible(is_local);
    m_test_button->set_visible(!is_local);
    m_port_group->set_visible(m_host_type->current_index() == Domain::Repetier - 1);
}

void PhysicalPrinterForm::load_connection_fields(const PrinterUpload& upload)
{
    const bool prev_loading = m_loading;
    m_loading               = true;
    m_host_type->set_current_index(host_type_to_index(upload.type));
    rebuild_auth_type_options(upload.auth_type);
    m_api_key->set_text(upload.api_key);
    m_user->set_text(upload.username);
    m_password->set_text(upload.password);
    m_port->set_text(upload.port);
    m_ca_file->set_text(upload.ca_file);
    m_ssl_ignore_revoke->set_checked(upload.ssl_revoke_best_effort);
    m_loading = prev_loading;
}

void PhysicalPrinterForm::rebuild_auth_type_options(Domain::PrintHostAuthType desired)
{
    m_auth_options = supported_auth_types(host_type_from_index(m_host_type->current_index()));

    std::vector<std::string> labels;
    labels.reserve(m_auth_options.size());
    for (Domain::PrintHostAuthType type : m_auth_options) {
        labels.push_back(auth_type_label(type));
    }

    int index = 0;
    for (int i = 0; i < static_cast<int>(m_auth_options.size()); ++i) {
        if (m_auth_options[i] == desired) {
            index = i;
            break;
        }
        if (m_auth_options[i] == Domain::PrintHostAuthType::ApiKey) {
            index = i;
        }
    }

    // Suppress the combo callbacks while repopulating; callers refresh visibility.
    const bool prev_loading = m_loading;
    m_loading               = true;
    m_auth_type->set_items(labels);
    m_auth_type->set_current_index(index);
    m_loading = prev_loading;
}

Domain::PrintHostAuthType PhysicalPrinterForm::current_auth_type() const
{
    const int index = m_auth_type->current_index();
    if (index < 0 || index >= static_cast<int>(m_auth_options.size())) {
        return Domain::PrintHostAuthType::None;
    }
    return m_auth_options[index];
}

void PhysicalPrinterForm::update_test_button_state()
{
    m_test_button->set_enabled(
        m_physical_printer_interactor.is_connection_testable(edited_config())
        && !m_physical_printer_interactor.is_connection_test_in_flight()
    );
}

bool PhysicalPrinterForm::has_valid_name() const
{
    return m_name->text().find_first_not_of(" \t\r\n") != std::string::npos;
}

void PhysicalPrinterForm::update_save_button_state()
{
    m_save_button->set_enabled(has_valid_name());
}

void PhysicalPrinterForm::on_connection_test_finished(PrintHostTestResult result)
{
    update_test_button_state();

    if (result.ok) {
        set_test_status(
            // TRN {} is the print host name, e.g. "OctoPrint".
            fmt::format(fmt::runtime(_u8L("Connection to {} works correctly.")), result.host_name),
            {},
            Platform::Color::Success
        );
        return;
    }

    set_test_status(
        // TRN {} is the print host name, e.g. "OctoPrint".
        fmt::format(fmt::runtime(_u8L("Could not connect to {}")), result.host_name),
        result.error_message,
        Platform::Color::Error
    );
}

void PhysicalPrinterForm::set_test_status(
    const std::string& text,
    const std::string& detail,
    Platform::Color color
)
{
    m_test_status->set_text(text);
    m_test_status->set_text_color(m_theme->color_imgui(color));

    m_test_status_detail->set_text(detail);
    m_test_status_detail->set_visible(!detail.empty());

    m_test_status_row->set_visible(true);
}

void PhysicalPrinterForm::clear_test_status()
{
    m_test_status->set_text(std::string{});
    m_test_status_detail->set_text(std::string{});
    m_test_status_detail->set_visible(false);
    m_test_status_row->set_visible(false);
}

void PhysicalPrinterForm::browse_ca_file()
{
    IDialogManager::FileCallback callback =
        [this](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (!success || file_paths.empty()) {
            return;
        }
        m_ca_file->set_text(file_paths.front().string());
        commit_edits();
    };

    AppServices::instance().dialog_manager().show_file_dialog(
        FileDialogType::Open,
        // TRN Title of the file dialog that picks an HTTPS CA certificate file.
        _u8L("Open CA certificate file"),
        AppServices::instance().app_config().get<std::string>("last_used_directory"),
        std::string{},
        Wildcards::generate_wildcards(
            Wildcards::TypeFlag::Certificate | Wildcards::TypeFlag::AllFiles
        ),
        callback
    );
}

} // namespace Slic3r::App
