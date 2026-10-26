#pragma once

#include "Slic3r/Domain/ConfigPhysical.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/App/PrinterNozzleRow.hpp"
#include "Slic3r/App/ThemeTypes.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

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
class Rectangle;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PhysicalPrinterForm : public Yoga::Item
{
public:
    struct Callbacks
    {
        std::function<void(Biz::PhysicalPrinter::PhysicalPrinterConfig& printer_config)>
            printer_saved{nullptr};
    };

    explicit PhysicalPrinterForm(Biz::ProjectInteractor& project_interactor);
    ~PhysicalPrinterForm();

    Callbacks& callbacks();

    void set_printer_config(const Biz::PhysicalPrinter::PhysicalPrinterConfig& config);

private:
    void build_form();
    void test_edits();
    void commit_edits();
    Biz::PhysicalPrinter::PhysicalPrinterConfig edited_config() const;
    void update_field_visibility();
    void update_test_button_state();
    void update_save_button_state();
    bool has_valid_name() const;

    void on_connection_test_finished(Biz::PhysicalPrinter::PrintHostTestResult result);
    void set_test_status(const std::string& text, const std::string& detail, Platform::Color color);
    void clear_test_status();

    void browse_ca_file();

    void load_connection_fields(const Biz::PhysicalPrinter::PrinterUpload& upload);
    void rebuild_auth_type_options(Domain::PrintHostAuthType desired);
    Domain::PrintHostAuthType current_auth_type() const;

private:
    using NozzleListView = Yoga::ListView<
        PrinterNozzleRow,
        Biz::Preset::ToolConfigItemObservableList,
        Yoga::ViewFactory<
            PrinterNozzleRow,
            Biz::Preset::ToolConfigItemObservableList,
            Biz::WeakerPointer<Biz::Preset::ToolConfigItemCompoundObservableList>>>;

    Biz::ProjectInteractor& m_project_interactor;
    Biz::PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;

    Biz::UnsharedPointer<Biz::Preset::SheetConfigItemObservableList> m_sheet_observable_list;
    Biz::UnsharedPointer<Biz::Preset::ToolConfigItemCompoundObservableList> m_tool_observable_list;

    Biz::PhysicalPrinter::PhysicalPrinterConfig m_config;
    Callbacks m_callbacks;

    Yoga::InputTextField* m_name{nullptr};

    Yoga::ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>* m_combo_sheets;
    NozzleListView* m_nozzle_list_view{nullptr};

    Yoga::InputTextField* m_host{nullptr};
    Yoga::ComboBox* m_host_type{nullptr};
    Yoga::ComboBox* m_auth_type{nullptr};

    Yoga::LayoutButton* m_add_connection_button{nullptr};
    Yoga::LayoutButton* m_remove_connection_button{nullptr};
    Yoga::Rectangle* m_connection_box{nullptr};

    std::vector<Domain::PrintHostAuthType> m_auth_options;
    Yoga::InputTextField* m_api_key{nullptr};
    Yoga::InputTextField* m_user{nullptr};
    Yoga::InputTextField* m_password{nullptr};
    Yoga::InputTextField* m_port{nullptr};
    Yoga::InputTextField* m_ca_file{nullptr};
    Yoga::ToggleButton* m_ssl_ignore_revoke{nullptr};

    Yoga::Item* m_api_key_group{nullptr};
    Yoga::Item* m_host_group{nullptr};
    Yoga::Item* m_user_group{nullptr};
    Yoga::Item* m_password_group{nullptr};
    Yoga::Item* m_auth_group{nullptr};
    Yoga::Item* m_port_group{nullptr};
    Yoga::Item* m_ca_group{nullptr};

    Yoga::LayoutButton* m_ca_file_browse_button{nullptr};

    Yoga::Item* m_test_status_row{nullptr};
    Yoga::Text* m_test_status{nullptr};
    Yoga::Text* m_test_status_detail{nullptr};
    Yoga::LayoutButton* m_test_button{nullptr};
    Yoga::LayoutButton* m_save_button{nullptr};

    // Guards the widget callbacks while the form is being populated.
    bool m_loading{false};
};
} // namespace Slic3r::App
