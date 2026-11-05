#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Dialog.hpp>
#include <Slic3r/App/Yoga/LayoutButton.hpp>

#include "YogaComponentFixture.hpp"

#include <imgui_internal.h>

#include <map>

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinAbs;

namespace {

struct MemoryGeometryStore : public IDialogGeometryStore
{
    std::optional<DialogGeometry> dialog_geometry(const std::string& name) const override
    {
        const auto it = entries.find(name);
        return it == entries.end() ? std::nullopt : std::optional{it->second};
    }
    void set_dialog_geometry(const std::string& name, const DialogGeometry& geometry) override
    {
        entries[name] = geometry;
    }
    void remove_dialog_geometry(const std::string& name) override { entries.erase(name); }

    std::map<std::string, DialogGeometry> entries;
};

struct TestDialog : public Dialog
{
    explicit TestDialog(const std::string& name) : Dialog(name) {}

    using Dialog::append_tab;
    using Dialog::close_button;
    using Dialog::maximize_button;
    using Dialog::pin_button;
    using Dialog::set_implicit_size;
};

struct DialogFixture : public YogaComponentFixture
{
    DialogFixture()
    {
        root.set_dialog_geometry_store(&store);

        anchor = window->emplace_back<Item>();
        anchor->set_position_type(YGPositionTypeAbsolute);
        anchor->set_left(100.f);
        anchor->set_top(100.f);
        anchor->set_width(100.f);
        anchor->set_height(50.f);

        dialog = window->emplace_back<TestDialog>("TestDialog");
        dialog->set_implicit_size(200.f, std::nullopt);
        dialog->content_item()->set_min_width(100.f);
        dialog->content_item()->set_min_height(100.f);
        dialog->attach_to_item(anchor, Position::Right);
    }

    ~DialogFixture() { root.set_dialog_geometry_store(nullptr); }

    Vec2f pos() const
    {
        return {dialog->content_item()->left(), dialog->content_item()->top()};
    }
    Vec2f size() const
    {
        return {dialog->content_item()->width(), dialog->content_item()->height()};
    }
    Vec2f header_point() const { return pos() + Vec2f{size().x() * 0.5f, 20.f}; }
    Vec2f grip_point() const { return pos() + size() - Vec2f{2.f, 2.f}; }

    void open_and_settle()
    {
        dialog->open();
        render();
        render();
    }

    void drag(const Vec2f& from, const Vec2f& delta)
    {
        mouse_move(from.x(), from.y());
        render();
        mouse_down();
        render();
        mouse_move(from.x() + delta.x(), from.y() + delta.y());
        render();
        render();
        mouse_up();
        render();
    }

    static void require_close(const Vec2f& actual, const Vec2f& expected)
    {
        REQUIRE_THAT(actual.x(), WithinAbs(expected.x(), 1.f));
        REQUIRE_THAT(actual.y(), WithinAbs(expected.y(), 1.f));
    }

    void click(const Item* button)
    {
        Vec2f local = Vec2f{button->width(), button->height()} * 0.5f;
        for (const Item* item = button; item != dialog->content_item(); item = item->parent_item()) {
            local += Vec2f{item->left(), item->top()};
        }
        const Vec2f point = pos() + local;
        simulate_click(point.x(), point.y());
        render();
    }

    MemoryGeometryStore store;
    Item* anchor       = nullptr;
    TestDialog* dialog = nullptr;
};

} // namespace

TEST_CASE_METHOD(DialogFixture, "Dialog: tab flex grow fills the top row with a centered label")
{
    TestDialog* tabbed = window->emplace_back<TestDialog>("TabbedDialog");
    tabbed->content_item()->set_width(300.f);
    tabbed->attach_to_center();
    LayoutButton* tab = tabbed->append_tab(std::string{});
    Item* label       = tab->emplace_back<Item>();
    label->set_width(30.f);
    label->set_height(10.f);
    tabbed->open();
    render();
    render();
    const float initial_tab_width = tab->width();
    const Item* buttons_area      = tabbed->close_button()->parent_item();
    const float initial_buttons_width = buttons_area->width();
    REQUIRE(initial_tab_width < initial_buttons_width);

    tabbed->set_tab_flex_grow(1);
    render();
    render();
    REQUIRE(tab->width() > initial_buttons_width);
    REQUIRE(buttons_area->width() < initial_tab_width);
    REQUIRE_THAT(
        tab->width() + buttons_area->width(),
        WithinAbs(tabbed->content_item()->width(), 1.f)
    );
    REQUIRE_THAT(label->left() + label->width() * 0.5f, WithinAbs(tab->width() * 0.5f, 1.f));
}

TEST_CASE_METHOD(DialogFixture, "Dialog: header drag detaches until closed")
{
    dialog->set_movable(true);
    open_and_settle();
    const Vec2f initial = pos();

    drag(header_point(), {50.f, 30.f});
    require_close(pos(), initial + Vec2f{50.f, 30.f});
    REQUIRE(dialog->detached());

    anchor->set_left(300.f);
    render();
    render();
    require_close(pos(), initial + Vec2f{50.f, 30.f});

    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->detached());
    require_close(pos(), initial + Vec2f{200.f, 0.f});
}

TEST_CASE_METHOD(DialogFixture, "Dialog: corner resize respects min and max size")
{
    dialog->set_resizable(true);
    dialog->content_item()->set_max_width(300.f);
    dialog->content_item()->set_max_height(300.f);
    open_and_settle();

    drag(grip_point(), {500.f, 500.f});
    require_close(size(), {300.f, 300.f});
    REQUIRE(dialog->detached());

    drag(grip_point(), {-500.f, -500.f});
    require_close(size(), {100.f, 100.f});
}

TEST_CASE_METHOD(DialogFixture, "Dialog: grip double click does not auto-fit the window")
{
    dialog->set_resizable(true);
    dialog->content_item()->set_max_width(400.f);
    dialog->content_item()->set_max_height(400.f);
    open_and_settle();
    const Vec2f initial_size = size();

    drag(grip_point(), {100.f, 100.f});
    const Vec2f enlarged = size();
    require_close(enlarged, initial_size + Vec2f{100.f, 100.f});

    const ImGuiWindow* imgui_window =
        ImGui::FindWindowByName(dialog->content_item()->object_name().c_str());
    REQUIRE(imgui_window != nullptr);

    const Vec2f grip = grip_point();
    mouse_move(grip.x(), grip.y());
    render();
    for (int i = 0; i < 2; ++i) {
        mouse_down();
        render();
        require_close({imgui_window->Size.x, imgui_window->Size.y}, enlarged);
        mouse_up();
        render();
    }
    render();
    require_close(size(), enlarged);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: resize stops at the viewport edge without moving")
{
    dialog->attach_to_center();
    dialog->set_resizable(true);
    dialog->content_item()->set_max_width(5000.f);
    dialog->content_item()->set_max_height(5000.f);
    open_and_settle();
    const Vec2f initial_pos = pos();

    drag(grip_point(), {2000.f, 2000.f});
    require_close(pos(), initial_pos);
    require_close(size(), {1280.f - initial_pos.x(), 720.f - initial_pos.y()});

    const Vec2f bottom_left = pos() + Vec2f{2.f, size().y() - 2.f};
    drag(bottom_left, {-2000.f, 0.f});
    REQUIRE_THAT(pos().x(), WithinAbs(0.f, 1.f));
    require_close(pos() + size(), {1280.f, 720.f});
}

TEST_CASE_METHOD(DialogFixture, "Dialog: header double click toggles maximize")
{
    dialog->set_resizable(true);
    open_and_settle();
    const Vec2f initial_pos  = pos();
    const Vec2f initial_size = size();

    const auto double_click = [this](const Vec2f& point) {
        mouse_move(point.x(), point.y());
        render();
        for (int i = 0; i < 2; ++i) {
            mouse_down();
            render();
            mouse_up();
            render();
        }
        render();
    };

    double_click(header_point());
    REQUIRE(dialog->maximized());
    require_close(size(), {1280.f, 720.f});

    double_click(header_point());
    REQUIRE_FALSE(dialog->maximized());
    require_close(pos(), initial_pos);
    require_close(size(), initial_size);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: maximize fills the viewport and restores")
{
    dialog->set_resizable(true);
    open_and_settle();
    const Vec2f initial_pos  = pos();
    const Vec2f initial_size = size();

    click(dialog->maximize_button());
    REQUIRE(dialog->maximized());
    require_close(pos(), {0.f, 0.f});
    require_close(size(), {1280.f, 720.f});

    click(dialog->maximize_button());
    REQUIRE_FALSE(dialog->maximized());
    require_close(pos(), initial_pos);
    require_close(size(), initial_size);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: pinned geometry is stored, restored and removed")
{
    dialog->set_movable(true);
    dialog->set_pinnable(true);
    open_and_settle();
    const Vec2f initial = pos();

    drag(header_point(), {40.f, 20.f});
    click(dialog->pin_button());
    REQUIRE(store.entries.contains("TestDialog"));
    require_close({store.entries["TestDialog"].left, store.entries["TestDialog"].top}, pos());

    drag(header_point(), {10.f, 10.f});
    const Vec2f moved = pos();
    require_close({store.entries["TestDialog"].left, store.entries["TestDialog"].top}, moved);

    dialog->close();
    render();
    open_and_settle();
    REQUIRE(dialog->pinned());
    REQUIRE(dialog->detached());
    require_close(pos(), moved);

    click(dialog->pin_button());
    REQUIRE_FALSE(store.entries.contains("TestDialog"));
    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->pinned());
    REQUIRE_FALSE(dialog->detached());
    require_close(pos(), initial);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: pin button click pins and unpins")
{
    dialog->set_movable(true);
    dialog->set_pinnable(true);
    open_and_settle();
    LayoutButton* pin_button = dialog->pin_button();

    click(pin_button);
    REQUIRE(dialog->pinned());
    REQUIRE(store.entries.contains("TestDialog"));

    click(pin_button);
    REQUIRE_FALSE(dialog->pinned());
    REQUIRE_FALSE(store.entries.contains("TestDialog"));

    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->pinned());
}

TEST_CASE_METHOD(DialogFixture, "Dialog: unpinned resized dialog reopens at its implicit size")
{
    dialog->set_resizable(true);
    open_and_settle();
    const Vec2f initial_size = size();

    drag(grip_point(), {50.f, 50.f});
    require_close(size(), initial_size + Vec2f{50.f, 50.f});

    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->detached());
    require_close(size(), initial_size);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: min size larger than the viewport is capped by the viewport")
{
    dialog->set_resizable(true);
    dialog->content_item()->set_min_height(5000.f);
    open_and_settle();

    REQUIRE(dialog->content_item()->min_heigth() == Unit{5000.f});
    require_close(size(), {200.f, default_size_info.viewport_size_y});
}

TEST_CASE_METHOD(DialogFixture, "Dialog: dialog closed while maximized reopens restored")
{
    dialog->set_resizable(true);
    open_and_settle();
    const Vec2f initial_size = size();

    click(dialog->maximize_button());
    REQUIRE(dialog->maximized());
    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->maximized());
    require_close(size(), initial_size);
}

TEST_CASE_METHOD(DialogFixture, "Dialog: centered dialog can be moved and resized")
{
    dialog->attach_to_center();
    dialog->set_resizable(true);
    dialog->content_item()->set_max_width(400.f);
    dialog->content_item()->set_max_height(400.f);
    open_and_settle();
    const Vec2f centered      = pos();
    const Vec2f centered_size = size();

    REQUIRE((dialog->content_item()->flags() & ImGuiWindowFlags_NoResize) == 0);
    drag(header_point(), {50.f, 30.f});
    require_close(pos(), centered + Vec2f{50.f, 30.f});
    REQUIRE(dialog->detached());

    drag(grip_point(), {60.f, 40.f});
    require_close(size(), centered_size + Vec2f{60.f, 40.f});

    dialog->close();
    render();
    open_and_settle();
    REQUIRE_FALSE(dialog->detached());
    require_close(size(), centered_size);
    require_close(pos(), centered);
}
