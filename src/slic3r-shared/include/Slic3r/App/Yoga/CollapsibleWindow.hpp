#pragma once

#include "Slic3r/App/Yoga/Window.hpp"

namespace Slic3r::App::Yoga {

class Text;
class LayoutButton;
class Separator;

class CollapsibleWindow : public Window
{
public:
    struct CollapsibleWindowCallbacks
    {
        std::function<void(bool collapsed)> collapsed_changed{nullptr};
    };

    explicit CollapsibleWindow(const std::string& label, const std::string& window_name);

    template <class T, class... Args>
    T* emplace_into_header(Args&&... args)
    {
        std::unique_ptr<T> item = std::make_unique<T>(std::forward<Args>(args)...);
        T* item_raw             = item.get();
        m_header_row->insert(std::move(item), m_header_row->object_count() - 1);
        return item_raw;
    }

    const std::string& label() const;
    void set_label(const std::string& label);

    bool collapsed() const;
    void set_collapsed(bool collapse);

    void set_flex_grow(float flex);

    Item* content() const;

    CollapsibleWindowCallbacks& collapsible_window_callbacks();

private:
    CollapsibleWindowCallbacks m_callbacks;

    bool m_collapsed{false};
    Item* m_header_row{nullptr};
    Text* m_label{nullptr};
    LayoutButton* m_collapse_button{nullptr};
    Item* m_content{nullptr};
    Separator* m_separator{nullptr};
    float m_set_flex_grow{0};
};

} // namespace Slic3r::App::Yoga
