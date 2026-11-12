#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Yoga/ButtonGroup.hpp>
#include <Slic3r/App/Yoga/AbstractButton.hpp>

#include "ImGuiFixture.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Yoga;

TEST_CASE_METHOD(ImGuiFixture, "ButtonGroup constructor")
{
    RootItem tree;

    AbstractButton* foo = tree.emplace_back<AbstractButton>();
    AbstractButton* bar = tree.emplace_back<AbstractButton>();
    AbstractButton* foobar = tree.emplace_back<AbstractButton>();
    foo->set_checkable(true);
    bar->set_checkable(true);
    foobar->set_checkable(true);

    ButtonGroup group({foo, bar, foobar});

    REQUIRE(group.button_count() == 3);
}

TEST_CASE_METHOD(ImGuiFixture, "ButtonGroup set_buttons")
{
    RootItem tree;

    AbstractButton* foo = tree.emplace_back<AbstractButton>();
    AbstractButton* bar = tree.emplace_back<AbstractButton>();
    AbstractButton* foobar = tree.emplace_back<AbstractButton>();
    foo->set_checkable(true);
    bar->set_checkable(true);
    foobar->set_checkable(true);

    ButtonGroup group;
    group.set_buttons({foo, bar, foobar});

    REQUIRE(group.button_count() == 3);

    group.set_buttons({});

    REQUIRE(group.button_count() == 0);
    REQUIRE(group.checked_button() == nullptr);
}

TEST_CASE_METHOD(ImGuiFixture, "ButtonGroup set_checked exclusive")
{
    RootItem tree;

    AbstractButton* foo = tree.emplace_back<AbstractButton>();
    foo->set_checkable(true);
    AbstractButton* bar = tree.emplace_back<AbstractButton>();
    bar->set_checkable(true);
    AbstractButton* foobar = tree.emplace_back<AbstractButton>();
    foobar->set_checkable(true);

    ButtonGroup group({foo, bar, foobar});

    AbstractButton* last = nullptr;
    AbstractButton* current = nullptr;

    group.callbacks().checked_changed = [&](AbstractButton* c, AbstractButton* l) {
        last = l;
        current = c;
    };

    foo->set_checked(true);
    bar->set_checked(true);

    REQUIRE(foo->checked() == false);
    REQUIRE(bar->checked() == true);
    REQUIRE(group.checked_button() == bar);
    REQUIRE(last == foo);
    REQUIRE(current == bar);
}
