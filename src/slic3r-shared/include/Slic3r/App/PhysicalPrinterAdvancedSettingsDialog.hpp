#pragma once

#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"

#include "Slic3r/App/ThemeTypes.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::PhysicalPrinter {
class PhysicalPrinterInteractor;
struct PrintHostTestResult;
} // namespace Slic3r::Biz::PhysicalPrinter

namespace Slic3r::App::Yoga {
class Item;
class InputTextField;
class ComboBox;
class ToggleButton;
class LayoutButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;

class PhysicalPrinterAdvancedSettingsDialog :
    public Yoga::Dialog,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener
{
public:
    explicit PhysicalPrinterAdvancedSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator
    );
    ~PhysicalPrinterAdvancedSettingsDialog();

    void on_selected_physical_printer_changed() override;

protected:
    void close_action() override;

private:
    void on_about_to_show() override;
    void on_about_to_close() override;

    void build_form();
    void load_from_interactor();
    void commit_to_interactor();
    void commit_edits();
    void persist_coerced_auth_type();
    void update_field_visibility();
    void update_test_button_state();

    void on_connection_test_finished(Biz::PhysicalPrinter::PrintHostTestResult result);
    void set_test_status(const std::string& text, const std::string& detail, Platform::Color color);
    void clear_test_status();

    void browse_ca_file();

    void rebuild_auth_type_options(Domain::PrintHostAuthType desired);
    Domain::PrintHostAuthType current_auth_type() const;

private:
    Biz::ListenerScope<
        Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor,
        PhysicalPrinterAdvancedSettingsDialog>
        m_physical_printer_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Biz::PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;
    Navigator& m_navigator;

    Yoga::InputTextField* m_name{nullptr};
    Yoga::InputTextField* m_host{nullptr};
    Yoga::ComboBox* m_host_type{nullptr};
    Yoga::ComboBox* m_auth_type{nullptr};

    std::vector<Domain::PrintHostAuthType> m_auth_options;
    Yoga::InputTextField* m_api_key{nullptr};
    Yoga::InputTextField* m_user{nullptr};
    Yoga::InputTextField* m_password{nullptr};
    Yoga::InputTextField* m_port{nullptr};
    Yoga::InputTextField* m_ca_file{nullptr};
    Yoga::ToggleButton* m_ssl_ignore_revoke{nullptr};

    Yoga::Item* m_api_key_row{nullptr};
    Yoga::Item* m_user_row{nullptr};
    Yoga::Item* m_password_row{nullptr};

    Yoga::LayoutButton* m_ca_file_browse_button{nullptr};

    Yoga::Item* m_test_status_row{nullptr};
    Yoga::Text* m_test_status{nullptr};
    Yoga::Text* m_test_status_detail{nullptr};
    Yoga::LayoutButton* m_test_button{nullptr};
    Yoga::LayoutButton* m_save_button{nullptr};
    Yoga::LayoutButton* m_change_hw_button{nullptr};

    // Guards the widget callbacks while the form is being populated.
    bool m_loading{false};
};
} // namespace Slic3r::App
