#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/InputText.hpp>
#include <Slic3r/App/Yoga/Validator.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

// Helper: parent a new InputText to the fixture's window with a fixed size
// and margin so that Yoga places it at (200, 200) with size (300 x 30).
static InputText* make_input(YogaComponentFixture& f)
{
    InputText* input = f.window->emplace_back<InputText>();
    input->set_width(300.f);
    input->set_height(30.f);
    input->set_margin(Margins(200.f, 200.f, 0.f, 0.f));
    return input;
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: active_changed fires true after request_focus")
{
    InputText* input = make_input(*this);

    std::vector<bool> active_events;
    input->callbacks().active_changed = [&](bool v) { active_events.push_back(v); };

    render();
    input->request_focus();
    flush_focus();

    REQUIRE(input->active());
    REQUIRE(active_events.size() >= 1);
    REQUIRE(active_events.front() == true);
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: focus_gained fires after request_focus")
{
    InputText* input = make_input(*this);

    int focus_gained_count         = 0;
    input->callbacks().focus_gained = [&] { ++focus_gained_count; };

    render();
    input->request_focus();
    flush_focus();

    REQUIRE(focus_gained_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: text_changed fires when characters are typed")
{
    InputText* input = make_input(*this);

    int text_changed_count         = 0;
    input->callbacks().text_changed = [&] { ++text_changed_count; };

    render();
    input->request_focus();
    flush_focus();

    type_text("hello");

    REQUIRE(text_changed_count >= 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: text reflects typed characters")
{
    InputText* input = make_input(*this);

    render();
    input->request_focus();
    flush_focus();

    type_text("hello");

    REQUIRE(input->text() == "hello");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: text_entered fires on Enter key while active")
{
    InputText* input = make_input(*this);

    int text_entered_count         = 0;
    input->callbacks().text_entered = [&] { ++text_entered_count; };

    render();
    input->request_focus();
    flush_focus();

    key_tap(ImGuiKey_Enter);

    REQUIRE(text_entered_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: text_changed fires for each typed batch")
{
    InputText* input = make_input(*this);

    int text_changed_count         = 0;
    input->callbacks().text_changed = [&] { ++text_changed_count; };

    render();
    input->request_focus();
    flush_focus();

    type_text("abc");
    type_text("def");

    REQUIRE(text_changed_count == 2);
    REQUIRE(input->text() == "abcdef");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: set_text is reflected in text()")
{
    InputText* input = make_input(*this);

    input->set_text("initial");
    REQUIRE(input->text() == "initial");

    input->set_text("updated");
    REQUIRE(input->text() == "updated");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputText: hovered is true when mouse is over the field")
{
    InputText* input = make_input(*this);

    render();
    const Vec2f pos = input->get_global_pos();
    const float cx  = pos.x() + input->width() * 0.5f;
    const float cy  = pos.y() + input->height() * 0.5f;

    mouse_move(cx, cy);
    render();

    REQUIRE(input->hovered());
}
