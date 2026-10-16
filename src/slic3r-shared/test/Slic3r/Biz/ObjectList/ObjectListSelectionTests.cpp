#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/ObjectList/ObjectListInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include "Slic3r/Directories.hpp"

#include <set>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Kind = ObjectList::ListRow::Kind;
using ObjectList::ClickModifier;
using Elements = std::set<Domain::ElementRef>;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

/**
 * @brief Objects A (instances a1, a2), B (volumes bv1..bv3) and C, listed in this order on the
 * first bed.
 */
struct ObjectListSelectionFixture
{
    ObjectListSelectionFixture()
    {
        set_data_dir(Tests::get_datadir().string());
        project_interactor.preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );
        project_interactor.new_project();

        const Domain::Project& project = project_interactor.selected_project();
        const auto bed_center = project.bed_container().beds().front()->center();
        const auto cube = []() { return Domain::TriangleMesh{TriMesh::make_cube(20, 20, 20)}; };
        const auto add_object = [&](double x_offset) -> const Domain::ModelObject&
        {
            scene_interactor.new_object_from_mesh(cube());
            Domain::Transform3d xform = Domain::Transform3d::Identity();
            xform.translate(Domain::Vec3d{bed_center.x() - 10 + x_offset, bed_center.y() - 10, 0});
            scene_interactor.transform_selection(xform.matrix());
            return *project.model().objects.back();
        };

        const Domain::ModelObject& a = add_object(-40);
        scene_interactor.add_instance(Domain::Vec2d(0, 30));
        const Domain::ModelObject& b = add_object(0);
        scene_interactor.add_volume_from_mesh(cube(), Domain::ModelVolumeType::MODEL_PART);
        scene_interactor.add_volume_from_mesh(cube(), Domain::ModelVolumeType::MODEL_PART);
        const Domain::ModelObject& c = add_object(40);

        REQUIRE(a.instances.size() == 2);
        REQUIRE(b.volumes.size() == 3);
        obj_a = a.id().id;
        obj_b = b.id().id;
        obj_c = c.id().id;
        a1    = {obj_a, a.instances[0]->id().id};
        a2    = {obj_a, a.instances[1]->id().id};
        b1    = {obj_b, b.instances[0]->id().id};
        c1    = {obj_c, c.instances[0]->id().id};
        bv1   = b.volumes[0]->id().id;
        bv2   = b.volumes[1]->id().id;
        bv3   = b.volumes[2]->id().id;

        const Domain::BedInstance& bed =
            *project.config_containers().front()->bed_instances().front();
        REQUIRE(bed.model_instances.size() == 4);
        bed_1 = bed.id().id;
    }

    ObjectList::ListRow
    row(Kind kind, size_t object_id, size_t instance_id = 0, size_t volume_id = 0) const
    {
        return {kind, bed_1, object_id, instance_id, volume_id};
    }

    void click(const ObjectList::ListRow& row, ClickModifier modifier = ClickModifier::None)
    {
        scene_interactor.object_list_interactor().select_row(row, modifier);
    }

    Elements selected() const
    {
        const Scene::ObjectSelection& selection = scene_interactor.object_selection();
        return {selection.elements.begin(), selection.elements.end()};
    }

    /** @brief Adds an instance of C placed on the bed instance at @p bed_index. */
    Domain::ElementRef add_c_instance_on_bed(size_t bed_index)
    {
        const Domain::ConfigContainer& config_container =
            *project_interactor.selected_project().config_containers().front();
        const Domain::BedInstance& bed = *config_container.bed_instances()[bed_index];
        const Domain::Vec3d bed_offset = bed.transformation.get_offset()
            - config_container.bed_instances()[0]->transformation.get_offset();

        click(row(Kind::Instance, obj_c, c1.instance_id));
        const Domain::ElementRefs added =
            scene_interactor.add_instance(Domain::Vec2d(bed_offset.x(), bed_offset.y()));
        REQUIRE(added.size() == 1);
        REQUIRE(bed.model_instances.size() == 1);
        REQUIRE(bed.model_instances.front()->id().id == added.front().instance_id);
        scene_interactor.clear_object_selection();
        return added.front();
    }

    std::set<size_t> selected_volumes() const
    {
        std::set<size_t> volumes;
        for (const Domain::ElementRef& element : scene_interactor.object_selection().elements) {
            volumes.insert(element.volume_id);
        }
        return volumes;
    }

    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};

    size_t obj_a{0}, obj_b{0}, obj_c{0};
    Domain::ElementRef a1, a2, b1, c1;
    size_t bv1{0}, bv2{0}, bv3{0};
    size_t bed_1{0};
};

TEST_CASE_METHOD(ObjectListSelectionFixture, "ObjectList plain and Ctrl click", "[ObjectList]")
{
    click(row(Kind::Object, obj_a));
    CHECK(selected() == Elements{a1, a2});

    click(row(Kind::Object, obj_c), ClickModifier::Ctrl);
    CHECK(selected() == Elements{a1, a2, c1});

    click(row(Kind::Instance, obj_b, b1.instance_id), ClickModifier::Ctrl);
    CHECK(selected() == Elements{a1, a2, b1, c1});

    SECTION("Ctrl click on a selected row deselects it")
    {
        click(row(Kind::Object, obj_a), ClickModifier::Ctrl);
        CHECK(selected() == Elements{b1, c1});
    }

    SECTION("Ctrl click on volumes keeps an instance selection")
    {
        click(row(Kind::Volume, obj_b, 0, bv1), ClickModifier::Ctrl);
        click(row(Kind::Volumes, obj_b), ClickModifier::Ctrl);
        CHECK(selected() == Elements{a1, a2, b1, c1});
    }

    SECTION("Ctrl click on the instances group adds all its instances")
    {
        click(row(Kind::Object, obj_c));
        click(row(Kind::Instances, obj_a), ClickModifier::Ctrl);
        CHECK(selected() == Elements{a1, a2, c1});
    }
}

TEST_CASE_METHOD(ObjectListSelectionFixture, "ObjectList Shift click", "[ObjectList]")
{
    SECTION("Objects between")
    {
        click(row(Kind::Object, obj_a));
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2, b1, c1});

        // Shift keeps the anchor.
        click(row(Kind::Object, obj_b), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2, b1});
    }

    SECTION("From an instance, the parent object isn't selected")
    {
        click(row(Kind::Instance, obj_a, a2.instance_id));
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        CHECK(selected() == Elements{a2, b1, c1});

        click(row(Kind::Instance, obj_a, a1.instance_id), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2});
    }

    SECTION("To an instance, the parent object isn't selected")
    {
        click(row(Kind::Object, obj_c));
        click(row(Kind::Instance, obj_a, a2.instance_id), ClickModifier::Shift);
        CHECK(selected() == Elements{a2, b1, c1});
    }

    SECTION("To a volume, its parent object is selected")
    {
        click(row(Kind::Object, obj_a));
        click(row(Kind::Volume, obj_b, 0, bv2), ClickModifier::Shift);
        CHECK(scene_interactor.object_selection().mode == Scene::SelectionMode::Instance);
        CHECK(selected() == Elements{a1, a2, b1});
    }
}

TEST_CASE_METHOD(ObjectListSelectionFixture, "ObjectList volume selection", "[ObjectList]")
{
    click(row(Kind::Volume, obj_b, 0, bv1));
    REQUIRE(scene_interactor.object_selection().mode == Scene::SelectionMode::Volume);
    CHECK(selected_volumes() == std::set<size_t>{bv1});

    click(row(Kind::Volume, obj_b, 0, bv3), ClickModifier::Ctrl);
    CHECK(selected_volumes() == std::set<size_t>{bv1, bv3});

    SECTION("Other rows can't be added")
    {
        click(row(Kind::Object, obj_a), ClickModifier::Ctrl);
        click(row(Kind::Instance, obj_a, a1.instance_id), ClickModifier::Ctrl);
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        click(ObjectList::ListRow{Kind::Bed, bed_1}, ClickModifier::Ctrl);
        CHECK(selected_volumes() == std::set<size_t>{bv1, bv3});
    }

    SECTION("Ctrl click on a selected volume deselects it")
    {
        click(row(Kind::Volume, obj_b, 0, bv1), ClickModifier::Ctrl);
        CHECK(selected_volumes() == std::set<size_t>{bv3});
    }

    SECTION("Shift click selects the volumes between")
    {
        click(row(Kind::Volume, obj_b, 0, bv2));
        click(row(Kind::Volume, obj_b, 0, bv1), ClickModifier::Shift);
        CHECK(selected_volumes() == std::set<size_t>{bv1, bv2});
    }
}

TEST_CASE_METHOD(ObjectListSelectionFixture, "ObjectList object selection state", "[ObjectList]")
{
    using ObjectList::ObjectSelectionState;

    const Domain::Project& project = project_interactor.selected_project();
    const ObjectList::ObjectObservableList objects(
        project_interactor,
        project_interactor.selected_project_id(),
        project.config_containers().front()->id().id,
        bed_1
    );
    const auto state = [&objects](size_t object_id)
    {
        for (size_t i = 0; i < objects.size(); ++i) {
            if (objects.at(i).id == object_id) {
                return objects.at(i).selection_state;
            }
        }
        FAIL("Object isn't listed");
        return ObjectSelectionState::None;
    };

    click(row(Kind::Object, obj_a));
    click(row(Kind::Object, obj_b), ClickModifier::Ctrl);
    CHECK(state(obj_a) == ObjectSelectionState::Full);
    CHECK(state(obj_b) == ObjectSelectionState::Full);
    CHECK(state(obj_c) == ObjectSelectionState::None);

    click(row(Kind::Instance, obj_a, a2.instance_id));
    CHECK(state(obj_a) == ObjectSelectionState::Partial);
    CHECK(state(obj_b) == ObjectSelectionState::None);

    click(row(Kind::Volume, obj_b, 0, bv2));
    CHECK(state(obj_a) == ObjectSelectionState::None);
    CHECK(state(obj_b) == ObjectSelectionState::Partial);
}

TEST_CASE_METHOD(ObjectListSelectionFixture, "ObjectList bed selection", "[ObjectList]")
{
    const Domain::ConfigContainer& config_container =
        *project_interactor.selected_project().config_containers().front();
    const size_t cc_id = config_container.id().id;
    scene_interactor.add_bed_instance(cc_id);
    scene_interactor.add_bed_instance(cc_id);
    const size_t bed_2 = config_container.bed_instances()[1]->id().id;
    const size_t bed_3 = config_container.bed_instances()[2]->id().id;
    const Scene::BedSelection& beds = scene_interactor.bed_selection();

    // A bed click keeps the object selection.
    scene_interactor.clear_object_selection();
    click({Kind::Bed, bed_1});
    CHECK(beds.selected_beds() == Domain::BedRefs{{cc_id, bed_1}});

    SECTION("Shift click selects the beds and the objects between")
    {
        click({Kind::Bed, bed_3}, ClickModifier::Shift);
        CHECK(beds.is_selected({cc_id, bed_1}));
        CHECK(beds.is_selected({cc_id, bed_2}));
        CHECK(beds.is_selected({cc_id, bed_3}));
        CHECK(selected() == Elements{a1, a2, b1, c1});
    }

    SECTION("Shift click between beds selects only the instances placed on them")
    {
        const Domain::ElementRef c2 = add_c_instance_on_bed(1);

        click({Kind::Bed, bed_3});
        click({Kind::Bed, bed_2}, ClickModifier::Shift);
        CHECK(beds.selected_beds().size() == 2);
        CHECK(beds.is_selected({cc_id, bed_2}));
        CHECK(beds.is_selected({cc_id, bed_3}));
        CHECK(selected() == Elements{c2});

        // The instances of the last bed in the list aren't selected.
        click({Kind::Bed, bed_1});
        click({Kind::Bed, bed_2}, ClickModifier::Shift);
        CHECK(beds.is_selected({cc_id, bed_1}));
        CHECK(beds.is_selected({cc_id, bed_2}));
        CHECK(selected() == Elements{a1, a2, b1, c1});
    }

    SECTION("Shift click from an instance adds only the instances listed under the range beds")
    {
        const Domain::ElementRef c2 = add_c_instance_on_bed(1);

        // C between the ends: only its bed_1 instance.
        click(row(Kind::Instance, obj_a, a1.instance_id));
        click(row(Kind::Volume, obj_b, 0, bv2), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2, b1});
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2, b1, c1});

        // C at the ends: the clicked bed brings only C's bed_2 instance.
        click({Kind::Bed, bed_2}, ClickModifier::Shift);
        CHECK(beds.is_selected({cc_id, bed_2}));
        CHECK(selected() == Elements{a1, a2, b1, c1, c2});

        // From bed_2's C row towards bed_1's instances.
        click(ObjectList::ListRow{Kind::Instances, bed_2, obj_c});
        CHECK(selected() == Elements{c2});
        click(row(Kind::Object, obj_b), ClickModifier::Shift);
        CHECK(selected() == Elements{b1, c1, c2});
    }

    SECTION("Shift click between objects adds only the instances listed under the range beds")
    {
        const Domain::ElementRef c2 = add_c_instance_on_bed(1);

        click(row(Kind::Object, obj_a));
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        CHECK(selected() == Elements{a1, a2, b1, c1});

        click(row(Kind::Instance, obj_a, a2.instance_id));
        click(row(Kind::Object, obj_c), ClickModifier::Shift);
        CHECK(selected() == Elements{a2, b1, c1});

        // A plain click still selects the whole object.
        click(ObjectList::ListRow{Kind::Object, bed_2, obj_c});
        CHECK(selected() == Elements{c1, c2});
        click(row(Kind::Volume, obj_b, 0, bv1), ClickModifier::Shift);
        CHECK(selected() == Elements{b1, c1, c2});
    }

    SECTION("Shift click from an object selects the clicked bed with its objects")
    {
        click({Kind::Bed, bed_3});
        click(row(Kind::Object, obj_b));
        click({Kind::Bed, bed_1}, ClickModifier::Shift);
        CHECK(beds.selected_beds() == Domain::BedRefs{{cc_id, bed_1}});
        CHECK(selected() == Elements{a1, a2, b1, c1});
    }

    SECTION("Ctrl click adds and removes beds and objects")
    {
        click({Kind::Bed, bed_2}, ClickModifier::Ctrl);
        click(row(Kind::Object, obj_c), ClickModifier::Ctrl);
        CHECK(beds.is_selected({cc_id, bed_1}));
        CHECK(beds.is_selected({cc_id, bed_2}));
        CHECK(selected() == Elements{c1});

        click({Kind::Bed, bed_1}, ClickModifier::Ctrl);
        CHECK(beds.selected_beds() == Domain::BedRefs{{cc_id, bed_2}});
    }

    SECTION("Ctrl click on a volume does nothing")
    {
        click(row(Kind::Volume, obj_b, 0, bv1), ClickModifier::Ctrl);
        CHECK(selected().empty());
    }
}
