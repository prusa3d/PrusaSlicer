#include "Slic3r/App/PhysicalPrinterAdvancedSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

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
constexpr Unit ICON_BUTTON_SIZE = 24_fpx;

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
    case Domain::PrintHostAuthType::None:   return _u8L("None");
    case Domain::PrintHostAuthType::ApiKey: return _u8L("API key");
    case Domain::PrintHostAuthType::Digest: return _u8L("HTTP digest");
    }
    return {};
}

} // namespace

PhysicalPrinterAdvancedSettingsDialog::PhysicalPrinterAdvancedSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog({_u8L("Printer")}, "PhysicalPrinterAdvancedSettingsDialog"),
    m_physical_printer_changed_listener_scope(project_interactor.physical_printer_interactor(), *this),
    m_project_interactor(project_interactor),
    m_physical_printer_interactor(project_interactor.physical_printer_interactor()),
    m_navigator(navigator)
{
    build_form();
    load_from_interactor();
}

PhysicalPrinterAdvancedSettingsDialog::~PhysicalPrinterAdvancedSettingsDialog()
{
    m_physical_printer_interactor.cancel_edited_printer_connection_test();
}

void PhysicalPrinterAdvancedSettingsDialog::build_form()
{
    content_item()->set_width(440_fpx);

    content()->set_orientation(Orientation::Vertical);
    content()->set_padding(0_fpx);

    ScrollArea* fields = content()->emplace_back<ScrollArea>();
    fields->set_orientation(Orientation::Vertical);
    fields->set_gap(4_fpx);
    fields->set_padding(15_fpx);
    fields->set_flex_grow(1);
    fields->set_max_height(460_fpx);

    auto commit = [this] {
        if (!m_loading) {
            commit_edits();
        }
    };

    auto make_row = [&](const std::string& label) -> Item* {
        Item* row = fields->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_align_items(YGAlignCenter);
        row->set_gap(5_fpx);

        Item* left = row->emplace_back<Item>();
        left->set_max_width(175_fpx);

        Text* text = left->emplace_back<Text>(label);
        text->set_width(175_fpx);
        text->set_height(40_fpx);
        text->set_wrap_mode(Text::WrapMode::WrapElide);
        text->set_align({AlignH::Left, AlignV::Center});
        return row;
    };

    auto add_input = [&](Item*& row_out, const std::string& label, const std::string& tooltip = {}) -> InputTextField* {
        Item* row = make_row(label);
        InputTextField* input = row->emplace_back<InputTextField>();
        input->set_flex_grow(1);
        if (!tooltip.empty()) {
            input->set_tooltip(tooltip);
        }
        input->callbacks().text_edited = commit;
        row_out = row;
        return input;
    };

    Item* unused_row = nullptr;

    m_name = add_input(unused_row, _u8L("Name"),
        _u8L("User given name to distinguish configurations."));
    m_host = add_input(unused_row, _u8L("Hostname, IP or URL"),
        _u8L("Slic3r can upload G-code files to a printer host.\n"
             "This field should contain the hostname, IP address or URL\n"
             "of the printer host instance.\n"
             "Print host behind HAProxy with basic auth enabled can be accessed\n"
             "by putting the user name and password into the URL\n"
             "in the following format: https://username:password@your-octopi-address/"));

    // Host type combo
    {
        Item* row = make_row(_u8L("Host Type"));
        std::vector<std::string> labels = {
            "PrusaLink", "SL1", "OctoPrint", "Moonraker", "Duet",
            "FlashAir", "AstroBox", "Repetier", "MKS"
        };
        m_host_type = row->emplace_back<ComboBox>(std::move(labels));
        m_host_type->set_flex_grow(1);
        m_host_type->tooltip().set_text(
            _u8L("Slic3r can upload G-code files to a printer host.\n"
                 "This field must contain the kind of the host."));
        m_host_type->callbacks().selection_changed = [this, commit](int) {
            // Available auth types depend on the host type.
            rebuild_auth_type_options(current_auth_type());
            update_field_visibility();
            commit();
        };
    }

    // Authorization type combo (options depend on the host type; populated on load)
    {
        Item* row = make_row(_u8L("Authorization Type"));
        m_auth_type = row->emplace_back<ComboBox>(std::vector<std::string>{});
        m_auth_type->set_flex_grow(1);
        m_auth_type->callbacks().selection_changed = [this, commit](int) {
            update_field_visibility();
            commit();
        };
    }

    m_api_key  = add_input(m_api_key_row, _u8L("API Key / Password"),
        _u8L("Slic3r can upload G-code files to a printer host.\n"
             "This field should contain the API Key or the password\n"
             "required for authentication."));
    m_user     = add_input(m_user_row, _u8L("User"));
    m_password = add_input(m_password_row, _u8L("Password"));
    m_password->set_input_flags(ImGuiInputTextFlags_Password);
    m_port     = add_input(unused_row, _u8L("Port"),
        _u8L("Port number. Optional parameter."));
    m_port->set_input_flags(ImGuiInputTextFlags_CharsDecimal);
    m_port->set_validator(std::make_unique<IntValidator>(0, 65535));
    Item* ca_file_row = nullptr;
    m_ca_file  = add_input(ca_file_row, _u8L("HTTPS CA File"),
        _u8L("Custom CA certificate file can be specified for HTTPS OctoPrint connections,\n"
             "in crt/pem format.\n"
             "If left blank, the default OS CA certificate repository is used."));

    m_ca_file_browse_button = ca_file_row->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::TobBarLoad,
        // TRN Tooltip of the button that opens a file dialog for the HTTPS CA certificate.
        _u8L("Browse for a CA certificate file."));
    m_ca_file_browse_button->set_min_width(ICON_BUTTON_SIZE);
    m_ca_file_browse_button->set_min_height(ICON_BUTTON_SIZE);
    m_ca_file_browse_button->callbacks().action = [this] { browse_ca_file(); };

    ca_file_row->set_visible(Network::IHttp::ca_file_supported());

    // Boolean option: label on the left column, bare toggle on the right.
    {
        Item* row = make_row(_u8L("Ignore HTTPS certificate revocation checks"));
        m_ssl_ignore_revoke = row->emplace_back<ToggleButton>(
            std::string{},
            _u8L("Ignore HTTPS certificate revocation checks in case of missing\n"
                 "or offline distribution points.\n"
                 "One may want to enable this option for self signed certificates\n"
                 "if connection fails."));
        m_ssl_ignore_revoke->callbacks().checked_changed = [commit](bool) { commit(); };
#ifndef _WIN32
        // The revocation check option is only meaningful on Windows.
        row->set_visible(false);
#endif
    }

    content()->emplace_back<Separator>(Orientation::Horizontal);

    m_test_status_row = content()->emplace_back<Item>();
    m_test_status_row->set_orientation(Orientation::Vertical);
    m_test_status_row->set_visible(false);

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

    Item* footer = content()->emplace_back<Item>();
    footer->set_padding(10_fpx);
    footer->set_justify_content(YGJustifyFlexEnd);
    footer->set_align_items(YGAlignCenter);
    footer->set_gap(5_fpx);

    // TRN Button that verifies the connection to the configured print host.
    m_test_button = footer->emplace_back<LayoutButton>(_u8L("Test"));
    m_test_button->callbacks().action = [this] {
        m_test_button->set_enabled(false);
        // TRN Status shown in the printer dialog while the connection test is running.
        set_test_status(_u8L("Testing connection..."), {}, Platform::Color::Text);
        m_physical_printer_interactor.test_edited_printer_connection(
            [this](PrintHostTestResult result) { on_connection_test_finished(std::move(result)); }
        );
    };

    m_save_button = footer->emplace_back<LayoutButton>(_u8L("Save"));
    m_save_button->callbacks().action = [this] {
        m_physical_printer_interactor.save_new_printer();
        m_navigator.set_opened_dialog(nullptr);
    };

    // Re-links an existing printer's hardware config to the currently selected logical printer.
    m_change_hw_button = footer->emplace_back<LayoutButton>(_u8L("Use current printer profile"));
    m_change_hw_button->set_visible(false);
    m_change_hw_button->callbacks().action = [this] {
        m_physical_printer_interactor.update_selected_hw_config();
        m_change_hw_button->set_visible(false); // now matches the current logical printer
    };
}

void PhysicalPrinterAdvancedSettingsDialog::load_from_interactor()
{
    clear_test_status();
    update_test_button_state();

    const PhysicalPrinterConfig& printer = m_physical_printer_interactor.edited_printer();
    const PrinterUpload* up = std::get_if<PrinterUpload>(&printer.payload);
    if (!up) {
        return;
    }

    m_loading = true;

    m_name->set_text(printer.name);
    m_host->set_text(printer.host);
    m_host_type->set_current_index(host_type_to_index(up->type));
    rebuild_auth_type_options(up->auth_type);
    m_api_key->set_text(up->api_key);
    m_user->set_text(up->username);
    m_password->set_text(up->password);
    m_port->set_text(up->port);
    m_ca_file->set_text(up->ca_file);
    m_ssl_ignore_revoke->set_checked(up->ssl_revoke_best_effort);

    m_loading = false;

    update_field_visibility();
    update_test_button_state();
    m_save_button->set_visible(m_physical_printer_interactor.is_filesystem_export_selected());
    m_change_hw_button->set_visible(
        m_physical_printer_interactor.is_printer_upload_selected()
        && !m_physical_printer_interactor.selected_hw_matches_current()
    );
}

void PhysicalPrinterAdvancedSettingsDialog::commit_to_interactor()
{
    // Start from the current printer so uuid and hw_config are preserved.
    PhysicalPrinterConfig printer = m_physical_printer_interactor.edited_printer();

    PrinterUpload up;
    if (const PrinterUpload* existing = std::get_if<PrinterUpload>(&printer.payload)) {
        up = *existing;
    }

    up.type                   = host_type_from_index(m_host_type->current_index());
    up.auth_type              = current_auth_type();
    up.api_key                = m_api_key->text();
    up.username               = m_user->text();
    up.password               = m_password->text();
    up.port                   = m_port->text();
    up.ca_file                = m_ca_file->text();
    up.ssl_revoke_best_effort = m_ssl_ignore_revoke->checked();

    printer.payload = std::move(up);
    printer.name    = m_name->text();
    printer.host    = m_host->text();

    m_physical_printer_interactor.set_edited_printer(printer);
}

void PhysicalPrinterAdvancedSettingsDialog::commit_edits()
{
    m_physical_printer_interactor.cancel_edited_printer_connection_test();
    commit_to_interactor();
    clear_test_status();
    update_test_button_state();
}

void PhysicalPrinterAdvancedSettingsDialog::persist_coerced_auth_type()
{
    const PhysicalPrinterConfig& printer = m_physical_printer_interactor.edited_printer();
    const PrinterUpload* up = std::get_if<PrinterUpload>(&printer.payload);
    if (up != nullptr && current_auth_type() != up->auth_type) {
        commit_edits();
    }
}

void PhysicalPrinterAdvancedSettingsDialog::update_field_visibility()
{
    const Domain::PrintHostAuthType auth = current_auth_type();

    const bool show_api_key = auth == Domain::PrintHostAuthType::ApiKey;
    const bool show_digest  = auth == Domain::PrintHostAuthType::Digest;

    m_api_key_row->set_visible(show_api_key);
    m_user_row->set_visible(show_digest);
    m_password_row->set_visible(show_digest);
}

void PhysicalPrinterAdvancedSettingsDialog::rebuild_auth_type_options(Domain::PrintHostAuthType desired)
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
    m_loading = true;
    m_auth_type->set_items(labels);
    m_auth_type->set_current_index(index);
    m_loading = prev_loading;
}

Domain::PrintHostAuthType PhysicalPrinterAdvancedSettingsDialog::current_auth_type() const
{
    const int index = m_auth_type->current_index();
    if (index < 0 || index >= static_cast<int>(m_auth_options.size())) {
        return Domain::PrintHostAuthType::None;
    }
    return m_auth_options[index];
}

void PhysicalPrinterAdvancedSettingsDialog::on_selected_physical_printer_changed()
{
    m_physical_printer_interactor.cancel_edited_printer_connection_test();
    load_from_interactor();
    if (opened()) {
        persist_coerced_auth_type();
    }
}

void PhysicalPrinterAdvancedSettingsDialog::update_test_button_state()
{
    m_test_button->set_enabled(
        m_physical_printer_interactor.is_connection_testable()
        && !m_physical_printer_interactor.is_connection_test_in_flight()
    );
}

void PhysicalPrinterAdvancedSettingsDialog::on_connection_test_finished(PrintHostTestResult result)
{
    update_test_button_state();

    if (result.ok) {
        // TRN {} is the print host name, e.g. "OctoPrint".
        set_test_status(
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

void PhysicalPrinterAdvancedSettingsDialog::set_test_status(
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

void PhysicalPrinterAdvancedSettingsDialog::clear_test_status()
{
    m_test_status->set_text(std::string{});
    m_test_status_detail->set_text(std::string{});
    m_test_status_detail->set_visible(false);
    m_test_status_row->set_visible(false);
}

void PhysicalPrinterAdvancedSettingsDialog::browse_ca_file()
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
        Wildcards::generate_wildcards(Wildcards::TypeFlag::Certificate | Wildcards::TypeFlag::AllFiles),
        callback
    );
}

void PhysicalPrinterAdvancedSettingsDialog::on_about_to_show()
{
    load_from_interactor();
    persist_coerced_auth_type();
}

void PhysicalPrinterAdvancedSettingsDialog::on_about_to_close()
{
    m_physical_printer_interactor.cancel_edited_printer_connection_test();
}

void PhysicalPrinterAdvancedSettingsDialog::close_action()
{
    if (dialog_callbacks().close_requested) {
        dialog_callbacks().close_requested();
    } else {
        m_navigator.set_opened_dialog(nullptr);
    }
}

} // namespace Slic3r::App
