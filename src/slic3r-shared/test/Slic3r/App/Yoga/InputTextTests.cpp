#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/InputText.hpp>
#include <Slic3r/App/Yoga/InputTextWithSpin.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>
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

TEST_CASE_METHOD(YogaComponentFixture, "InputText: nav cursor is hidden after focused editing")
{
    InputText* input_other          = make_input(*this);
    InputText* input                = make_input(*this);
    input->callbacks().text_entered = [input_other]() { input_other->request_focus(); };

    render();
    input->request_focus();
    flush_focus();
    REQUIRE(input->active());

    key_tap(ImGuiKey_Enter);

    flush_focus();

    REQUIRE_FALSE(input->active());
    REQUIRE_FALSE(ImGui::GetCurrentContext()->NavCursorVisible);
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

static InputTextWithSpin* make_spin(Item* parent, const std::string& text)
{
    auto* spin = parent->emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>());
    spin->set_width(280.f);
    spin->set_height(30.f);
    spin->set_flex_shrink(0.f);
    spin->set_text(text);
    return spin;
}

TEST_CASE_METHOD(YogaComponentFixture, "InputTextWithSpin: mouse wheel steps the value")
{
    InputTextWithSpin* spin       = make_spin(window, "2");
    int edited                    = 0;
    spin->callbacks().text_edited = [&]() { ++edited; };

    hover(spin, 0.3f);
    mouse_wheel(1.f);
    REQUIRE(spin->text() == "3");
    mouse_wheel(-1.f);
    REQUIRE(spin->text() == "2");
    REQUIRE(edited == 2);

    key_down(ImGuiMod_Ctrl);
    mouse_wheel(1.f);
    key_up(ImGuiMod_Ctrl);
    REQUIRE(spin->text() == "7");
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "InputTextWithSpin: mouse wheel sums deltas into one step per frame"
)
{
    InputTextWithSpin* spin = make_spin(window, "2");

    hover(spin, 0.3f);
    for (int i = 0; i < 10; ++i) {
        mouse_wheel(0.25f);
    }
    REQUIRE(spin->text() == "4");

    mouse_wheel(2.f);
    REQUIRE(spin->text() == "5");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputTextWithSpin: mouse wheel works over the spin buttons")
{
    InputTextWithSpin* spin = make_spin(window, "2");

    hover(spin, 0.99f);
    mouse_wheel(1.f);

    REQUIRE(spin->text() == "3");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputTextWithSpin: mouse wheel is ignored while typing")
{
    InputTextWithSpin* spin = make_spin(window, "2");
    std::vector<std::string> edited_texts;
    spin->callbacks().text_edited = [&]() { edited_texts.push_back(spin->text()); };

    render();
    const Vec2f pos = spin->get_global_pos();
    simulate_click(pos.x() + spin->width() * 0.3f, pos.y() + spin->height() * 0.5f);
    key_down(ImGuiMod_Ctrl);
    key_tap(ImGuiKey_A);
    key_up(ImGuiMod_Ctrl);
    type_text("50");
    mouse_wheel(1.f);
    key_tap(ImGuiKey_Enter);

    REQUIRE(edited_texts == std::vector<std::string>{"50"});
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "InputTextWithSpin: mouse wheel is ignored when it does not change values"
)
{
    InputTextWithSpin* spin = make_spin(window, "2");

    Item::set_wheel_changes_value(false);
    hover(spin, 0.3f);
    mouse_wheel(1.f);
    Item::set_wheel_changes_value(true);

    REQUIRE(spin->text() == "2");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputTextWithSpin: mouse wheel is ignored on Mixed")
{
    InputTextWithSpin* spin = make_spin(window, "2");
    spin->set_override_label("Mixed");

    hover(spin, 0.3f);
    mouse_wheel(1.f);

    REQUIRE(spin->text() == "2");
}

TEST_CASE_METHOD(YogaComponentFixture, "InputTextWithSpin: mouse wheel at the bound does nothing")
{
    InputTextWithSpin* spin = make_spin(window, "2");
    static_cast<IntValidator*>(spin->validator())->set_to(2);
    int edited                    = 0;
    spin->callbacks().text_edited = [&]() { ++edited; };

    hover(spin, 0.3f);
    mouse_wheel(1.f);

    REQUIRE(spin->text() == "2");
    REQUIRE(edited == 0);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "InputTextWithSpin: mouse wheel does not scroll parent ScrollArea"
)
{
    ScrollArea* page        = make_page();
    InputTextWithSpin* spin = make_spin(page, "2");
    add_overflow(page);

    hover(spin, 0.3f);
    mouse_wheel(-1.f);

    REQUIRE(spin->text() == "1");
    REQUIRE(page->scroll_pos().y() == 0.f);
}
