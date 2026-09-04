#pragma once

#include "Slic3r/App/Yoga/Popup.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

namespace Slic3r::App::Yoga {

class Text;
class LayoutButton;
class Separator;

class Dialog : public Popup
{
public:
    struct DialogCallbacks
    {
        std::function<void(size_t current_tab)> tab_selected{nullptr};
        std::function<void()> close_requested{nullptr};
    };

    explicit Dialog(const std::string& name = {});
    explicit Dialog(std::initializer_list<std::string> tabs, const std::string& name = {});

    DialogCallbacks& dialog_callbacks();

    bool closable() const;
    void set_closable(bool closable);

    size_t current_tab_index() const;
    void set_current_tab(size_t current_index);

protected:
    Item* content() const;
    LayoutButton* close_button() const;
    Separator* add_separator();
    LayoutButton* append_tab(const std::string& tab);
    void remove_tab(size_t index);

    virtual void on_tab_selected(int current_index);

    virtual void close_action();

private:
    void set_current_tab_internal(size_t current_index);

private:
    bool m_closable = true;

    DialogCallbacks m_callbacks;

    LayoutButton* m_close_button = nullptr;
    Item* m_content              = nullptr;
    Item* m_top_row              = nullptr;
    Item* m_tab_container        = nullptr;
    size_t m_current_tab_index   = 0;

    std::vector<LayoutButton*> m_tab_buttons;
    ButtonGroup m_tab_button_group;
};

} // namespace Slic3r::App::Yoga
