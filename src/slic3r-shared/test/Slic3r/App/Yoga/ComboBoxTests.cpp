#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/ComboBox.hpp>
#include <Slic3r/App/Yoga/LayoutButton.hpp>
#include <Slic3r/App/Yoga/Validator.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

// Helper: add a 200x30 ComboBox to the fixture's window at a known position (200,200), or to a page.
static ComboBox* make_combo(
    YogaComponentFixture& f,
    std::initializer_list<std::string> items = {},
    ScrollArea* page                         = nullptr
)
{
    ComboBox* combo = (page ? static_cast<Item*>(page) : f.window)->emplace_back<ComboBox>(items);
    combo->set_width(200.f);
    combo->set_height(30.f);
    combo->set_flex_shrink(0.f);
    if (!page) {
        combo->set_margin(Margins(200.f, 200.f, 0.f, 0.f));
    }
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
    click_popup_item(*this, combo, 1);

    REQUIRE(changed_index == -1);
    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel changes selection while hovered")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_current_index(1);
    std::vector<int> changed_indices;
    combo->callbacks().selection_changed = [&](int index) { changed_indices.push_back(index); };

    hover(combo);
    mouse_wheel(1.f);
    mouse_wheel(-1.f);

    REQUIRE(changed_indices == std::vector<int>{2, 1});
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel at the bound does nothing")
{
    ComboBox* combo                      = make_combo(*this, {"alpha", "beta"});
    int changed_count                    = 0;
    combo->callbacks().selection_changed = [&](int) { ++changed_count; };

    hover(combo);
    mouse_wheel(-1.f);
    REQUIRE(combo->current_index() == 0);
    REQUIRE(changed_count == 0);

    mouse_wheel(1.f);
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == 1);
    REQUIRE(changed_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel on Mixed selects first or last item")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_current_index(1);
    combo->set_override_label("Mixed");
    std::vector<int> changed_indices;
    combo->callbacks().selection_changed = [&](int index) { changed_indices.push_back(index); };

    hover(combo);
    mouse_wheel(-1.f);
    mouse_wheel(1.f);

    REQUIRE(changed_indices == std::vector<int>{2, 0});
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel steps from a label set from outside")
{
    ComboBox* combo = make_combo(*this, {"0%", "5%", "10%"});
    combo->set_editable(true);
    auto validator = std::make_unique<DoubleValidator>();
    validator->set_units({"%"});
    combo->set_validator(std::move(validator));
    combo->set_item_numbers({0., 5., 10.});
    hover(combo, 0.25f);

    combo->set_current_label("5", 5.);
    REQUIRE(combo->current_index() == 1);
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == 2);

    combo->set_current_label("7", 7.);
    REQUIRE(combo->current_index() == -1);
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == 2);

    combo->set_current_label("7", 7.);
    mouse_wheel(-1.f);
    REQUIRE(combo->current_index() == 1);

    combo->set_current_label("12", 12.);
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == -1);

    combo->set_current_label("7 %");
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == -1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel up selects the larger item value")
{
    ComboBox* combo = make_combo(*this, {"Solid", "Subtle", "Balanced"});
    combo->set_editable(true);
    combo->set_current_index(1);
    combo->set_item_numbers({1., 0.85, 0.7});
    hover(combo, 0.25f);

    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == 0);

    mouse_wheel(-1.f);
    mouse_wheel(-1.f);
    REQUIRE(combo->current_index() == 2);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: an item with a number shows its own text")
{
    ComboBox* combo = make_combo(*this, {"Solid", "Subtle"});
    combo->set_editable(true);
    combo->set_validator(std::make_unique<DoubleValidator>(0.5, 1.));
    combo->set_item_numbers({1., 0.85});

    combo->set_current_index(1);
    REQUIRE(combo->current_label() == "Subtle");

    combo->set_current_label("0.6", 0.6);
    REQUIRE(combo->current_label() == "0.6");
    REQUIRE(combo->current_index() == -1);

    combo->set_current_label("1", 1.);
    REQUIRE(combo->current_label() == "Solid");
    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_items drops the item numbers")
{
    ComboBox* combo = make_combo(*this, {"Solid", "Subtle"});
    combo->set_item_numbers({1., 0.85});
    combo->set_items({"alpha", "beta", "gamma"});
    hover(combo);

    mouse_wheel(1.f);

    REQUIRE(combo->current_index() == 1);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: set_current_label selects the item of the same text"
)
{
    ComboBox* combo = make_combo(*this, {"PLA", "PET", "ABS"});
    combo->set_editable(true);
    hover(combo, 0.25f);

    combo->set_current_label("PET");
    REQUIRE(combo->current_index() == 1);
    mouse_wheel(1.f);
    REQUIRE(combo->current_index() == 2);

    combo->set_current_label("Custom");
    REQUIRE(combo->current_index() == -1);
    mouse_wheel(-1.f);
    REQUIRE(combo->current_index() == -1);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: set_current_label updates the revert button")
{
    ComboBox* combo = make_combo(*this, {"0%", "5%"});
    combo->set_editable(true);
    combo->set_item_numbers({0., 5.});
    auto* revert = window->emplace_back<LayoutButton>("revert");
    combo->set_revert_button(revert);

    combo->set_current_label("7", 7.);
    REQUIRE(revert->is_self_visible());

    combo->set_current_label("0", 0.);
    REQUIRE(!revert->is_self_visible());
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel is ignored while typing")
{
    ComboBox* combo = make_combo(*this, {"0%", "5%", "10%"});
    combo->set_editable(true);
    combo->set_current_index(2);

    render();
    const Vec2f pos = combo->get_global_pos();
    simulate_click(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    type_text("7");
    mouse_wheel(-1.f);

    REQUIRE(combo->current_index() == 2);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel is ignored while the popup is open")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    render();
    open_combo(*this, combo);

    mouse_wheel(1.f);
    mouse_wheel(1.f);

    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: disabled ignores mouse wheel")
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta"});
    combo->set_enabled(false);

    hover(combo);
    mouse_wheel(1.f);

    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: mouse wheel is ignored when the wheel is disabled"
)
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta"});
    combo->set_wheel_enabled(false);

    hover(combo);
    mouse_wheel(1.f);

    REQUIRE(combo->current_index() == 0);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: mouse wheel scrolls the page when it does not change values"
)
{
    ScrollArea* page = make_page();
    ComboBox* combo  = make_combo(*this, {"alpha", "beta"}, page);
    combo->set_current_index(1);
    add_overflow(page);

    Item::set_wheel_changes_value(false);
    hover(combo);
    mouse_wheel(-1.f);
    Item::set_wheel_changes_value(true);

    REQUIRE(combo->current_index() == 1);
    REQUIRE(page->scroll_pos().y() > 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel does not scroll parent ScrollArea")
{
    ScrollArea* page = make_page();
    ComboBox* combo  = make_combo(*this, {"alpha", "beta"}, page);
    combo->set_editable(true);
    add_overflow(page);

    for (const float fx : {0.25f, 0.97f}) {
        combo->set_current_index(1);
        hover(combo, fx);
        mouse_wheel(-1.f);

        REQUIRE(combo->current_index() == 0);
        REQUIRE(page->scroll_pos().y() == 0.f);
    }
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: mouse wheel does not scroll remapped ScrollArea")
{
    ScrollArea* page = make_page(Orientation::Horizontal);
    page->set_remap_horizontal_scroll(true);
    ComboBox* combo = make_combo(*this, {"alpha", "beta"}, page);
    combo->set_current_index(1);
    add_overflow(page);

    hover(combo);
    mouse_wheel(-1.f);

    REQUIRE(combo->current_index() == 0);
    REQUIRE(page->scroll_pos().x() == 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "ComboBox: page scroll continues over the combo")
{
    ScrollArea* page = make_page();
    Item* spacer     = page->emplace_back<Item>();
    spacer->set_height(60.f);
    spacer->set_flex_shrink(0.f);
    ComboBox* combo = make_combo(*this, {"alpha", "beta"}, page);
    combo->set_current_index(1);
    add_overflow(page);

    hover(spacer, 0.3f, 0.15f);
    mouse_wheel(-1.f);
    const float first_scroll = page->scroll_pos().y();
    const float mouse_y      = ImGui::GetIO().MousePos.y;
    const float combo_y      = combo->get_global_pos().y();
    REQUIRE(combo_y <= mouse_y);
    REQUIRE(mouse_y < combo_y + combo->height());
    mouse_wheel(-1.f);

    REQUIRE(first_scroll > 0.f);
    REQUIRE(page->scroll_pos().y() > first_scroll);
    REQUIRE(combo->current_index() == 1);
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

// ── Override label (mixed) tests ──────────────────────────────────────────────

// Helper: open the popup of an editable combo by clicking its arrow button (the body is a text field).
static void open_editable_combo(YogaComponentFixture& f, ComboBox* combo)
{
    const Vec2f pos = combo->get_global_pos();
    f.simulate_click(
        pos.x() + combo->width() - combo->height() * 0.5f,
        pos.y() + combo->height() * 0.5f
    );
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: override label - selecting current item fires selection_changed"
)
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_current_index(1);
    combo->set_override_label("Mixed");

    int changed_index                    = -1;
    combo->callbacks().selection_changed = [&](int idx) { changed_index = idx; };

    render();
    open_combo(*this, combo);
    click_popup_item(*this, combo, 1);

    REQUIRE(changed_index == 1);
    REQUIRE(combo->current_index() == 1);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: override label - editable keeps current label"
)
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta"});
    combo->set_editable(true);
    combo->set_current_index(1);
    combo->set_override_label("Mixed");

    render();
    render();

    REQUIRE(combo->current_label() == "beta");
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: override label - editable selection_changed fires when user selects item"
)
{
    ComboBox* combo = make_combo(*this, {"alpha", "beta", "gamma"});
    combo->set_editable(true);
    combo->set_override_label("Mixed");

    int changed_index                    = -1;
    combo->callbacks().selection_changed = [&](int idx) { changed_index = idx; };

    render();
    open_editable_combo(*this, combo);
    click_popup_item(*this, combo, 2);

    REQUIRE(changed_index == 2);
    REQUIRE(combo->current_label() == "gamma");
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "ComboBox: override label - editable text_edited fires with validated value"
)
{
    ComboBox* combo = make_combo(*this, {"10", "20"});
    combo->set_validator(std::make_unique<IntValidator>());
    combo->set_editable(true);
    combo->set_override_label("Mixed");

    bool text_edited               = false;
    combo->callbacks().text_edited = [&] { text_edited = true; };

    render();
    const Vec2f pos = combo->get_global_pos();
    simulate_click(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    key_down(ImGuiKey_LeftCtrl);
    key_tap(ImGuiKey_A);
    key_up(ImGuiKey_LeftCtrl);
    type_text("42");
    key_tap(ImGuiKey_Enter);

    REQUIRE(text_edited);
    auto* validator = dynamic_cast<IntValidator*>(combo->validator());
    REQUIRE(validator != nullptr);
    REQUIRE(validator->value() == 42);
}
