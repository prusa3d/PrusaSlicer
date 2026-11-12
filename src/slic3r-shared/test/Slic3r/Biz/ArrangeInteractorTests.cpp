#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/IArrangeEventsListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

using namespace Slic3r;

struct ArrangeEventsListener : Biz::IArrangeEventsListener
{
    void on_elements_not_arranged(Domain::SelectionId project, const Domain::ElementRefs& elements)
        override
    {
        (void)project;
        not_arranged = elements;
    }

    Domain::ElementRefs not_arranged;
};

struct ArrangeInteractorFixture
{
    ArrangeInteractorFixture()
    {
        Biz::Platform::PlatformServices::instance().set_job_manager(
            std::make_unique<Biz::Platform::JobManager::JobManager>(dispatcher));

        set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir()));

        project_id = project_interactor.new_project();
        arrange_interactor.add_listener<Biz::IArrangeEventsListener>(&arrange_listener);

        project = &workbench.project(project_id);
        config_container = &project_interactor.selected_config_container();

        const auto& bed{*project->bed_container().beds().front()};
        const Domain::Vec2d bed_size{bed.contour_aabb_extent()};
        cube_size = std::min(bed_size.x(), bed_size.y()) * 0.7;

        initial_instance = add_cube(0.25);
        REQUIRE(initial_instance);
    }

    ~ArrangeInteractorFixture()
    {
        dispatcher.close();
    }

    const Domain::ModelInstance* add_cube(double relative_size = 1.0)
    {
        const double side{cube_size * relative_size};
        const Domain::ElementRefs refs{scene_interactor.new_object_from_mesh(
            Domain::TriangleMesh{Biz::Algorithms::TriangleMesh::make_cube(side, side, side)})};

        REQUIRE(refs.size() == 1);
        const Domain::ElementRef& ref{refs.front()};
        const Domain::ModelInstance* instance{
            project->find_instance_by_id(ref.object_id, ref.instance_id)};
        REQUIRE(instance);
        return instance;
    }

    Domain::ElementRef instance_ref(const Domain::ModelInstance* instance) const
    {
        return Domain::ElementRef{instance->get_object()->id().id, instance->id().id};
    }

    std::vector<const Domain::ModelInstance*> add_overlapping_instances(
        const Domain::ModelInstance* source,
        std::size_t count)
    {
        std::vector<const Domain::ModelInstance*> created;
        created.reserve(count);

        scene_interactor.set_object_selection(
            {Biz::Scene::SelectionMode::Instance, {instance_ref(source)}});

        for (std::size_t index{}; index < count; ++index) {
            const Domain::ElementRefs refs{scene_interactor.add_instance(Domain::Vec2d{0.0, 0.0})};
            REQUIRE(refs.size() == 1);

            const Domain::ElementRef& ref{refs.front()};
            const Domain::ModelInstance* instance{
                project->find_instance_by_id(ref.object_id, ref.instance_id)};
            REQUIRE(instance);
            created.push_back(instance);
        }

        return created;
    }

    void run_arrange(
        const std::vector<Biz::BedToArrange>& beds,
        std::optional<std::size_t> config_container_to_add_beds,
        const Domain::ConstModelInstanceList& extra)
    {
        using namespace std::chrono_literals;

        bool finished{false};
        arrange_interactor.arrange(
            project_id,
            beds,
            config_container_to_add_beds,
            extra,
            Biz::Arrange::Settings{},
            [&finished]() { finished = true; });

        const auto wait_start{std::chrono::steady_clock::now()};
        while (!finished && (std::chrono::steady_clock::now() - wait_start) < 5s) {
            dispatcher.dispatch_enqueued();
            std::this_thread::sleep_for(1ms);
        }

        REQUIRE(finished);
    }

    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Biz::Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    ArrangeEventsListener arrange_listener;
    Biz::ArrangeInteractor arrange_interactor{scene_interactor, workbench};
    Domain::SelectionId project_id{0};
    Domain::Project* project{nullptr};
    Domain::ConfigContainer* config_container{nullptr};
    const Domain::ModelInstance* initial_instance{nullptr};
    double cube_size{0.0};
};

TEST_CASE_METHOD(
    ArrangeInteractorFixture,
    "Arranges all instances in config container and adds beds when needed",
    "[ArrangeInteractor]")
{
    add_cube();
    add_cube();

    const std::size_t beds_before{config_container->bed_instances().size()};
    const Domain::SelectionId config_container_id{config_container->id().id};

    std::vector<Biz::BedToArrange> beds;
    Domain::ConstModelInstanceList instances;
    for (const auto& bed_instance : config_container->bed_instances()) {
        beds.push_back(
            {Domain::BedRef{config_container_id, bed_instance->id().id}, bed_instance->index()});
        instances.insert(
            instances.end(),
            bed_instance->model_instances.begin(),
            bed_instance->model_instances.end());
    }
    const Domain::ModelInstanceList& unplaced{
        scene_interactor.unplaced_model_instances(project_id)};
    instances.insert(instances.end(), unplaced.begin(), unplaced.end());

    run_arrange(beds, config_container_id, instances);

    CHECK(config_container->bed_instances().size() > beds_before);
    CHECK(project->unplaced_model_instances().empty());
    CHECK(arrange_listener.not_arranged.empty());
}

TEST_CASE_METHOD(
    ArrangeInteractorFixture,
    "Arranges selected instances in config container while keeping non selected fixed",
    "[ArrangeInteractor]")
{
    const Domain::ModelInstance* selected_a{add_cube()};
    const Domain::ModelInstance* selected_b{add_cube()};
    const Domain::ModelInstance* selected_c{add_cube()};

    const std::size_t beds_before{config_container->bed_instances().size()};
    const Domain::SelectionId config_container_id{config_container->id().id};

    const Domain::Vec3d fixed_offset_before{initial_instance->get_offset()};

    const Domain::ElementRefs
        selection{instance_ref(selected_a), instance_ref(selected_b), instance_ref(selected_c)};
    scene_interactor.set_object_selection({Biz::Scene::SelectionMode::Instance, selection});

    auto is_selected = [&selection](const Domain::ElementRef& ref)
    { return std::find(selection.begin(), selection.end(), ref) != selection.end(); };
    std::vector<Biz::BedToArrange> beds;
    for (const auto& bed_instance : config_container->bed_instances()) {
        Biz::BedToArrange bed{{config_container_id, bed_instance->id().id}, bed_instance->index()};
        bed.fixed_wipe_tower = true;
        for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
            if (!is_selected(instance_ref(instance))) {
                bed.fixed.push_back(instance);
            }
        }
        beds.push_back(std::move(bed));
    }

    Domain::ConstModelInstanceList selected_instances;
    for (const Domain::ElementRef& ref : selection) {
        const Domain::ModelInstance* instance{
            project->find_instance_by_id(ref.object_id, ref.instance_id)};
        REQUIRE(instance);
        selected_instances.push_back(instance);
    }

    run_arrange(beds, config_container_id, selected_instances);

    CHECK(initial_instance->get_offset().isApprox(fixed_offset_before));
    CHECK(config_container->bed_instances().size() > beds_before);

    std::set<std::size_t> selected_ids;
    for (const Domain::ElementRef& ref : selection) {
        selected_ids.insert(ref.instance_id);
    }
    for (const Domain::ModelInstance* unplaced_instance : project->unplaced_model_instances()) {
        CHECK(!selected_ids.contains(unplaced_instance->id().id));
    }
}

TEST_CASE_METHOD(
    ArrangeInteractorFixture,
    "Arranges full selected bed without adding beds and reports not arranged instances",
    "[ArrangeInteractor]")
{
    const Domain::ModelInstance* base_instance{add_cube()};
    add_overlapping_instances(base_instance, 3);

    const std::size_t beds_before{config_container->bed_instances().size()};
    REQUIRE(!scene_interactor.bed_selection().selected_beds().empty());
    const Domain::BedRef bed_ref{scene_interactor.bed_selection().selected_beds().front()};

    scene_interactor.bed_selection().select_one(bed_ref);

    const Domain::BedInstance* bed_instance{project->find_bed_instance_by_id(bed_ref.instance_id)};
    REQUIRE(bed_instance);

    Biz::BedToArrange bed_to_arrange;
    bed_to_arrange.ref   = bed_ref;
    bed_to_arrange.index = bed_instance->index();
    bed_to_arrange.arrangeable.insert(
        bed_to_arrange.arrangeable.end(),
        bed_instance->model_instances.begin(),
        bed_instance->model_instances.end());

    REQUIRE(bed_to_arrange.arrangeable.size() >= 2);

    run_arrange({bed_to_arrange}, std::nullopt, {});

    CHECK(config_container->bed_instances().size() == beds_before);
    CHECK(!arrange_listener.not_arranged.empty());
    CHECK(arrange_listener.not_arranged.size() < bed_to_arrange.arrangeable.size());
}

TEST_CASE_METHOD(
    ArrangeInteractorFixture,
    "Arranges selected instances on selected bed and keeps non selected fixed",
    "[ArrangeInteractor]")
{
    const Domain::ModelInstance* selected_a{add_cube(0.35)};
    const auto selected_clones{add_overlapping_instances(selected_a, 1)};
    REQUIRE(selected_clones.size() == 1);
    const Domain::ModelInstance* selected_b{selected_clones.front()};

    const std::size_t beds_before{config_container->bed_instances().size()};
    REQUIRE(!scene_interactor.bed_selection().selected_beds().empty());
    const Domain::BedRef bed_ref{scene_interactor.bed_selection().selected_beds().front()};
    scene_interactor.bed_selection().select_one(bed_ref);

    const Domain::Vec3d fixed_offset_before{initial_instance->get_offset()};

    const Domain::ElementRefs selection{instance_ref(selected_a), instance_ref(selected_b)};
    scene_interactor.set_object_selection({Biz::Scene::SelectionMode::Instance, selection});

    const Domain::Vec3d selected_a_offset_before{selected_a->get_offset()};
    const Domain::Vec3d selected_b_offset_before{selected_b->get_offset()};
    REQUIRE(selected_a_offset_before.isApprox(selected_b_offset_before));

    const Domain::BedRefs selected_beds{scene_interactor.bed_selection().selected_beds()};
    std::set<const Domain::ModelInstance*> selected_on_bed;
    auto is_selected = [&selection](const Domain::ElementRef& ref)
    { return std::find(selection.begin(), selection.end(), ref) != selection.end(); };
    std::vector<Biz::BedToArrange> beds;

    for (const Domain::BedRef& selected_bed : selected_beds) {
        Biz::BedToArrange bed;
        bed.ref              = selected_bed;
        bed.fixed_wipe_tower = false;

        const Domain::BedInstance* bed_instance{
            project->find_bed_instance_by_id(selected_bed.instance_id)};
        REQUIRE(bed_instance);
        bed.index = bed_instance->index();

        for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
            if (is_selected(instance_ref(instance))) {
                bed.arrangeable.push_back(instance);
                selected_on_bed.insert(instance);
            } else {
                bed.fixed.push_back(instance);
            }
        }

        beds.push_back(std::move(bed));
    }

    Domain::ConstModelInstanceList extra;
    for (const Domain::ElementRef& ref : selection) {
        const Domain::ModelInstance* instance{
            project->find_instance_by_id(ref.object_id, ref.instance_id)};
        REQUIRE(instance);
        if (!selected_on_bed.contains(instance)) {
            extra.push_back(instance);
        }
    }

    run_arrange(beds, std::nullopt, extra);

    CHECK(config_container->bed_instances().size() == beds_before);
    CHECK(initial_instance->get_offset().isApprox(fixed_offset_before));

    for (const Domain::ElementRef& ref : arrange_listener.not_arranged) {
        CHECK(std::find(selection.begin(), selection.end(), ref) != selection.end());
    }

    const std::size_t selected_not_arranged_count{arrange_listener.not_arranged.size()};
    CHECK(selected_not_arranged_count <= selection.size());
    CHECK(selected_not_arranged_count < selection.size());

    const bool at_least_one_selected_moved{
        !selected_a->get_offset().isApprox(selected_a_offset_before)
        || !selected_b->get_offset().isApprox(selected_b_offset_before)};
    CHECK((selected_not_arranged_count > 0 || at_least_one_selected_moved));
}
