#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;

TEST_CASE("Scene basic transform", "[Node]") {
    using namespace Slic3r;
    using namespace Slic3r::App::Scene;
    using Catch::Matchers::WithinRel;

    Scene scene;

    auto* n1 = new Node;

    REQUIRE(n1->parent() == nullptr);

    scene.add_child(n1);

    REQUIRE(n1->parent() != nullptr);

    auto* n2 = new Node;

    scene.add_child(n2, n1);

    {
        const auto& n1_world = n1->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n1_world.x()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n1_world.y()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n1_world.z()), WithinRel(0, 0.0001));

        const auto& n2_world = n2->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n2_world.x()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n2_world.y()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n2_world.z()), WithinRel(0, 0.0001));
    }

    Transform3d t = Transform3d::Identity();
    t.translate(Vec3d{1, 2, 3});
    n1->set_local_transform(t);

    {
        const auto& n1_world = n1->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n1_world.x()), WithinRel(1, 0.0001));
        REQUIRE_THAT((n1_world.y()), WithinRel(2, 0.0001));
        REQUIRE_THAT((n1_world.z()), WithinRel(3, 0.0001));

        const auto& n2_world = n2->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n2_world.x()), WithinRel(1, 0.0001));
        REQUIRE_THAT((n2_world.y()), WithinRel(2, 0.0001));
        REQUIRE_THAT((n2_world.z()), WithinRel(3, 0.0001));
    }

    t = Transform3d::Identity();
    t.translate(Vec3d{-1, -2, -3});
    n2->set_local_transform(t);

    {
        const auto& n1_world = n1->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n1_world.x()), WithinRel(1, 0.0001));
        REQUIRE_THAT((n1_world.y()), WithinRel(2, 0.0001));
        REQUIRE_THAT((n1_world.z()), WithinRel(3, 0.0001));

        const auto& n2_world = n2->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n2_world.x()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n2_world.y()), WithinRel(0, 0.0001));
        REQUIRE_THAT((n2_world.z()), WithinRel(0, 0.0001));
    }

    t.setIdentity();
    t.translate(Vec3d{9, 8, 7});
    n2->set_world_transform(t);

    {
        const auto& n1_world = n1->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n1_world.x()), WithinRel(1, 0.0001));
        REQUIRE_THAT((n1_world.y()), WithinRel(2, 0.0001));
        REQUIRE_THAT((n1_world.z()), WithinRel(3, 0.0001));

        const auto& n2_world = n2->world_transform().matrix().block<3, 1>(0, 3);
        REQUIRE_THAT((n2_world.x()), WithinRel(9, 0.0001));
        REQUIRE_THAT((n2_world.y()), WithinRel(8, 0.0001));
        REQUIRE_THAT((n2_world.z()), WithinRel(7, 0.0001));
    }
}
