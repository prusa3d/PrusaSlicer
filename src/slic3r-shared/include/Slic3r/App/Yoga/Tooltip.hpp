#pragma once

#include "Slic3r/App/Yoga/Popup.hpp"

namespace Slic3r::App::Yoga {

class Text;

class Tooltip : public Popup
{
public:
    Tooltip(
        Item* parent,
        const std::string& text,
        const std::string& shortcut,
        const std::string& window_name = {}
    );

    const std::string& text() const;
    void set_text(const std::string& text);

    void set_text_wrap(bool wrap);

    const std::string& shortcut() const;
    void set_shortcut(const std::string& shortcut);

private:
    Text* m_text     = nullptr;
    Text* m_shortcut = nullptr;
};

} // namespace Slic3r::App::Yoga
