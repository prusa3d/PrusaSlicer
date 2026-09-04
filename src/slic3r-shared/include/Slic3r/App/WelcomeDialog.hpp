#pragma once

#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/Popup.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

namespace Slic3r::App {

struct PrinterToAdd;

class PrinterScreen;
class LogInScreen;

class WelcomeDialog : public Yoga::Popup
{
public:
    WelcomeDialog(Biz::ProjectInteractor& project_interactor);

private:
    Biz::ProjectInteractor& m_project_interactor;

    Yoga::Item* m_top_bar{nullptr};
    Yoga::Item* m_content{nullptr};
    const Yoga::Item* m_current_screen{nullptr};
    Yoga::ButtonGroup m_online_decision_button_group;
    Yoga::RectangleButton* m_online_button{nullptr};
    Yoga::RectangleButton* m_offline_button{nullptr};

    Yoga::Item* m_changelog_screen{nullptr};
    Yoga::Item* m_online_decision_screen{nullptr};
    Yoga::Item* m_sentry_screen{nullptr};
    LogInScreen* m_login_screen{nullptr};
    PrinterScreen* m_printer_screen{nullptr};

    bool m_online{true};
    bool m_sentry_enabled{false};

    void finalize(const std::vector<PrinterToAdd>& printers);

    void reload_top_bar();

    void go_to_screen(const Yoga::Item* screen);

    void sync_config() const;

    Yoga::ItemPtr create_online_decision_screen(const Theme& theme,
                                                std::function<void()> go_to_prev,
                                                std::function<void()> confirm_choice);

};
} // namespace Slic3r::App::Plater
