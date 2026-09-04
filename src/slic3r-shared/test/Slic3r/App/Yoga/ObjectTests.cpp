
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>

#include "ImGuiFixture.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinRel;

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Object] set_object_name")
{
    RootItem item;

    Object* object1 = item.emplace_back<Object>();
    object1->set_object_name("foo");

    Object* object2 = item.emplace_back<Object>();
    object2->set_object_name("foo");

    Object* object3 = item.emplace_back<Object>();
    object3->set_object_name("foo");

    item.remove(object3);

    Object* object4 = item.emplace_back<Object>();
    object4->set_object_name("foo");

    REQUIRE(object1->object_name() == "foo_1");
    REQUIRE(object2->object_name() == "foo_2");
    REQUIRE(object4->object_name() == "foo_4");
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Object] heartbeat")
{
    RootItem item;

    Object* object1                   = item.emplace_back<Object>();
    ObjectHeartBeat object1_heartbeat = object1->heartbeat();

    Object* object2                   = item.emplace_back<Object>();
    ObjectHeartBeat object2_heartbeat = object2->heartbeat();

    item.remove(object2);

    REQUIRE(!object1_heartbeat.expired());
    REQUIRE(object2_heartbeat.expired());
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Object] children order and access")
{
    RootItem item;

    Object* a = item.emplace_back<Object>();
    Object* b = item.emplace_back<Object>();
    Object* c = item.emplace_back<Object>();

    std::vector<Object*> children = item.objects();
    REQUIRE(children.size() == 3);
    REQUIRE(children[0] == a);
    REQUIRE(children[1] == b);
    REQUIRE(children[2] == c);

    REQUIRE(item.get_object(0) == a);
    REQUIRE(item.get_object(1) == b);
    REQUIRE(item.get_object(2) == c);
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Object] index_of")
{
    RootItem item;

    Object* a = item.emplace_back<Object>();
    Object* b = item.emplace_back<Object>();
    Object stranger;

    REQUIRE(item.index_of(a) == std::optional<size_t>(0));
    REQUIRE(item.index_of(b) == std::optional<size_t>(1));
    REQUIRE(item.index_of(&stranger) == std::nullopt);
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Object] object count")
{
    RootItem item;

    Object* robject = item.emplace_back<Object>();
    item.emplace_back<Object>();
    item.emplace_back<Object>();
    item.emplace_back<Object>();

    robject->emplace_back<Object>();
    robject->emplace_back<Object>();
    robject->emplace_back<Object>();
    robject->emplace_back<Object>();
    robject->emplace_back<Object>();

    REQUIRE(item.object_count() == 4);
    REQUIRE(robject->object_count() == 5);
}
