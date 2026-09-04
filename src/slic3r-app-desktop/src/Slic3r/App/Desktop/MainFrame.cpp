#include "MainFrame.hpp"
#include "MainFrameDropTarget.hpp"

#include "Slic3r/Version.hpp"
#include "Slic3r/Directories.hpp"

#include "Slic3r/App/Desktop/LeftBar.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include <Slic3r/App/AppServices.hpp>
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/ProjectSaver.hpp"
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>
#include <Slic3r/App/WX/format.hpp>
#include <Slic3r/App/WX/I18N.hpp>
#include <Slic3r/App/WX/MsgDialog.hpp>
#include <Slic3r/App/WX/WebView/WebViewFactory.hpp>

#include <Slic3r/App/Localization.hpp>
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/Browser/BrowserLogicPrintables.hpp"
#include "Slic3r/App/Browser/BrowserLogicConnectPage.hpp"
#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"
#include "Slic3r/App/Browser/BrowserLogicPhysicalPrinter.hpp"

#include "Slic3r/App/Platform/AbstractRenderModule.hpp"

#include "Slic3r/App/Scene/Scene.hpp"

#include "Slic3r/App/WX/Scalable.hpp"
#include "Slic3r/App/WX/WindowMetrics.hpp"

#ifdef USE_NATIVE_MENU
#include "Slic3r/App/WX/MacOSNativeMenuBar.hpp"
#endif

#include <Slic3r/Biz/Platform/Termination.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"


#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/string.h>
#include <wx/display.h>
#include <wx/toplevel.h>

#ifdef WIN32
#include <windows.h>
#include <dbt.h>
#include <shlobj.h>
#endif

namespace Slic3r::App::Desktop {

using namespace WX;

// just temporary function to test color mode / font size testing
#ifdef TOP_BAR
static void add_experimets_page(TopBar* top_bar, MainFrame* main_frame)
#else
static void add_experimets_page(TabsBar* top_bar, MainFrame* main_frame)
#endif
{
    wxPanel* test_panel = new wxPanel(top_bar, wxID_ANY);
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    test_panel->SetSizer(main_sizer);
    main_sizer->SetSizeHints(test_panel);

    wxBoxSizer* test_sizer = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(test_sizer, 0, wxEXPAND);

    wxStaticText* test_txt = new wxStaticText(test_panel, wxID_ANY, from_u8("Change: "));
    test_sizer->Add(test_txt, 0, wxALIGN_CENTRE_VERTICAL | wxALL, 20);

    ScalableButton* test_btn = new ScalableButton(test_panel, wxID_ANY, "cog", _L("Color mode"));
    test_btn->SetFont(w_config()->bold_font());
    ScalableButton* test_btn2 = new ScalableButton(test_panel, wxID_ANY, "edit", _L("Apply"));
    test_btn2->SetFont(w_config()->bold_font());

    ScalableButton* lang_selection_btn = new ScalableButton(test_panel, wxID_ANY, "language", _L("Select the language"), wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, 24);
    lang_selection_btn->SetFont(w_config()->bold_font());

    test_sizer->Add(test_btn, 0, wxALIGN_CENTRE_VERTICAL | wxALL, 20);

    wxBoxSizer* test_sizer2 = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(test_sizer2, 0, wxEXPAND);

    main_sizer->Add(lang_selection_btn, 0, wxALL, 40);

    wxStaticText* test_txt2 = new wxStaticText(test_panel, wxID_ANY, from_u8("Text size: "));
    test_sizer2->Add(test_txt2, 0, wxALIGN_CENTRE_VERTICAL | wxALL, 20);

    wxTextCtrl* edit_font = new wxTextCtrl(
        test_panel,
        wxID_ANY,
        wxString::Format(from_u8("%d"), w_config()->normal_font().GetPointSize())
    );
    test_sizer2->Add(edit_font, 0, wxALIGN_CENTRE_VERTICAL | wxALL, 20);

    test_sizer2->Add(test_btn2, 0, wxALIGN_CENTRE_VERTICAL | wxALL, 20);

    test_btn->Bind(
        wxEVT_BUTTON,
        [=](wxCommandEvent& e)
        {
            main_frame->sys_color_changed();

            test_btn->sys_color_changed();
            test_btn2->sys_color_changed();
            lang_selection_btn->sys_color_changed();
            test_panel->Refresh();
        }
    );

    test_btn2->Bind(
        wxEVT_BUTTON,
        [=](wxCommandEvent& e)
        {
            int font_sz;
            edit_font->GetValue().ToInt(&font_sz);

            if (w_config()->normal_font().GetPointSize() != font_sz) {
                wxFont font = w_config()->normal_font();
                font.SetPointSize(font_sz);
                w_config()->update_fonts(font, w_config()->em_unit());
                w_config()->force_fonts_update(main_frame, true);
            }

            test_panel->Layout();
        }
    );

    lang_selection_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) { main_frame->select_language(); });

#ifdef DEBUG
    top_bar->AddPage(test_panel, from_u8("UI - test"));
#endif
}

#if _WIN32
// Loads all sizes embedded in the exe's icon resource directly from the module image
// (see "2 ICON ..." in slic3r-app-launcher.rc.in - the "2" here must match that ID).
static wxIconBundle main_frame_icon_bundle()
{
    return wxIconBundle(wxT("#2"), ::GetModuleHandle(nullptr));
}
#else // _WIN32
// Load the icon from the ico file.
static wxIcon main_frame_icon()
{
    return wxIcon(WX::from_u8(var("PrusaSlicer_128px.png")), wxBITMAP_TYPE_PNG);
}
#endif // _WIN32

MainFrame::MainFrame(
    Domain::Workbench& workbench,
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    std::shared_ptr<ProjectSaver> project_saver
) :
    wxFrame(nullptr, wxID_ANY, from_u8(::Slic3r::BUILD_ID), wxDefaultPosition,wxDefaultSize,
        wxDEFAULT_FRAME_STYLE, from_u8("mainframe")),
    m_workbench(workbench),
    m_project_interactor(project_interactor),
    m_preset_interactor(project_interactor.preset_interactor()),
    m_navigator(navigator),
    m_project_saver(project_saver),
    m_projects_changed_listener_scope(m_project_interactor, *this)
{
    // AppInstanceCheck on Windows expects "PrusaSlicer" in the title
    // (in AppInstanceMessageHandlerWin32.cpp). Better check it.
    ASSERT(into_u8(this->GetTitle()).find("PrusaSlicer") != std::string::npos);

    // Load the icon either from the exe, or from the ico file.
#if _WIN32
    SetIcons(main_frame_icon_bundle());
#else
    SetIcon(main_frame_icon());
#endif

    WidgetsConfig* config = WidgetsConfig::instance();
    SetBackgroundColour(config->get_window_default_clr());
    SetForegroundColour(config->get_label_clr_default());

    AppServices::instance().app_config_interactor().add_listener<IAppConfigChangedListener>(this);
    project_interactor.physical_printer_interactor().add_listener<Biz::PhysicalPrinter::IPhysicalPrinterChangedListener>(this);

    localization().add_listener<ILanguageChangedListener>(this);
    auto em = w_config()->em_unit();

    const wxSize min_size = FromDIP(wxSize(110 * em, 60 * em));
    this->SetMinSize(min_size);
    this->SetSize(min_size);

    wxFont font = w_config()->normal_font();
    w_config()->update_fonts(font, w_config()->em_unit());

    this->SetFont(w_config()->normal_font());

    init_left_bar(project_interactor);
    complete_and_bind_left_bar();

    m_tabs_bar_menus.set_account_menu_callbacks(
        [this, &project_interactor]()
        {
            if (!project_interactor.user_account_interactor().is_logged_in()) {
                AppServices::instance().dialog_manager().show_webview_dialog(
                    std::make_unique<Browser::BrowserLogicLogInRedirect>(project_interactor.user_account_interactor()),
                    &project_interactor
                );
                if (project_interactor.raise_app_fn()) {
                    project_interactor.raise_app_fn()();
                }

            } else {
                project_interactor.user_account_interactor().do_log_out();
            }
        },
        [&project_interactor]()
        {
            return TabsBarMenus::UserAccountInfo{
                project_interactor.user_account_interactor().is_logged_in(),
                project_interactor.user_account_interactor().username(),
                project_interactor.user_account_interactor().avatar()
            };
        }
    );

    project_interactor.user_account_interactor().set_update_menu_callback(
        [this](bool avatar)
        {
            m_tabs_bar_menus.UpdateAccountMenu();
            m_left_bar->GetLeftBarCtrl()->UpdateAccountButton(avatar);
        }
    );

    project_interactor.set_raise_app_fn(
        [this]()
        {
            bool was_maximized = IsMaximized();
            this->Show(true);
            CallAfter([this, was_maximized]() {
                this->Restore();
                this->Raise();
                this->SetFocus();
                // Maximize call fixes 2 issues.
                // - On windows the window did de-maximize without reason.
                // - On linux the windows did not come forward at all.
                this->Maximize(was_maximized);
            });
        }
    );

    this->Bind(
        wxEVT_SYS_COLOUR_CHANGED,
        [this](wxSysColourChangedEvent& event)
        {
            event.Skip();
            m_left_bar->OnColorsChanged();
        }
    );

#ifndef __WXOSX__
    this->Bind(
        wxEVT_DPI_CHANGED,
        [this](wxDPIChangedEvent& event)
        {
            event.Skip();
            m_left_bar->Rescale();

            if (IsMaximized()) {
                // When maximized, the OS does not send a real WM_SIZE on DPI change,
                // so wxBookCtrlBase never re-distributes space between its control strip
                // and pages. Sending a size event to m_left_bar triggers that re-layout.
                m_left_bar->SendSizeEvent();
            }
        }
    );
#endif

    Bind(
        wxEVT_SHOW,
        [this](wxShowEvent& event)
        {
            wxTheApp->CallAfter([this]() {
#if _WIN32
                // Explorer sometimes caches a stale taskbar-button icon if it's set
                // before the button exists; re-apply once the window is really on screen.
                this->SetIcons(main_frame_icon_bundle());
#endif
#ifdef USE_NATIVE_MENU
                this->setup_macos_native_menu_bar();
#endif
                this->update_accel_table();
            });
            event.Skip();
        }
    );

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::on_close, this);

    Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& event)
        {
#ifdef _WIN32
    // TODO
    // wxGetApp().other_instance_message_handler()->update_windows_properties(this);
#endif // WIN32
            bool compact = event.GetSize().GetWidth() < 1500;
            m_left_bar->set_compact_mode(compact);
            event.Skip();
        }
    );

    persist_window_geometry(this, true);

    // We enable or disable the accelerator table depending on whether the canvas has focus.
    //
    // Reason:
    // wxAcceleratorTable intercepts wxEVT_KEY_DOWN and wxEVT_CHAR events before they reach
    // child controls. As a result, wxGLCanvas receives only wxEVT_KEY_UP, while KEY_DOWN and
    // CHAR are lost whenever an accelerator matches the pressed key.
    //
    // To allow the canvas to process raw keyboard input (e.g. camera controls) while it is
    // focused, we temporarily disable the accelerator table on wxEVT_SET_FOCUS, and restore it
    // on wxEVT_KILL_FOCUS. This ensures:
    //
    // * When the canvas IS focused -> it receives full keyboard events (DOWN/UP/CHAR)
    // * When the canvas is NOT focused -> global accelerators function normally
    //
    // This is the only cross-platform way to combine wxGLCanvas keyboard handling with
    // application-wide accelerators in wxWidgets.
    m_canvas->Bind(
        wxEVT_SET_FOCUS,
        [this](wxFocusEvent&)
        {
            set_accel_table();
            // Canvas now receives normal key events again
        }
    );
    m_canvas->Bind(
        wxEVT_KILL_FOCUS,
        [this](wxFocusEvent& e)
        {
            e.Skip();
            set_accel_table();
            // Events from acceleration table will be processed now
        }
    );

    m_navigator.callbacks().render_module_switched = [this]()
    {
        wxTheApp->CallAfter([this]() {
#ifdef USE_NATIVE_MENU
            // No-op unless the module taking over is the first one carrying menus.
            this->setup_macos_native_menu_bar();
#endif
            this->update_accel_table();
        });
    };

    m_navigator.callbacks().modal_dialog_changed = [this](ModalDialog dialog)
    {
        m_left_bar->GetLeftBarCtrl()->preferences_btn()->Enable(
            dialog == ModalDialog::Preferences || dialog == ModalDialog::None
        );
        m_left_bar->GetLeftBarCtrl()->account_btn()->Enable(dialog == ModalDialog::None);
        m_left_bar->GetTabsBarCtrl()->enable_buttons(dialog == ModalDialog::None);

        m_left_bar->GetLeftBarCtrl()->preferences_btn()->set_selected(
            dialog == ModalDialog::Preferences
        );
    };

    SetDropTarget(new MainFrameDropTarget(project_interactor, m_navigator, [this]() {
        // Nothing can be loaded before the presets are known to be usable.
        if (!m_navigator.has_modules()) {
            return false;
        }
        wxWindow* page = m_left_bar->GetCurrentPage();
        return page && page->GetId() == static_cast<wxWindowID>(LeftBarTabs::Slicing);
    }));
}

MainFrame::~MainFrame()
{
    localization().remove_listener<ILanguageChangedListener>(this);
}

void MainFrame::on_language_changed()
{
    // Save language at application config.
    // app_config->set("translation_language", localization().active_language());

    this->Refresh();
}

void MainFrame::on_app_config_changed(const std::string& key)
{
    AppConfig& app_config = AppServices::instance().app_config();

    if (key == "enable_printables") {
        if (bool printables_enabled = AppServices::instance().app_config().is_printables_enabled();
            m_printables_page_added != printables_enabled)
        {
            update_printables_left_bar(printables_enabled);
        }
    }

    if (key == "enable_prusa_account") {
        if (bool acc_enabled = AppServices::instance().app_config().is_prusa_account_enabled();
            acc_enabled != m_project_interactor.user_account_interactor().is_enabled())
        {
            m_project_interactor.user_account_interactor().init(acc_enabled);
        }
    }

    if (key == "graphics_quality") {
        update_graphics_settings();
    }
    else if (key == "translation_language") {
        if (const std::string new_language = app_config.get<std::string>("translation_language");
            localization().active_language() != new_language)
        {
            IDialogManager& dialog_manager = AppServices::instance().dialog_manager();

            // If something was failed during the set new language:
            std::string message = fmt::format(
                fmt::runtime(
                    Biz::_u8L(
                        "The selected language \"{}\" has been saved and will be applied the next time the application starts."
                    )
                ),
                localization().language_description(new_language)
            );
            // Show info dialog
            dialog_manager.show_info_dialog(message, Biz::_u8L("PrusaSlicer - Switching language"));
        }
    } else if (key == "theme") {
        AppServices::instance().dialog_manager().show_info_dialog(
            Biz::_u8L(
                "The selected theme has been saved and will be applied the next time the application starts."
            ),
            Biz::_u8L("PrusaSlicer - Switching theme")
        );
    } else if (key == "sentry") {
        if (app_config.get<bool>("initialized")) {
            const bool enabled{app_config.get<bool>("sentry")};

            const std::string message{
                Biz::_u8L("The change will take effect after restarting the application.")};

            AppServices::instance().dialog_manager().show_info_dialog(
                enabled ?
                    // TRN {} is message
                    fmt::format(fmt::runtime(Biz::_u8L("Crash reporting is now enabled. {}")),
                                message) :
                    // TRN {} is message
                    fmt::format(fmt::runtime(Biz::_u8L("Crash reporting is now disabled. {}")),
                                message),
                Biz::_u8L("PrusaSlicer - Enable crash reporting"));
        }
    }
}

void MainFrame::on_close(wxCloseEvent& event)
{
    if (!event.CanVeto() || // Close(true) or some system-initiated closes cannot be cancelled.
        !m_project_interactor.backup_store().is_any_project_unsaved())
    {
        event.Skip();
        Slic3r::Biz::Platform::close();
        return;
    }

    MessageDialog dialog{
        this,
        from_u8(Biz::_u8L("There are unsaved changes.")),
        from_u8(Biz::_u8L("Close application")),
        wxYES_NO | wxCANCEL | wxICON_EXCLAMATION
    };

    dialog.SetYesNoCancelLabels(
        from_u8(Biz::_u8L("Save")),
        from_u8(Biz::_u8L("Discard")),
        from_u8(Biz::_u8L("Cancel"))
    );

    switch (dialog.ShowModal()) {
    case wxID_YES:
        if (m_project_saver->save_unsaved_projects()) {
            event.Skip();
            Slic3r::Biz::Platform::close();
        } else {
            // Saving failed, or the user cancelled a nested file dialog.
            event.Veto();
        }
        break;

    case wxID_NO:
        event.Skip();
        Slic3r::Biz::Platform::close();
        break;

    case wxID_CANCEL:
    default:
        event.Veto();
        break;
    }
}

void MainFrame::update_accel_table()
{
#ifdef USE_NATIVE_MENU
    if (m_native_menu_bar) {
        wxMenuBar* menu_bar = m_native_menu_bar->get_menu_bar();
        if (auto* accel = menu_bar->GetAcceleratorTable()) {
            m_accel_table = *accel;
        }
        if (!m_accel_table_window) {
            m_accel_table_window = menu_bar;
        }
    }
#else
    std::vector<wxAcceleratorEntry> entries;
    entries.reserve(100);

    auto add_entry = [&entries, this](const std::string& cmd_id, Platform::ICommand* cmd_ptr) {
        if (!cmd_ptr->keyboard_shortcuts().has_value()) {
            // menus without shortcut
            return;
        }

        std::vector<std::string>  keyboard_shortcuts = cmd_ptr->keyboard_shortcut_accel_string();
        for (const std::string& keyboard_shortcut : keyboard_shortcuts) {
            int entry_id = wxNewId();
            if (auto entry{ std::unique_ptr<wxAcceleratorEntry>{wxAcceleratorEntry::Create(
                    WX::from_u8("\t" + keyboard_shortcut))} })
            {
                entries.emplace_back(entry->GetFlags(), entry->GetKeyCode(), entry_id);

                this->Bind(
                    wxEVT_MENU,
                    [cmd_ptr, this](wxCommandEvent& event)
                    {
                        if (!m_canvas->HasFocus()) {
                            m_canvas->SetFocus();
                        }
                        if (cmd_ptr->enabled()) {
                            cmd_ptr->execute();
                        }
                    },
                    entry_id
                );
            }
        }
    };

    for (const auto& [cmd_id, cmd] : m_canvas->get_render_module()->commands()) {
        add_entry(cmd_id, cmd.get());
    }

    if (m_canvas->get_render_module()->is_gizmo_manager_completed()) {
        for (const auto& [cmd_id, cmd] : m_canvas->get_render_module()->gizmo_commands()) {
            add_entry(cmd_id, cmd.get());
        }
    }

    // Add here something extra into acceleration table, if we need it

    m_accel_table = wxAcceleratorTable(entries.size(), entries.data());
    if (!m_accel_table_window) {
        m_accel_table_window = this;
    }
#endif

    set_accel_table();
}

void MainFrame::set_accel_table()
{
    if (!m_accel_table_window) {
        return;
    }

    if (m_canvas->HasFocus()) {
        m_accel_table_window->SetAcceleratorTable(wxNullAcceleratorTable);
    } else {
        m_accel_table_window->SetAcceleratorTable(m_accel_table);
    }
}

void MainFrame::init_left_bar(Biz::ProjectInteractor& project_interactor)
{
    m_left_bar = LeftBar::Create(this, &m_tabs_bar_menus);
    m_left_bar->set_compact_mode(true);

    init_slicing_page();
    init_printers_page(project_interactor);
    init_printables_page(project_interactor);
    init_preferences_button();

    //! experiments just for UI testing
    add_experimets_page(m_left_bar, this);
}

void MainFrame::init_preferences_button()
{
    m_left_bar->GetLeftBarCtrl()->preferences_btn()->Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent& event)
        {
            wxWindow* selected_page        = m_left_bar->GetCurrentPage();
            const bool is_slicing_selected = selected_page
                && selected_page->GetId() == static_cast<wxWindowID>(LeftBarTabs::Slicing);
            if (m_left_bar->GetLeftBarCtrl()->preferences_btn()->is_selected()
                && !is_slicing_selected)
            {
                // just switch to the Slicing page
                switch_left_tab(LeftBarTabs::Slicing, std::string());
                return;
            }

            const bool newly_selected =
                !m_left_bar->GetLeftBarCtrl()->preferences_btn()->is_selected();
            if (newly_selected && !is_slicing_selected) {
                switch_left_tab(LeftBarTabs::Slicing, std::string());
            }
            m_navigator.set_modal_dialog(
                newly_selected ? ModalDialog::Preferences : ModalDialog::None
            );
        }
    );
}

static size_t get_tab_insert_index(LeftBar* left_bar, LeftBarTabs page_id)
{
    for (size_t idx = 0; idx < left_bar->GetPageCount(); ++idx) {
        if (left_bar->GetPage(idx)->GetId() > static_cast<wxWindowID>(page_id)) {
            return idx;
        }
    }
    return left_bar->GetPageCount();
}

void MainFrame::init_printers_page(Biz::ProjectInteractor& project_interactor)
{
    if (!AppServices::instance().app_config().is_prusa_account_enabled()) {
        return;
    }
    assert(!m_printers_page_added);
    std::unique_ptr<App::Browser::BrowserLogicConnectPage> logic = std::make_unique<App::Browser::BrowserLogicConnectPage>(project_interactor);
    WX::WebView::AbstractWebViewPanel* webview_panel = WebView::new_web_view_panel(m_left_bar, static_cast<int>(LeftBarTabs::Printers), std::move(logic), false);
    project_interactor.user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(webview_panel);
    m_left_bar->InsertNewPage(get_tab_insert_index(m_left_bar, LeftBarTabs::Printers), webview_panel, WX::_L("Connect"), "lb_printers");
    webview_panel->set_switch_left_tab_fn(std::bind(&MainFrame::switch_left_tab, this, std::placeholders::_1, std::placeholders::_2));
    m_printers_page_added = true;
}

void MainFrame::init_slicing_page()
{
    m_canvas = std::make_unique<Platform::WX::WXRenderCanvas>(m_left_bar,  static_cast<int>(LeftBarTabs::Slicing));
    m_left_bar->AddNewPage(m_canvas.get(), WX::_L("Slicing"), "lb_slicing");
}

void MainFrame::init_printables_page(Biz::ProjectInteractor& project_interactor)
{
    if (!AppServices::instance().app_config().is_printables_enabled()) {
        return;
    }
    assert(!m_printables_page_added);
    std::unique_ptr<App::Browser::BrowserLogicPrintables> logic = std::make_unique<App::Browser::BrowserLogicPrintables>(project_interactor);
    WX::WebView::AbstractWebViewPanel* webview_panel = WebView::new_web_view_panel(m_left_bar, static_cast<int>(LeftBarTabs::Printables), std::move(logic), false);
    project_interactor.user_account_interactor().add_listener<Biz::UserAccount::IUserAccountListener>(webview_panel);
    m_left_bar->InsertNewPage(get_tab_insert_index(m_left_bar, LeftBarTabs::Printables), webview_panel, WX::_L("Printables"), "lb_printables");
    webview_panel->set_switch_left_tab_fn(std::bind(&MainFrame::switch_left_tab, this, std::placeholders::_1, std::placeholders::_2));
    m_printables_page_added = true;
}

static std::string build_physical_printer_url(const std::string& host)
{
    std::string url = host;
    static const std::regex scheme_regex(R"(^[a-zA-Z][a-zA-Z0-9+.-]*://)");
    if (!std::regex_search(url, scheme_regex)) {
        url = "http://" + url;
    }
    return url;
}

void MainFrame::init_physical_printer_page(Biz::ProjectInteractor& project_interactor)
{
    assert(!m_physical_printer_page_added);
    const auto selected_printer = project_interactor.physical_printer_interactor().selected_physical_printer_data();
    const auto* payload = std::get_if<Slic3r::Biz::PhysicalPrinter::PrinterUpload>(&selected_printer.payload);
    if (!payload) {
        return;
    }
    std::string url = build_physical_printer_url(selected_printer.host);
    std::unique_ptr<App::Browser::BrowserLogicPhysicalPrinter> logic = std::make_unique<App::Browser::BrowserLogicPhysicalPrinter>(url, payload->api_key, payload->username, payload->password);
    WX::WebView::AbstractWebViewPanel* webview_panel = WebView::new_web_view_panel(m_left_bar, static_cast<int>(LeftBarTabs::PhysicalPrinter), std::move(logic), false);
    m_left_bar->InsertNewPage(get_tab_insert_index(m_left_bar, LeftBarTabs::PhysicalPrinter), webview_panel, WX::_L("Physical Printer"), "lb_printers");
    webview_panel->set_switch_left_tab_fn(std::bind(&MainFrame::switch_left_tab, this, std::placeholders::_1, std::placeholders::_2));
    m_physical_printer_page_added = true;
}

void MainFrame::complete_and_bind_left_bar()
{
    int slicing_page_id = m_left_bar->FindPage(m_canvas.get());
    m_left_bar->SetSelection(slicing_page_id);

    m_left_bar->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [](wxBookCtrlEvent& e) {});
}

static size_t get_tab_index_by_id(LeftBar* left_bar, LeftBarTabs page_id)
{
    for (size_t id = 0; id < left_bar->GetPageCount(); ++id) {
        if (left_bar->GetPage(id)->GetId() == static_cast<wxWindowID>(page_id)) {
            return id;
        }
    }
    return size_t(-1);
};

void MainFrame::remove_left_bar_page(LeftBarTabs page_id)
{
    int page_index = get_tab_index_by_id(m_left_bar, page_id);
    ASSERT(page_index != size_t(-1));
    m_left_bar->RemovePage(page_index);
}

void MainFrame::ensure_slicing_page_selected()
{
    if (wxWindow* selected_page = m_left_bar->GetCurrentPage();
        selected_page && selected_page->GetId() != static_cast<wxWindowID>(LeftBarTabs::Slicing))
    {
        switch_left_tab(LeftBarTabs::Slicing, std::string());
    }
}

void MainFrame::update_printables_left_bar(bool printables_enabled)
{
    ASSERT(m_left_bar);

    if (!printables_enabled) {
        remove_left_bar_page(LeftBarTabs::Printables);
        m_printables_page_added = false;
    } else if (printables_enabled) {
        init_printables_page(m_project_interactor);
    }

    ensure_slicing_page_selected();
}

void MainFrame::on_user_account_enabled_state_changed(bool is_enabled)
{
    ASSERT(m_left_bar);

    if (m_printers_page_added && !is_enabled) {
        remove_left_bar_page(LeftBarTabs::Printers);
        m_printers_page_added = false;
    } else if (!m_printers_page_added && is_enabled) {
        init_printers_page(m_project_interactor);
    }

    m_left_bar->ShowUserAccount(is_enabled);

    ensure_slicing_page_selected();
}

void MainFrame::on_selected_physical_printer_changed() 
{
    ASSERT(m_left_bar);


    bool show_physicial_printer_page = m_project_interactor.physical_printer_interactor().is_printer_upload_selected();
    
    if (m_physical_printer_page_added) {
        remove_left_bar_page(LeftBarTabs::PhysicalPrinter);
        m_physical_printer_page_added = false;
    }

    if (!m_physical_printer_page_added && show_physicial_printer_page) {
        init_physical_printer_page(m_project_interactor);
    }

    ensure_slicing_page_selected();
}

void MainFrame::on_printer_data_changed()
{
    ASSERT(m_left_bar);

    if (!m_physical_printer_page_added) {
        return;
    }

    auto& physical_printer_interactor = m_project_interactor.physical_printer_interactor();
    if (!physical_printer_interactor.is_printer_upload_selected()) {
        return;
    }

    const auto selected_printer = physical_printer_interactor.selected_physical_printer_data();
    const auto* payload = std::get_if<Slic3r::Biz::PhysicalPrinter::PrinterUpload>(&selected_printer.payload);
    if (!payload) {
        return;
    }

    size_t page_index = get_tab_index_by_id(m_left_bar, LeftBarTabs::PhysicalPrinter);
    if (page_index == size_t(-1)) {
        return;
    }

    auto* webview_panel = dynamic_cast<WX::WebView::AbstractWebViewPanel*>(m_left_bar->GetPage(page_index));
    if (!webview_panel) {
        return;
    }

    auto* logic = dynamic_cast<App::Browser::BrowserLogicPhysicalPrinter*>(webview_panel->browser_logic());
    if (!logic) {
        return;
    }

    logic->update_connection(build_physical_printer_url(selected_printer.host), payload->api_key, payload->username, payload->password);
}

void MainFrame::on_menu_updated()
{
#ifdef USE_NATIVE_MENU
    m_native_menu_bar->build_from_menu_manager();
    this->SetMenuBar(m_native_menu_bar->get_menu_bar());
#endif
}


void MainFrame::switch_left_tab(LeftBarTabs id, const std::string& data)
{
    ASSERT(m_left_bar);

    size_t page_index = get_tab_index_by_id(m_left_bar, id);
    if (page_index == size_t(-1) || page_index >= m_left_bar->GetPageCount()) {
        // This could happen. F.e. notification trying to switch to disabled Printables.
        return;
    }

    if (id == LeftBarTabs::Printables) {
        WebView::AbstractWebViewPanel* webview_panel =
            dynamic_cast<WebView::AbstractWebViewPanel*>(m_left_bar->GetPage(page_index));
        webview_panel->set_next_show_url(data);
    }
    m_left_bar->SetSelection(page_index);
}

void MainFrame::on_project_loaded(Domain::SelectionId project_id)
{
    const Domain::Project* project =
        m_project_interactor.workbench().find_project_by_id(project_id);
    ASSERT(project);

    AppServices::instance().app_config().app_settings_advanced().push_recent_project(
        project->loaded_file_path().string()
    );
}

void MainFrame::on_project_saved(Domain::SelectionId project_id)
{
    const Domain::Project* project =
        m_project_interactor.workbench().find_project_by_id(project_id);
    ASSERT(project);

    AppServices::instance().app_config().app_settings_advanced().push_recent_project(
        project->loaded_file_path().string()
    );
}

void MainFrame::sys_color_changed()
{
    w_config()->force_colors_update(w_config()->dark_mode(), {this});

    m_left_bar->OnColorsChanged();
}

static int GetSingleChoiceIndex(const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    WX::w_config()->UpdateDlgDarkUI(&dialog);
    auto children = dialog.GetChildren();
    for (auto child : children)
        child->SetFont(WX::w_config()->normal_font());

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}

bool MainFrame::select_language()
{
    wxArrayString names;
    auto language_infos = localization().languages();
    names.Alloc(language_infos.size());

    // Some valid language should be selected since the application start up.
    const std::string active_language = localization().active_language();
    int init_selection                = -1;
    for (size_t i = 0; i < language_infos.size(); ++i) {
        if (language_infos[i].canonical_name == active_language)
            // The dictionary matches the active language and country.
            init_selection = i;
        names.Add(WX::from_u8(language_infos[i].description));
    }

    const long index = GetSingleChoiceIndex(_L("Select the language"), _L("Language"), names, init_selection);

    // Try to load a new language.
    if (index != -1 && (init_selection == -1 || init_selection != index)) {
        if (localization().set_language(language_infos[index].canonical_name))
            return true;

        // If something was failed during the set new language:

        wxString message = WX::
            format_wxstr(_L("Switching PrusaSlicer to language %1% failed."), language_infos[index].canonical_name);
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message += "\n" + WX::format_wxstr(_L("You may need to reconfigure the missing locales, likely by running the %1% and %2% commands.\n"), "\"locale-gen\"", "\"dpkg-reconfigure locales\"");
#endif
        MessageDialog(this, message, _L("PrusaSlicer - Switching language failed"), wxOK | wxICON_ERROR);
    }
    return false;
}

static Scene::ShadingType shading_type(App::GraphicsQuality graphics_quality)
{
    switch (graphics_quality) {
    case App::GraphicsQuality::Legacy:
        return Scene::ShadingType::Legacy;
    case App::GraphicsQuality::Low:
        return Scene::ShadingType::Shadows;
    case App::GraphicsQuality::Medium:
        return Scene::ShadingType::AO;
    case App::GraphicsQuality::High:
        return Scene::ShadingType::PBR;
    }
    return Scene::ShadingType::Legacy;
};

void MainFrame::update_graphics_settings()
{
    Scene::Scene::set_shading_type(shading_type(
        AppServices::instance().app_config().get<App::GraphicsQuality>("graphics_quality")
    ));
}

#ifdef WIN32
void MainFrame::register_win32_callbacks()
{
    static GUID GUID_DEVINTERFACE_HID = {0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30};

    // Register USB HID (Human Interface Devices) notifications to trigger the 3DConnexion enumeration.
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {0};
    NotificationFilter.dbcc_size                     = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype               = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid                = GUID_DEVINTERFACE_HID;
    m_hDeviceNotify = ::RegisterDeviceNotification(this->GetHWND(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Using Win32 Shell API to register for media insert / removal events.
    LPITEMIDLIST ppidl;
    if (SHGetSpecialFolderLocation(this->GetHWND(), CSIDL_DESKTOP, &ppidl) == NOERROR) {
        SHChangeNotifyEntry shCNE;
        shCNE.pidl       = ppidl;
        shCNE.fRecursive = TRUE;
        // Returns a positive integer registration identifier (ID).
        // Returns zero if out of memory or in response to invalid parameters.
        m_ulSHChangeNotifyRegister = SHChangeNotifyRegister(
            this->GetHWND(), // Hwnd to receive notification
            SHCNE_DISKEVENTS, // Event types of interest (sources)
            SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED,
            // SHCNE_UPDATEITEM,                                                     // Events of interest - use SHCNE_ALLEVENTS for all events
            WM_USER_MEDIACHANGED, // Notification message to be sent upon the event
            1, // Number of entries in the pfsne array
            &shCNE
        ); // Array of SHChangeNotifyEntry structures that
        // contain the notifications. This array should
        // always be set to one when calling SHChnageNotifyRegister
        // or SHChangeNotifyDeregister will not work properly.
        ASSERT(m_ulSHChangeNotifyRegister != 0); // Shell notification failed
    } else {
        // Failed to get desktop location
        ASSERT(false);
    }

    {
        static constexpr int device_count    = 1;
        RAWINPUTDEVICE devices[device_count] = {0};
        // multi-axis mouse (SpaceNavigator, etc.)
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage     = 0x08;
        if (!RegisterRawInputDevices(devices, device_count, sizeof(RAWINPUTDEVICE))) {
            SPDLOG_ERROR("RegisterRawInputDevices failed");
        }
    }
}
#endif // _WIN32

void MainFrame::window_pos_save(wxTopLevelWindow* window, const std::string& name)
{
    if (name.empty()) {
        return;
    }

    WindowMetrics metrics = WindowMetrics::from_window(window);
    const auto config_key = fmt::format("{}_window_metrics", name);

    AppConfig& app_config = AppServices::instance().app_config();
    app_config.set(config_key, metrics.serialize());
    // save changed app_config here, before all action related to a close of application is processed
    app_config.save();
}

void MainFrame::window_pos_restore(wxTopLevelWindow* window, const std::string& name, bool default_maximized)
{
    if (name.empty()) { return; }
    const std::string config_key = fmt::format("{}_window_metrics", name);

    AppConfig& app_config = AppServices::instance().app_config();
    std::string metrics_string = app_config.get<std::string>(config_key);

    if (metrics_string.empty()) {
        window->Maximize(default_maximized);
        return;
    }

    auto metrics = WindowMetrics::deserialize(metrics_string);
    if (!metrics) {
        window->Maximize(default_maximized);
        return;
    }

    const wxRect& rect = metrics->get_rect();

    if (app_config.get<bool>("restore_win_position")) {
        // workaround for crash related to the positioning of the window on secondary monitor
        app_config.record_crash("restore_win_pos", "restore_win_position");
        window->SetPosition(rect.GetPosition());

        // workaround for crash related to the positioning of the window on secondary monitor
        app_config.record_crash("restore_win_size", "restore_win_position");
        window->SetSize(rect.GetSize());

        // invalidate "crash_reason" value if application wasn't crashed
        app_config.resolve_crash(std::string(), "restore_win_position");
    }
    else {
        window->CenterOnScreen();
    }

    window->Maximize(metrics->get_maximized());
}

void MainFrame::window_pos_sanitize(wxTopLevelWindow* window)
{
    int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    }
    else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

void MainFrame::persist_window_geometry(wxTopLevelWindow* window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [this, name, window](wxCloseEvent& event) {
        window_pos_save(window, name);
        event.Skip();
        });

    window_pos_restore(window, name, default_maximized);

    on_window_geometry(window, [this, window]() {
        window_pos_sanitize(window);
        });
}

#ifdef USE_NATIVE_MENU
void MainFrame::setup_macos_native_menu_bar()
{
    // Only build once
    if (m_native_menu_bar) {
        return;
    }

    auto* render_module = m_canvas->get_render_module();
    if (!render_module) {
        return;
    }

    MenuManager& menu_manager                      = render_module->menu_manager();
    if (menu_manager.empty()) {
        // The module carrying the app through startup has no menus of its own; the menu bar is
        // built once the plater takes over.
        return;
    }
    CommandBindingManager& command_binding_manager = render_module->command_binding_manager();

    m_native_menu_bar = std::make_unique<MacOSNativeMenuBar>(
        m_project_interactor,
        menu_manager,
        command_binding_manager,
        [this]() { m_canvas->SetFocus(); }
    );

    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(
        m_native_menu_bar.get()
    );
    m_project_interactor.user_account_interactor()
        .add_listener<Biz::UserAccount::IUserAccountListener>(m_native_menu_bar.get());
    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        m_native_menu_bar.get()
    );
    m_project_interactor.removable_drive_service().add_status_listener(m_native_menu_bar.get());

    menu_manager.add_listener<IMenuUpdatedListener>(this);

    m_native_menu_bar->build_from_menu_manager();
    this->SetMenuBar(m_native_menu_bar->get_menu_bar());

    // Setup Apple menu (About, Preferences, Quit) - must be after SetMenuBar
    m_native_menu_bar->setup_apple_menu();
}
#endif

} // namespace Slic3r::App::Desktop
