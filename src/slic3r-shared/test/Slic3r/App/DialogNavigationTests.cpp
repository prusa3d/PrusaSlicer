#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <Slic3r/App/DialogNavigation.hpp>
#include <Slic3r/App/Yoga/Dialog.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>

#include "Yoga/ImGuiFixture.hpp"

using namespace Slic3r;
using namespace Slic3r::App;
using namespace Slic3r::App::Yoga;

struct DialogFixture : public ImGuiFixture
{
    void init_dialogs()
    {
        dialog_A1  = root_item.emplace_back<Dialog>();
        dialog_A2  = root_item.emplace_back<Dialog>();
        dialog_A2A = root_item.emplace_back<Dialog>();
        dialog_A2B = root_item.emplace_back<Dialog>();
        dialog_B1  = root_item.emplace_back<Dialog>();
        dialog_B2  = root_item.emplace_back<Dialog>();
        dialog_B3  = root_item.emplace_back<Dialog>();

        dialog_A1->attach_to_center();
        dialog_navigation.insert_dialog(dialog_A1);
        dialog_A2->attach_to_center();
        dialog_navigation.insert_dialog(dialog_A2, dialog_A1);
        dialog_A2A->attach_to_center();
        dialog_navigation.insert_dialog(dialog_A2A, dialog_A2);
        dialog_A2B->attach_to_center();
        dialog_navigation.insert_dialog(dialog_A2B, dialog_A2);
        dialog_B1->attach_to_center();
        dialog_navigation.insert_dialog(dialog_B1);
        dialog_B2->attach_to_center();
        dialog_navigation.insert_dialog(dialog_B2, dialog_B1);
        dialog_B3->attach_to_center();
        dialog_navigation.insert_dialog(dialog_B3, dialog_B2);
    }

    RootItem root_item;
    DialogNavigation dialog_navigation;
    Dialog* dialog_A1{nullptr};
    Dialog* dialog_A2{nullptr};
    Dialog* dialog_A2A{nullptr};
    Dialog* dialog_A2B{nullptr};
    Dialog* dialog_B1{nullptr};
    Dialog* dialog_B2{nullptr};
    Dialog* dialog_B3{nullptr};
};

TEST_CASE_METHOD(ImGuiFixture, "DialogLogic simple open")
{
    DialogNavigation dialog_navigation;

    RootItem root_item;

    Dialog* dialog = root_item.emplace_back<Dialog>();

    dialog->attach_to_center();
    dialog_navigation.insert_dialog(dialog);

    dialog_navigation.open_dialog(dialog);

    REQUIRE(dialog->opened());
}

TEST_CASE_METHOD(ImGuiFixture, "DialogLogic simple close")
{
    DialogNavigation dialog_navigation;

    RootItem root_item;

    Dialog* dialog = root_item.emplace_back<Dialog>();

    dialog->attach_to_center();
    dialog_navigation.insert_dialog(dialog);

    dialog_navigation.open_dialog(dialog);

    dialog_navigation.open_dialog(nullptr);

    REQUIRE(!dialog->opened());
}

TEST_CASE_METHOD(DialogFixture, "DialogLogic tree open")
{
    init_dialogs();

    dialog_navigation.open_dialog(dialog_A2A);

    REQUIRE(dialog_A1->opened());
    REQUIRE(dialog_A2->opened());
    REQUIRE(dialog_A2A->opened());

    REQUIRE(!dialog_A2B->opened());
    REQUIRE(!dialog_B1->opened());
    REQUIRE(!dialog_B2->opened());
    REQUIRE(!dialog_B3->opened());
}

TEST_CASE_METHOD(DialogFixture, "DialogLogic tree close")
{
    init_dialogs();

    dialog_navigation.open_dialog(dialog_A2A);

    dialog_navigation.open_dialog(nullptr);

    REQUIRE(!dialog_A1->opened());
    REQUIRE(!dialog_A2->opened());
    REQUIRE(!dialog_A2A->opened());
    REQUIRE(!dialog_A2B->opened());
    REQUIRE(!dialog_B1->opened());
    REQUIRE(!dialog_B2->opened());
    REQUIRE(!dialog_B3->opened());
}

TEST_CASE_METHOD(DialogFixture, "DialogLogic tree switch")
{
    init_dialogs();

    dialog_navigation.open_dialog(dialog_A2A);

    dialog_navigation.open_dialog(dialog_B2);

    REQUIRE(!dialog_A1->opened());
    REQUIRE(!dialog_A2->opened());
    REQUIRE(!dialog_A2A->opened());
    REQUIRE(!dialog_A2B->opened());
    REQUIRE(dialog_B1->opened());
    REQUIRE(dialog_B2->opened());
    REQUIRE(!dialog_B3->opened());
}

TEST_CASE_METHOD(DialogFixture, "DialogLogic tree switch same branch")
{
    init_dialogs();

    dialog_navigation.open_dialog(dialog_A2A);

    REQUIRE(dialog_A1->opened());
    REQUIRE(dialog_A2->opened());
    REQUIRE(dialog_A2A->opened());
    REQUIRE(!dialog_A2B->opened());
    REQUIRE(!dialog_B1->opened());
    REQUIRE(!dialog_B2->opened());
    REQUIRE(!dialog_B3->opened());

    dialog_navigation.open_dialog(dialog_A2B);

    REQUIRE(dialog_A1->opened());
    REQUIRE(dialog_A2->opened());
    REQUIRE(!dialog_A2A->opened());
    REQUIRE(dialog_A2B->opened());
    REQUIRE(!dialog_B1->opened());
    REQUIRE(!dialog_B2->opened());
    REQUIRE(!dialog_B3->opened());
}
