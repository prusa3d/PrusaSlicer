#include "Slic3r/App/SearchBar.hpp"

#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/SearchPopup.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

SearchBar::SearchBar(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_search_observable_list(
        std::make_shared<SearchObservableList>(project_interactor.preset_interactor())
    )
{
    m_search_popup = emplace_back<SearchPopup>(
        m_project_interactor,
        m_navigator,
        m_search_observable_list.get()
    );
    m_search_popup->attach_to_item(this, Position::Bottom, 15);

    set_gap(5);
    set_align_items(YGAlignCenter);

    Icon* icon = emplace_back<Icon>(Render::Icon::Search);
    icon->set_width(18);
    icon->set_height(18);

    m_input_text = emplace_back<InputText>();
    m_input_text->set_hint(_u8L("Search..."));
    m_input_text->set_width(150);
    m_input_text->set_flags(ImGuiInputTextFlags_EscapeClearsAll);

    m_input_text->callbacks().text_entered = [this] { m_search_popup->open_selected(); };
    m_input_text->callbacks().text_changed = [this]
    {
        const std::string& text = m_input_text->text();
        m_search_observable_list->set_search_text(text);
        if (!text.empty()) {
            m_search_popup->select_top();
            m_search_popup->open();
            m_search_popup->content_item()->bring_to_front();
        } else {
            m_search_popup->close();
        }
    };
    m_input_text->callbacks().focus_lost   = [this] { update_open_popup(); };
    m_input_text->callbacks().focus_gained = [this]
    {
        const std::string& text = m_input_text->text();
        if (!text.empty()) {
            m_search_popup->select_top();
            m_search_popup->open();
            m_search_popup->content_item()->bring_to_front();
        }
    };
    m_search_popup->content_item()->callbacks().hovered_changed = [this](bool)
    { update_open_popup(); };
}

void SearchBar::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    Item::render(pos, size);

    if (m_input_text->active()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_search_popup->navigate_up();
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            m_search_popup->navigate_down();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            m_search_popup->open_selected();
        }
    }
}

void SearchBar::focus_search()
{
    m_input_text->request_focus();
}

void SearchBar::update_open_popup()
{
    if (!m_search_popup->content_item()->hovered() && !m_input_text->has_focus()) {
        m_search_popup->close();
    }
}

} // namespace Slic3r::App
