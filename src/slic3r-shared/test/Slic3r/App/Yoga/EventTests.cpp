
#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>

#include "TestRootItem.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Yoga;

TEST_CASE("remove_later defers removal until process_loop_events")
{
    TestRootItem root;
    Item* child = root.emplace_back<Item>();

    root.remove_later(child);

    REQUIRE(root.object_count() == 1); // still in tree immediately after

    root.process_loop_events();

    REQUIRE(root.object_count() == 0);
}

TEST_CASE("remove_later skipped when item already removed")
{
    TestRootItem root;
    Item* child               = root.emplace_back<Item>();
    ObjectHeartBeat heartbeat = child->heartbeat();

    root.remove_later(child);

    // Immediately remove the item — kills its heartbeat
    root.remove(child);
    REQUIRE(heartbeat.expired());

    // Event should silently skip, not crash
    REQUIRE_NOTHROW(root.process_loop_events());
    REQUIRE(root.object_count() == 0);
}

TEST_CASE("remove_later skipped when parent already removed")
{
    TestRootItem root;
    Item* parent                     = root.emplace_back<Item>();
    Item* child                      = parent->emplace_back<Item>();
    ObjectHeartBeat parent_heartbeat = parent->heartbeat();

    parent->remove_later(child);

    // Remove the parent — kills parent's heartbeat, invalidating the event
    root.remove(parent);
    REQUIRE(parent_heartbeat.expired());

    REQUIRE_NOTHROW(root.process_loop_events());
}

TEST_CASE("push_event routes from nested item to root queue")
{
    TestRootItem root;
    Item* parent = root.emplace_back<Item>();
    Item* child  = parent->emplace_back<Item>();

    // push_event on a non-root Item must route up to root's loop events
    parent->remove_later(child);

    REQUIRE(parent->object_count() == 1);

    root.process_loop_events();

    REQUIRE(parent->object_count() == 0);
}

TEST_CASE("move_later defers move to different parent")
{
    TestRootItem root;
    Item* src   = root.emplace_back<Item>();
    Item* dst   = root.emplace_back<Item>();
    Item* child = src->emplace_back<Item>();

    child->move_later(dst, 0);

    REQUIRE(src->object_count() == 1);
    REQUIRE(dst->object_count() == 0);

    root.process_loop_events();

    REQUIRE(src->object_count() == 0);
    REQUIRE(dst->object_count() == 1);
    REQUIRE(child->parent() == dst);
}

TEST_CASE("move_later skipped when item already removed")
{
    TestRootItem root;
    Item* src   = root.emplace_back<Item>();
    Item* dst   = root.emplace_back<Item>();
    Item* child = src->emplace_back<Item>();

    child->move_later(dst, 0);

    // Immediately remove the item — kills its heartbeat
    src->remove(child);

    REQUIRE_NOTHROW(root.process_loop_events());
    REQUIRE(dst->object_count() == 0);
}

TEST_CASE("multiple remove_later events all execute")
{
    TestRootItem root;
    root.emplace_back<Item>();
    root.emplace_back<Item>();
    root.emplace_back<Item>();

    for (Item* child : root.items()) {
        root.remove_later(child);
    }

    root.process_loop_events();

    REQUIRE(root.object_count() == 0);
}

TEST_CASE("move_later target index adjusted when earlier sibling removed")
// NOTE: this test was the reproducer for the inverted < vs > bug in
// MoveEvent::affected() that caused the wrong insertion position.
{
    TestRootItem root;
    // root = [A(0), B(1), C(2), D(3)]
    Item* a = root.emplace_back<Item>();
    Item* b = root.emplace_back<Item>();
    Item* c = root.emplace_back<Item>();
    Item* d = root.emplace_back<Item>();

    // Queue: remove B first, then move D to index 2 (the slot originally before C)
    root.remove_later(b);
    d->move_later(&root, 2);

    root.process_loop_events();

    // After removing B: effective list is [A, C, D].
    // D's target index should adjust from 2 to 1 (B's removal shifts everything after index 1 down).
    // Result: [A, D, C]
    REQUIRE(root.object_count() == 3);
    REQUIRE(root.get_object(0) == a);
    REQUIRE(root.get_object(1) == d);
    REQUIRE(root.get_object(2) == c);
}

TEST_CASE("remove_later of parent invalidates pending child move")
{
    TestRootItem root;
    Item* parent             = root.emplace_back<Item>();
    Item* dst                = root.emplace_back<Item>();
    Item* child              = parent->emplace_back<Item>();
    ObjectHeartBeat child_hb = child->heartbeat();

    // remove_later(parent) is queued FIRST — it will fire before the move event
    root.remove_later(parent);
    child->move_later(dst, 0);

    root.process_loop_events();

    // Parent (and child) deleted by the first event; move event's heartbeat
    // check sees an expired child and skips silently
    REQUIRE(child_hb.expired());
    REQUIRE(root.object_count() == 1); // only dst remains
    REQUIRE(dst->object_count() == 0); // child never arrived
}

TEST_CASE("queued remove after move targets item's new parent")
{
    TestRootItem root;
    Item* src   = root.emplace_back<Item>();
    Item* dst   = root.emplace_back<Item>();
    Item* child = src->emplace_back<Item>();

    // Move child to dst first, then queue a remove that still holds src's heartbeat
    child->move_later(dst, 0);
    src->remove_later(child);

    root.process_loop_events();

    // Move fires: child lands in dst.  src is still alive so the remove
    // event passes is_valid(); it finds child's current parent (dst) and
    // removes it from there — not from src.
    REQUIRE(src->object_count() == 0);
    REQUIRE(dst->object_count() == 0); // child removed from its new parent
}

TEST_CASE("move_later index adjusted by cascading sibling removals")
{
    TestRootItem root;
    // root = [A(0), B(1), C(2), D(3), E(4)]
    Item* a = root.emplace_back<Item>();
    Item* b = root.emplace_back<Item>();
    Item* c = root.emplace_back<Item>();
    Item* d = root.emplace_back<Item>();
    Item* e = root.emplace_back<Item>();

    // Remove A, B, C in order; each one decrements the move's target index
    // 3 → 2 → 1 → 0.  E should end up before D.
    root.remove_later(a);
    root.remove_later(b);
    root.remove_later(c);
    e->move_later(&root, 3);

    root.process_loop_events();

    // Expected: [E, D]
    REQUIRE(root.object_count() == 2);
    REQUIRE(root.get_object(0) == e);
    REQUIRE(root.get_object(1) == d);
}

TEST_CASE("mixed removes and moves across nested subtrees")
{
    TestRootItem root;
    Item* parent1        = root.emplace_back<Item>();
    Item* parent2        = root.emplace_back<Item>();
    Item* child_a        = parent1->emplace_back<Item>();
    Item* child_b        = parent1->emplace_back<Item>();
    Item* child_c        = parent2->emplace_back<Item>();
    ObjectHeartBeat a_hb = child_a->heartbeat();
    ObjectHeartBeat b_hb = child_b->heartbeat();

    // Strip parent1's children, remove parent1, and move child_c to root — all deferred
    parent1->remove_later(child_a);
    parent1->remove_later(child_b);
    root.remove_later(parent1);
    child_c->move_later(&root, 0);

    root.process_loop_events();

    REQUIRE(a_hb.expired());
    REQUIRE(b_hb.expired());
    REQUIRE(root.object_count() == 2); // [child_c, parent2]
    REQUIRE(root.get_object(0) == child_c);
    REQUIRE(root.get_object(1) == parent2);
    REQUIRE(parent2->object_count() == 0);
}
