#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Scene/MouseBindingScheme.hpp"

using namespace Slic3r;
using namespace Slic3r::App;
using namespace Slic3r::App::Platform;

TEST_CASE("PrusaSlicer mouse binding scheme rotates on Left and pans on Right/Middle", "[MouseBindingScheme]") {
    Scene::MouseBindingScheme scheme{MouseNavigationScheme::PrusaSlicer};
    REQUIRE(scheme.is_prusaslicer());

    REQUIRE(scheme.is_rotate_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));

    REQUIRE(scheme.is_pan_start(MouseButton::Right, KeyModifiers(KeyModifier::None)));
    REQUIRE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Right, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
}

TEST_CASE("Tinkercad mouse binding scheme rotates on Right, pans on Middle or Shift+Right", "[MouseBindingScheme]") {
    Scene::MouseBindingScheme scheme{MouseNavigationScheme::Tinkercad};
    REQUIRE_FALSE(scheme.is_prusaslicer());

    REQUIRE(scheme.is_rotate_start(MouseButton::Right, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Right, KeyModifiers(KeyModifier::None)));

    REQUIRE(scheme.is_pan_start(MouseButton::Right, KeyModifiers(KeyModifier::Shift)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Right, KeyModifiers(KeyModifier::Shift)));

    REQUIRE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));

    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
}

TEST_CASE("Blender mouse binding scheme rotates/pans on Middle, never on Left", "[MouseBindingScheme]") {
    Scene::MouseBindingScheme scheme{MouseNavigationScheme::Blender};
    REQUIRE_FALSE(scheme.is_prusaslicer());

    REQUIRE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));

    REQUIRE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::Shift)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::Shift)));

    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
}

TEST_CASE("SolidWorks mouse binding scheme rotates on Middle, pans on Ctrl+Middle", "[MouseBindingScheme]") {
    Scene::MouseBindingScheme scheme{MouseNavigationScheme::SolidWorks};
    REQUIRE_FALSE(scheme.is_prusaslicer());

    REQUIRE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));

    REQUIRE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::Ctrl)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::Ctrl)));

    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
}

TEST_CASE("Fusion mouse binding scheme pans on Middle, rotates on Shift+Middle", "[MouseBindingScheme]") {
    Scene::MouseBindingScheme scheme{MouseNavigationScheme::Fusion};
    REQUIRE_FALSE(scheme.is_prusaslicer());

    REQUIRE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::None)));

    REQUIRE(scheme.is_rotate_start(MouseButton::Middle, KeyModifiers(KeyModifier::Shift)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Middle, KeyModifiers(KeyModifier::Shift)));

    REQUIRE_FALSE(scheme.is_rotate_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
    REQUIRE_FALSE(scheme.is_pan_start(MouseButton::Left, KeyModifiers(KeyModifier::None)));
}
