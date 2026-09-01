#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/ComboBox.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

// Helper: add a ComboBox to the fixture's window at a known position (200,200), size 200x30.
static ComboBox* make_combo(YogaComponentFixture& f, std::initializer_list<std::string> items = {})
{
    ComboBox* combo = f.window->emplace_back<ComboBox>(items);
    combo->set_width(200.f);
    combo->set_height(30.f);
    combo->set_margin(Margins(200.f, 200.f, 0.f, 0.f));
    return combo;
}

// Helper: simulate opening the combo dropdown by clicking its body.
static void open_combo(YogaComponentFixture& f, ComboBox* combo)
{
    const Vec2f pos = combo->get_global_pos();
    f.simulate_click(pos.x() + combo->width() * 0.5f, pos.y() + combo->height() * 0.5f);
}

// Helper: click item at index inside an already-open combo popup.
// The popup opens immediately below the combo widget; items are stacked top-to-bottom.
static void click_popup_item(YogaComponentFixture& f, ComboBox* combo, int item_index)
{
    const Vec2f pos       = combo->get_global_pos();
    const float popup_top = pos.y() + combo->height();
    // Window.cpp pushes WindowPadding={0,0}, so popup content starts at popup_top.
    // Each selectable item has height combo->height() (passed as im_size.y in ComboBox::render).
    const float item_h  = combo->height();
    const float item_cy = popup_top + item_h * static_cast<float>(item_index) + item_h * 0.5f;
    f.simulate_click(pos.x() + combo->width() * 0.5f, item_cy);
}

// ── Data / API tests ──────────────────────────────────────────────────────────

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: initializer_list constructor sets items")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    REQUIRE(combo->items().size() == 3);
    REQUIRE(combo->items()[0] == "alpha");
    REQUIRE(combo->items()[2] == "gamma");
    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_items replaces content")
{
    ComboBox* combo = make_combo(*this);
    combo->set_items({"a", "b", "c"});
    REQUIRE(combo->items().size() == 3);
    REQUIRE(combo->items()[1] == "b");
    REQUIRE(combo->current_label() == "a");
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_current_index updates label")
{
    ComboBox* combo = make_combo(*this, {"foo", "bar", "baz"});
    combo->set_current_index(2);
    REQUIRE(combo->current_index() == 2);
    REQUIRE(combo->current_label() == "baz");
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_current_index clamped to last valid")
{
    ComboBox* combo = make_combo(*this, {"a", "b", "c"});
    combo->set_current_index(99);
    REQUIRE(combo->current_index() == 2);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: set_current_index on empty list returns minus one"
)
{
    ComboBox* combo = make_combo(*this, {});
    combo->set_current_index(0);
    REQUIRE(combo->current_index() == -1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_override_label")
{
    ComboBox* combo = make_combo(*this, {"a", "b"});
    combo->set_override_label("OVERRIDE");
    REQUIRE(combo->override_label() == "OVERRIDE");
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: editable mode")
{
    ComboBox* combo = make_combo(*this, {"a", "b"});
    REQUIRE(combo->editable() == false);
    combo->set_editable(true);
    REQUIRE(combo->editable() == true);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: selection_changed fires when user selects item")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});

    int changed_index                    = -1;
    combo->callbacks().selection_changed = [&](int idx) { changed_index = idx; };

    render();
    open_combo(*this, combo);
    click_popup_item(*this, combo, 1);

    REQUIRE(changed_index == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: current_index updates after user selects item")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});

    render();
    open_combo(*this, combo);
    click_popup_item(*this, combo, 2);

    REQUIRE(combo->current_index() == 2);
    REQUIRE(combo->current_label() == "gamma");
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: selection_changed fires with correct index")
{
    ComboBox* combo = make_combo(*this, {"x", "y", "z"});

    std::vector<int> fired_indices;
    combo->callbacks().selection_changed = [&](int idx) { fired_indices.push_back(idx); };

    render();
    open_combo(*this, combo);
    click_popup_item(*this, combo, 0);

    render();
    open_combo(*this, combo);
    click_popup_item(*this, combo, 2);

    REQUIRE(fired_indices.size() == 2);
    REQUIRE(fired_indices[0] == 0);
    REQUIRE(fired_indices[1] == 2);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: selection_changed does not fire when disabled")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_enabled(false);

    int changed_index                    = -1;
    combo->callbacks().selection_changed = [&](int idx) { changed_index = idx; };

    render();
    open_combo(*this, combo);

    REQUIRE(changed_index == -1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel changes selection while hovered")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_current_index(1);

    std::vector<int> changed_indices;
    combo->callbacks().selection_changed = [&](int index) { changed_indices.push_back(index); };

    render();
    const Vec2f pos = combo->get_global_pos();
    mouse_move(pos.x() + combo->width() * 0.5f, pos.y() + combo->height() * 0.5f);
    render();
    ImGui::GetIO().AddMouseWheelEvent(0.f, 1.f);
    render();

    REQUIRE(combo->current_index() == 0);
    REQUIRE(changed_indices == std::vector<int>{0});

    ImGui::GetIO().AddMouseWheelEvent(0.f, -1.f);
    render();

    REQUIRE(combo->current_index() == 1);
    REQUIRE(changed_indices == std::vector<int>{0, 1});
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel changes editable selection")
{
    ComboBox* combo = make_combo(*this, {"0%", "5%", "10%"});
    combo->set_editable(true);
    combo->set_current_index(1);

    int changed_index                    = -1;
    combo->callbacks().selection_changed = [&](int index) { changed_index = index; };

    render();
    const Vec2f pos = combo->get_global_pos();
    mouse_move(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    render();
    ImGui::GetIO().AddMouseWheelEvent(0.f, 1.f);
    render();

    REQUIRE(combo->current_index() == 0);
    REQUIRE(changed_index == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel respects bounds")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta"});

    int changed_count                    = 0;
    combo->callbacks().selection_changed = [&](int) { ++changed_count; };

    render();
    const Vec2f pos = combo->get_global_pos();
    mouse_move(pos.x() + combo->width() * 0.5f, pos.y() + combo->height() * 0.5f);
    render();
    ImGui::GetIO().AddMouseWheelEvent(0.f, 1.f);
    render();

    REQUIRE(combo->current_index() == 0);
    REQUIRE(changed_count == 0);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: mouse wheel does not scroll parent ScrollArea"
)
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(300.f);
    scroll_area->set_height(100.f);
    scroll_area->set_orientation(Orientation::Vertical);

    ComboBox* combo = scroll_area->emplace_back<ComboBox>(
        std::initializer_list<std::string>{"alpha", "beta"}
    );
    combo->set_editable(true);
    combo->set_width(280.f);
    combo->set_height(30.f);
    combo->set_flex_shrink(0.f);

    Item* overflow = scroll_area->emplace_back<Item>();
    overflow->set_height(300.f);
    overflow->set_flex_shrink(0.f);

    render();
    render();
    REQUIRE(scroll_area->scroll_size().y() > 0.f);

    const Vec2f pos = combo->get_global_pos();
    mouse_move(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    render();
    ImGui::GetIO().AddMouseWheelEvent(0.f, -1.f);
    render();

    REQUIRE(combo->current_index() == 1);
    REQUIRE(scroll_area->scroll_pos().y() == 0.f);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: text_edited fires after user types and presses Enter"
)
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta"});
    combo->set_editable(true);

    bool text_edited               = false;
    combo->callbacks().text_edited = [&] { text_edited = true; };

    render();
    const Vec2f pos = combo->get_global_pos();
    // Click the left portion of the editable combo to focus the text field (not the arrow)
    simulate_click(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    key_down(ImGuiKey_LeftCtrl); // hold Ctrl
    key_tap(ImGuiKey_A); // Ctrl+A: select all existing text
    key_up(ImGuiKey_LeftCtrl);
    type_text("custom");
    key_tap(ImGuiKey_Enter);

    REQUIRE(text_edited);
    REQUIRE(combo->current_label() == "custom");
}
