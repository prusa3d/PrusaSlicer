///
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;

TEST_CASE("Item 1 child Item::add")
{
    RootItem tree;

    std::unique_ptr<Item> child = std::make_unique<Item>();
    tree.append(std::move(child));

    REQUIRE(tree.object_count() == 1);
}

TEST_CASE("Item 1 child constructor parent")
{
    RootItem tree;

    Item* child = tree.emplace_back<Item>();

    REQUIRE(child->parent() == &tree);
    REQUIRE(tree.object_count() == 1);
}

TEST_CASE("Item 1 child Item::remove")
{
    RootItem tree;

    Item* child_ptr{tree.emplace_back<Item>()};

    // Result needs to be captured otherwise the child is deallocated.
    const ItemPtr child{unique_dynamic_cast<Item>(tree.remove(child_ptr))};

    REQUIRE(child_ptr == child.get());
    REQUIRE(child_ptr->parent() == nullptr);
    REQUIRE(tree.object_count() == 0);
}

TEST_CASE("Item parent_item after append")
{
    RootItem tree;
    Item* child = tree.emplace_back<Item>();

    REQUIRE(child->parent_item() == &tree);
}

TEST_CASE("Item prepend ordering")
{
    RootItem tree;
    Item* first = tree.emplace_back<Item>();

    auto second_owned = std::make_unique<Item>();
    Item* second       = second_owned.get();
    tree.prepend(std::move(second_owned));

    REQUIRE(tree.items().size() == 2);
    REQUIRE(tree.items()[0] == second);
    REQUIRE(tree.items()[1] == first);
}

TEST_CASE("Item enabled parent propagation")
{
    RootItem parent;
    Item* child = parent.emplace_back<Item>();

    REQUIRE(child->enabled());

    parent.set_enabled(false);
    REQUIRE(!child->enabled());

    parent.set_enabled(true);
    REQUIRE(child->enabled());

    // Child's own disabled state is independent of parent
    child->set_enabled(false);
    REQUIRE(!child->enabled());
    parent.set_enabled(false);
    REQUIRE(!child->enabled());
    parent.set_enabled(true);
    REQUIRE(!child->enabled()); // still disabled by its own flag
}
