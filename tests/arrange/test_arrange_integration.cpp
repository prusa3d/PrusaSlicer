#include <catch2/catch.hpp>
#include "test_utils.hpp"

#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Items/ArrangeItem.hpp>
#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/SceneBuilder.hpp>
#include <arrange-wrapper/ModelArrange.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Format/3mf.hpp"

static Slic3r::Model get_example_model_with_20mm_cube()
{
    using namespace Slic3r;

    Model model;

    ModelObject* new_object = model.add_object();
    new_object->name = "20mm_cube";
    new_object->add_instance();
    TriangleMesh mesh = make_cube(20., 20., 20.);
    mesh.translate(Vec3f{-10.f, -10.f, 0.});
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    return model;
}

[[maybe_unused]]
static Slic3r::Model get_example_model_with_random_cube_objects(size_t N = 0)
{
    using namespace Slic3r;

    Model model;

    auto cube_count = N == 0 ? random_value(size_t(1), size_t(100)) : N;

    INFO("Cube count " << cube_count);

    ModelObject* new_object = model.add_object();
    new_object->name = "20mm_cube";
    TriangleMesh mesh = make_cube(20., 20., 20.);
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    for (size_t i = 0; i < cube_count; ++i) {
        ModelInstance *inst = new_object->add_instance();
        arr2::transform_instance(*inst,
                                 Vec2d{random_value(-arr2::UnscaledCoordLimit / 10., arr2::UnscaledCoordLimit / 10.),
                                       random_value(-arr2::UnscaledCoordLimit / 10., arr2::UnscaledCoordLimit / 10.)},
                                 random_value(0., 2 * PI));
    }

    return model;
}

static Slic3r::Model get_example_model_with_arranged_primitives()
{
    using namespace Slic3r;

    Model model;

    ModelObject* new_object = model.add_object();
    new_object->name = "20mm_cube";
    ModelInstance *cube_inst = new_object->add_instance();
    TriangleMesh mesh = make_cube(20., 20., 20.);
    mesh.translate(Vec3f{-10.f, -10.f, 0.});
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    ModelInstance *inst = new_object->add_instance(*cube_inst);
    auto tr = inst->get_matrix();
    tr.translate(Vec3d{25., 0., 0.});
    inst->set_transformation(Geometry::Transformation{tr});

    new_object = model.add_object();
    new_object->name = "20mm_cyl";
    new_object->add_instance();
    mesh = make_cylinder(10., 20.);
    mesh.translate(Vec3f{0., -25.f, 0.});
    new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    new_object = model.add_object();
    new_object->name = "20mm_sphere";
    new_object->add_instance();
    mesh = make_sphere(10.);
    mesh.translate(Vec3f{25., -25.f, 0.});
    new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    return model;
}


class RandomArrangeSettings: public Slic3r::arr2::ArrangeSettingsView {
    Slic3r::arr2::ArrangeSettingsDb::Values m_v;

    std::mt19937 m_rng;
public:
    explicit RandomArrangeSettings(int seed) : m_rng(seed)
    {
        std::uniform_real_distribution<float> fdist(0., 100.f);
        std::uniform_int_distribution<> bdist(0, 1);
        std::uniform_int_distribution<> dist;
        m_v.d_obj = fdist(m_rng);
        m_v.d_bed = fdist(m_rng);
        m_v.rotations = bdist(m_rng);
        m_v.geom_handling = static_cast<GeometryHandling>(dist(m_rng) % ghCount);
        m_v.arr_strategy  = static_cast<ArrangeStrategy>(dist(m_rng) % asCount);
        m_v.xl_align      = static_cast<XLPivots>(dist(m_rng) % xlpCount);
    }
    explicit RandomArrangeSettings() : m_rng(std::random_device{} ()) {}

    float get_distance_from_objects() const override { return m_v.d_obj; }
    float get_distance_from_bed() const override { return m_v.d_bed; }
    bool  is_rotation_enabled() const override { return m_v.rotations; }
    XLPivots get_xl_alignment() const override { return m_v.xl_align; }
    GeometryHandling get_geometry_handling() const override { return m_v.geom_handling; }
    ArrangeStrategy get_arrange_strategy() const override { return m_v.arr_strategy; }
};


TEST_CASE("ModelInstance should be retrievable when imbued into ArrangeItem",
          "[arrange2][integration]")
{
    using namespace Slic3r;

    Model model = get_example_model_with_20mm_cube();
    auto mi = model.objects.front()->instances.front();

    arr2::ArrangeItem itm;
    arr2::PhysicalOnlyVBedHandler vbedh;
    auto vbedh_ptr = static_cast<arr2::VirtualBedHandler *>(&vbedh);
    auto arrbl = arr2::ArrangeableModelInstance{mi, vbedh_ptr, nullptr, {0, 0}, std::nullopt};
    arr2::imbue_id(itm, arrbl.id());

    std::optional<ObjectID> id_returned = arr2::retrieve_id(itm);

    REQUIRE((id_returned && *id_returned == mi->id()));
}

struct PhysicalBed
{
    Slic3r::arr2::InfiniteBed bed;
    Slic3r::arr2::PhysicalOnlyVBedHandler vbedh;
    int bed_idx_min = 0, bed_idx_max = 0;
};

struct XStriderBed
{
    Slic3r::arr2::RectangleBed bed;
    Slic3r::arr2::XStriderVBedHandler vbedh;
    int bed_idx_min = 0, bed_idx_max = 100;

    XStriderBed() :
        bed{Slic3r::scaled(250.), Slic3r::scaled(210.)},
        vbedh{bounding_box(bed), bounding_box(bed).size().x() / 10} {}
};

TEMPLATE_TEST_CASE("Writing arrange transformations into ModelInstance should be correct",
                   "[arrange2][integration]",
                   PhysicalBed,
                   XStriderBed)
{
    auto [tx, ty, rot] = GENERATE(map(
        [](int i) {
            return std::make_tuple(-Slic3r::arr2::UnscaledCoordLimit / 2. + i * Slic3r::arr2::UnscaledCoordLimit / 100.,
                                   -Slic3r::arr2::UnscaledCoordLimit / 2. + i * Slic3r::arr2::UnscaledCoordLimit / 100.,
                                   -PI + i * (2 * PI / 100.));
        },
        range(0, 100)));

    using namespace Slic3r;

    Model model = get_example_model_with_20mm_cube();

    auto transl = scaled(Vec2d(tx, ty));

    INFO("Translation = : " << transl.transpose());
    INFO("Rotation is: " << rot * 180 / PI);

    auto mi = model.objects.front()->instances.front();

    BoundingBox bb_before = scaled(to_2d(arr2::instance_bounding_box(*mi)));

    TestType bed_case;
    auto bed_index = random_value<int>(bed_case.bed_idx_min, bed_case.bed_idx_max);

    bed_case.vbedh.assign_bed(arr2::VBedPlaceableMI{*mi}, bed_index);
    INFO("bed_index = " << bed_index);

    auto builder = arr2::SceneBuilder{}
                       .set_bed(bed_case.bed)
                       .set_model(model)
                       .set_arrange_settings(arr2::ArrangeSettings{}.set_distance_from_objects(0.))
                       .set_virtual_bed_handler(&bed_case.vbedh);

    arr2::Scene scene{std::move(builder)};

    using ArrItem = arr2::ArrangeItem;

    auto cvt = arr2::ArrangeableToItemConverter<ArrItem>::create(scene);

    ArrItem itm;
    scene.model().visit_arrangeable(model.objects.front()->instances.front()->id(),
                                    [&cvt, &itm](const arr2::Arrangeable &arrbl){
        itm = cvt->convert(arrbl);
    });

    BoundingBox bb_itm_before = arr2::fixed_bounding_box(itm);
    REQUIRE((bb_itm_before.min - bb_before.min).norm() < SCALED_EPSILON);
    REQUIRE((bb_itm_before.max - bb_before.max).norm() < SCALED_EPSILON);

    arr2::rotate(itm, rot);
    arr2::translate(itm, transl);
    arr2::set_bed_index(itm, arr2::PhysicalBedId);

    if (auto id = retrieve_id(itm)) {
        scene.model().visit_arrangeable(*id, [&itm](arr2::Arrangeable &arrbl) {
            arrbl.transform(unscaled(get_translation(itm)), get_rotation(itm));
        });
    }

    auto phys_tr = bed_case.vbedh.get_physical_bed_trafo(bed_index);
    auto outline = arr2::extract_convex_outline(*mi, phys_tr);
    BoundingBox bb_after = get_extents(outline);
    BoundingBox bb_itm_after = arr2::fixed_bounding_box(itm);
    REQUIRE((bb_itm_after.min - bb_after.min).norm() < 2 * SCALED_EPSILON);
    REQUIRE((bb_itm_after.max - bb_after.max).norm() < 2 * SCALED_EPSILON);
}

struct OutlineExtractorConvex {
    auto operator() (const Slic3r::ModelInstance *mi)
    {
        return Slic3r::arr2::extract_convex_outline(*mi);
    }
};

struct OutlineExtractorFull {
    auto operator() (const Slic3r::ModelInstance *mi)
    {
        return Slic3r::arr2::extract_full_outline(*mi);
    }
};

TEMPLATE_TEST_CASE("Outline extraction from ModelInstance",
                   "[arrange2][integration]",
                   OutlineExtractorConvex,
                   OutlineExtractorFull)
{
    using namespace Slic3r;
    using OutlineExtractor = TestType;

    Model model = get_example_model_with_20mm_cube();

    ModelInstance *mi = model.objects.front()->instances.front();
    auto matrix = mi->get_matrix();
    matrix.scale(Vec3d{random_value(0.1, 5.),
                       random_value(0.1, 5.),
                       random_value(0.1, 5.)});

    matrix.rotate(Eigen::AngleAxisd(random_value(-PI, PI), Vec3d::UnitZ()));

    matrix.translate(Vec3d{random_value(-100., 100.),
                           random_value(-100., 100.),
                           random_value(0., 100.)});

    mi->set_transformation(Geometry::Transformation{matrix});

    GIVEN("An empty ModelInstance without mesh")
    {
        const ModelInstance *mi = model.add_object()->add_instance();

        WHEN("the outline is generated") {
            auto outline = OutlineExtractor{}(mi);

            THEN ("the outline is empty") {
                REQUIRE(outline.empty());
            }
        }
    }

    GIVEN("A simple cube as outline") {
        const ModelInstance *mi = model.objects.front()->instances.front();

        WHEN("the outline is generated") {
            auto outline = OutlineExtractor{}(mi);

            THEN("the 2D ortho projection of the model bounding box is the "
                 "same as the outline's bb")
            {
                auto bb = unscaled(get_extents(outline));
                auto modelbb = to_2d(model.bounding_box_exact());

                REQUIRE((bb.min - modelbb.min).norm() < EPSILON);
                REQUIRE((bb.max - modelbb.max).norm() < EPSILON);
            }
        }
    }
}

template<class VBH>
auto create_vbed_handler(const Slic3r::BoundingBox &bedbb, coord_t gap)
{
    return VBH{};
}

template<>
auto create_vbed_handler<Slic3r::arr2::PhysicalOnlyVBedHandler>(const Slic3r::BoundingBox &bedbb, coord_t gap)
{
    return Slic3r::arr2::PhysicalOnlyVBedHandler{};
}

template<>
auto create_vbed_handler<Slic3r::arr2::XStriderVBedHandler>(const Slic3r::BoundingBox &bedbb, coord_t gap)
{
    return Slic3r::arr2::XStriderVBedHandler{bedbb, gap};
}

template<>
auto create_vbed_handler<Slic3r::arr2::YStriderVBedHandler>(const Slic3r::BoundingBox &bedbb, coord_t gap)
{
    return Slic3r::arr2::YStriderVBedHandler{bedbb, gap};
}

template<>
auto create_vbed_handler<Slic3r::arr2::GridStriderVBedHandler>(const Slic3r::BoundingBox &bedbb, coord_t gap)
{
    return Slic3r::arr2::GridStriderVBedHandler{bedbb, {gap, gap}};
}

TEMPLATE_TEST_CASE("Common virtual bed handlers",
                   "[arrange2][integration][vbeds]",
                   Slic3r::arr2::PhysicalOnlyVBedHandler,
                   Slic3r::arr2::XStriderVBedHandler,
                   Slic3r::arr2::YStriderVBedHandler,
                   Slic3r::arr2::GridStriderVBedHandler)
{
    using namespace Slic3r;
    using VBP = arr2::VBedPlaceableMI;

    Model model = get_example_model_with_20mm_cube();

    const auto bedsize = Vec2d{random_value(21., 500.), random_value(21., 500.)};

    const Vec2crd bed_displace = {random_value(scaled(-100.), scaled(100.)),
                                  random_value(scaled(-100.), scaled(100.))};

    const BoundingBox bedbb{bed_displace, scaled(bedsize) + bed_displace};

    INFO("Bed boundaries bedbb = { {" << unscaled(bedbb.min).transpose() << "}, {"
                                      << unscaled(bedbb.max).transpose() << "} }" );

    auto modelbb = model.bounding_box_exact();

    // Center the single instance within the model
    arr2::transform_instance(*model.objects.front()->instances.front(),
                             unscaled(bedbb.center()) - to_2d(modelbb.center()),
                             0.);

    const auto vbed_gap = GENERATE(0, random_value(1, scaled(100.)));

    INFO("vbed_gap = " << unscaled(vbed_gap));

    std::unique_ptr<arr2::VirtualBedHandler> vbedh = std::make_unique<TestType>(
        create_vbed_handler<TestType>(bedbb, vbed_gap));

    GIVEN("A ModelInstance on the physical bed")
    {
        ModelInstance *mi = model.objects.front()->instances.front();

        WHEN ("trying to move the item to an invalid bed index")
        {
            auto &mi_to_move = *model.objects.front()->add_instance(*mi);
            Transform3d mi_trafo_before = mi_to_move.get_matrix();
            bool was_accepted = vbedh->assign_bed(VBP{mi_to_move}, arr2::Unarranged);

            Transform3d mi_trafo_after = mi_to_move.get_matrix();

            THEN("the model instance should be unchanged") {
                REQUIRE(!was_accepted);
                REQUIRE(mi_trafo_before.isApprox(mi_trafo_after));
            }
        }
    }

    GIVEN("A ModelInstance being assigned to a virtual bed")
    {
        ModelInstance *mi = model.objects.front()->instances.front();

        auto bedidx_to = GENERATE(random_value(-1000, -1), 0, random_value(1, 1000));
        INFO("bed index = " << bedidx_to);

        auto &mi_to_move = *model.objects.front()->add_instance(*mi);

        // Move model instance to the given virtual bed
        bool was_accepted = vbedh->assign_bed(VBP{mi_to_move}, bedidx_to);

        WHEN ("querying the virtual bed index of this item")
        {
            int bedidx_on = vbedh->get_bed_index(VBP{mi_to_move});

            THEN("should actually be on that bed, or the assign should be discarded") {
                REQUIRE(((!was_accepted) || (bedidx_to == bedidx_on)));
            }

            THEN("assigning the same bed index again should produce the same result")
            {
                auto &mi_to_move_cpy = *model.objects.front()->add_instance(mi_to_move);
                bool was_accepted_rep = vbedh->assign_bed(VBP{mi_to_move_cpy}, bedidx_to);
                int bedidx_on_rep = vbedh->get_bed_index(VBP{mi_to_move_cpy});

                REQUIRE(was_accepted_rep == was_accepted);
                REQUIRE(((!was_accepted_rep) || (bedidx_to == bedidx_on_rep)));
            }
        }

        WHEN ("moving back to the physical bed")
        {
            auto &mi_back_to_phys = *model.objects.front()->add_instance(mi_to_move);
            bool moved_back_to_physical = vbedh->assign_bed(VBP{mi_back_to_phys}, arr2::PhysicalBedId);

            THEN("model instance should actually move back to the physical bed")
            {
                REQUIRE(moved_back_to_physical);
                int bedidx_mi2 = vbedh->get_bed_index(VBP{mi_back_to_phys});
                REQUIRE(bedidx_mi2 == 0);
            }

            THEN("the bounding box should be inside bed")
            {
                auto bbf = arr2::instance_bounding_box(mi_back_to_phys);
                auto bb = BoundingBox{scaled(to_2d(bbf))};
                INFO("bb = { {" << unscaled(bb.min).transpose() << "}, {"
                                << unscaled(bb.max).transpose() << "} }" );

                REQUIRE(bedbb.contains(bb));
            }
        }

        WHEN("extracting transformed model instance bounding box using the "
             "physical bed trafo")
        {
            int from_bed_idx = vbedh->get_bed_index(VBP{mi_to_move});
            auto physical_bed_trafo = vbedh->get_physical_bed_trafo(from_bed_idx);

            auto &mi_back_to_phys = *model.objects.front()->add_instance(mi_to_move);
            mi_back_to_phys.set_transformation(Geometry::Transformation{
                physical_bed_trafo * mi_back_to_phys.get_matrix()});

            auto bbf = arr2::instance_bounding_box(mi_back_to_phys);

            auto bb = BoundingBox{scaled(to_2d(bbf))};

            THEN("the bounding box should be inside bed")
            {
                INFO("bb = { {" << unscaled(bb.min).transpose() << "}, {"
                                << unscaled(bb.max).transpose() << "} }" );

                REQUIRE(bedbb.contains(bb));
            }

            THEN("the outline should be inside the physical bed")
            {
                auto outline = arr2::extract_convex_outline(mi_to_move,
                                                            physical_bed_trafo);
                auto bb = get_extents(outline);
                INFO("bb = { {" << bb.min.transpose() << "}, {"
                                << bb.max.transpose() << "} }" );

                REQUIRE(bedbb.contains(bb));
            }
        }
    }
}

TEST_CASE("Virtual bed handlers - StriderVBedHandler", "[arrange2][integration][vbeds]")
{
    using namespace Slic3r;
    using VBP = arr2::VBedPlaceableMI;

    Model model = get_example_model_with_20mm_cube();

    static const Vec2d bedsize{250., 210.};
    static const BoundingBox bedbb{{0, 0}, scaled(bedsize)};
    static const auto modelbb = model.bounding_box_exact();

    GIVEN("An instance of StriderVBedHandler with a stride of the bed width"
          " and random non-negative gap")
    {
        auto [instance_pos, instance_displace] = GENERATE(table<std::string, Vec2d>({
            {"start", unscaled(bedbb.min) - to_2d(modelbb.min) + Vec2d::Ones() * EPSILON},  // at the min edge of vbed
            {"middle", unscaled(bedbb.center()) - to_2d(modelbb.center())}, // at the center
            {"end", unscaled(bedbb.max) - to_2d(modelbb.max) - Vec2d::Ones() * EPSILON} // at the max edge of vbed
        }));

        // Center the single instance within the model
        arr2::transform_instance(*model.objects.front()->instances.front(),
                                 instance_displace,
                                 0.);

        INFO("Instance pos at " << instance_pos << " of bed");

        coord_t gap = GENERATE(0, random_value(1, scaled(100.)));

        INFO("Gap is " << unscaled(gap));

        arr2::XStriderVBedHandler vbh{bedbb, gap};

        WHEN("a model instance is on the Nth virtual bed (spatially)")
        {
            ModelInstance *mi = model.objects.front()->instances.front();
            auto &mi_to_move = *model.objects.front()->add_instance(*mi);

            auto bed_index = GENERATE(random_value(-1000, -1), 0, random_value(1, 1000));
            INFO("N is " << bed_index);

            double bed_disp = bed_index * unscaled(vbh.stride_scaled());
            arr2::transform_instance(mi_to_move, Vec2d{bed_disp, 0.}, 0.);

            THEN("the bed index of this model instance should be max(0, N)")
            {
                REQUIRE(vbh.get_bed_index(VBP{mi_to_move}) == bed_index);
            }

            THEN("the physical trafo should move the instance back to bed 0")
            {
                auto tr = vbh.get_physical_bed_trafo(bed_index);
                mi_to_move.set_transformation(Geometry::Transformation{tr * mi_to_move.get_matrix()});
                REQUIRE(vbh.get_bed_index(VBP{mi_to_move}) == 0);

                auto instbb = BoundingBox{scaled(to_2d(arr2::instance_bounding_box(mi_to_move)))};
                INFO("bedbb = { {" << bedbb.min.transpose() << "}, {" << bedbb.max.transpose() << "} }" );
                INFO("instbb = { {" << instbb.min.transpose() << "}, {" << instbb.max.transpose() << "} }" );

                REQUIRE(bedbb.contains(instbb));
            }
        }

        WHEN("a model instance is on the physical bed")
        {
            ModelInstance *mi = model.objects.front()->instances.front();
            auto &mi_to_move = *model.objects.front()->add_instance(*mi);

            THEN("assigning the model instance to the Nth bed will move it N*stride in the X axis")
            {
                auto bed_index = GENERATE(random_value(-1000, -1), 0, random_value(1, 1000));
                INFO("N is " << bed_index);

                if (vbh.assign_bed(VBP{mi_to_move}, bed_index))
                    REQUIRE(vbh.get_bed_index(VBP{mi_to_move}) == bed_index);
                else
                    REQUIRE(bed_index < 0);

                auto tr = vbh.get_physical_bed_trafo(bed_index);
                auto ref_pos = tr * Vec3d::Zero();

                auto displace = bed_index * (unscaled(vbh.stride_scaled()));
                REQUIRE(ref_pos.x() == Approx(-displace));

                auto ref_pos_mi = mi_to_move.get_matrix() * Vec3d::Zero();
                REQUIRE(ref_pos_mi.x() == Approx(instance_displace.x() + (bed_index >= 0) * displace));
            }
        }
    }

    GIVEN("An instance of StriderVBedHandler with a stride of the bed width"
          " and a 100mm gap")
    {
        coord_t gap = scaled(100.);

        arr2::XStriderVBedHandler vbh{bedbb, gap};

        WHEN("a model instance is within the gap on the Nth virtual bed")
        {
            ModelInstance *mi = model.objects.front()->instances.front();
            auto &mi_to_move = *model.objects.front()->add_instance(*mi);

            auto bed_index = GENERATE(random_value(-1000, -1), 0, random_value(1, 1000));
            INFO("N is " << bed_index);

            auto bed_disp = Vec2d{bed_index * unscaled(vbh.stride_scaled()), 0.};
            auto instbb_before = to_2d(arr2::instance_bounding_box(mi_to_move));

            auto transl_to_bed_end = Vec2d{bed_disp + unscaled(bedbb.max)
                                           - instbb_before.min + Vec2d::Ones() * EPSILON};

            arr2::transform_instance(mi_to_move,
                                     transl_to_bed_end + Vec2d{unscaled(gap / 2), 0.},
                                     0.);

            THEN("the model instance should reside on the Nth logical bed but "
                 "outside of the bed boundaries")
            {
                REQUIRE(vbh.get_bed_index(VBP{mi_to_move}) == bed_index);

                auto instbb = BoundingBox{scaled(to_2d(arr2::instance_bounding_box(mi_to_move)))};
                INFO("bedbb = { {" << bedbb.min.transpose() << "}, {" << bedbb.max.transpose() << "} }" );
                INFO("instbb = { {" << instbb.min.transpose() << "}, {" << instbb.max.transpose() << "} }" );

                REQUIRE(! bedbb.contains(instbb));
            }
        }
    }
}

TEMPLATE_TEST_CASE("Bed needs to be completely filled with 1cm cubes",
                   "[arrange2][integration][bedfilling]",
                   Slic3r::arr2::ArrangeItem)
{
    using namespace Slic3r;
    using ArrItem = TestType;

    std::string basepath = TEST_DATA_DIR PATH_SEPARATOR;

    DynamicPrintConfig cfg;
    cfg.load_from_ini(basepath + "default_fff.ini",
                      ForwardCompatibilitySubstitutionRule::Enable);
    cfg.set_key_value("bed_shape",
                      new ConfigOptionPoints(
                          {{0., 0.}, {100., 0.}, {100., 100.}, {0, 100.}}));

    Model m;

    ModelObject* new_object = m.add_object();
    new_object->name = "10mm_box";
    ModelInstance *instance = new_object->add_instance();
    TriangleMesh mesh = make_cube(10., 10., 10.);
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

        store_3mf("fillbed_10mm.3mf", &m, &cfg, false);

    arr2::ArrangeSettings settings;
    settings.values().d_obj = 0.;
    settings.values().d_bed = 0.;

    arr2::FixedSelection sel({{true}});

    arr2::BedConstraints constraints;
    constraints.insert({instance->id(), 0});

    arr2::Scene scene{arr2::SceneBuilder{}
                                 .set_model(m)
                                 .set_arrange_settings(settings)
                                 .set_selection(&sel)
                                 .set_bed_constraints(std::move(constraints))
                                 .set_bed(cfg, Point::new_scale(10, 10))};

    auto task = arr2::FillBedTask<ArrItem>::create(scene);
    auto result = task->process_native(arr2::DummyCtl{});
    result->apply_on(scene.model());

    store_3mf("fillbed_10mm_result.3mf", &m, &cfg, false);

    Points bedpts = get_bed_shape(cfg);
    arr2::ArrangeBed bed = arr2::to_arrange_bed(bedpts, Point::new_scale(10, 10));

    REQUIRE(bed.which() == 1); // Rectangle bed

    auto bedbb = unscaled(bounding_box(bed));
    auto bedbbsz = bedbb.size();

    REQUIRE(m.objects.size() == 1);
    REQUIRE(m.objects.front()->instances.size() ==
            bedbbsz.x() * bedbbsz.y() / 100);

    REQUIRE(task->unselected.empty());
    REQUIRE(result->to_add.size() + result->arranged_items.size() == arr2::model_instance_count(m));

    // All the existing items should be on the physical bed
    REQUIRE(std::all_of(result->arranged_items.begin(),
                        result->arranged_items.end(), [](auto &itm) {
                            return arr2::get_bed_index(itm) == 0;
                        }));

    REQUIRE(
        std::all_of(result->to_add.begin(), result->to_add.end(), [](auto &itm) {
            return arr2::get_bed_index(itm) == 0;
        }));
}

template<class It, class Fn>
static void foreach_combo(const Slic3r::Range<It> &range, const Fn &fn)
{
    std::vector<bool> pairs(range.size(), false);

    assert(range.size() >= 2);
    pairs[range.size() - 1] = true;
    pairs[range.size() - 2] = true;

    do {
        std::vector<typename std::iterator_traits<It>::value_type> items;
        for (size_t i = 0; i < pairs.size(); i++) {
            if (pairs[i]) {
                auto it = range.begin();
                std::advance(it, i);
                items.emplace_back(*it);
            }
        }
        fn (items[0], items[1]);
    } while (std::next_permutation(pairs.begin(), pairs.end()));
}

TEST_CASE("Testing minimum area bounding box rotation on simple cubes", "[arrange2][integration]")
{
    using namespace Slic3r;

    BoundingBox bb{Point::Zero(), scaled(Vec2d(10., 10.))};
    Polygon sh = arr2::to_rectangle(bb);

    auto prot = random_value(0., 2 * PI);
    sh.translate(Vec2crd{random_value<coord_t>(-scaled(10.), scaled(10.)),
                         random_value<coord_t>(-scaled(10.), scaled(10.))});
    sh.rotate(prot);

    INFO("box item is rotated by: " << prot << " rads");

    arr2::ArrangeItem itm{sh};
    arr2::rotate(itm, random_value(0., 2 * PI));

    double rot = arr2::get_min_area_bounding_box_rotation(itm);

    arr2::translate(itm,
                    Vec2crd{random_value<coord_t>(-scaled(10.), scaled(10.)),
                            random_value<coord_t>(-scaled(10.), scaled(10.))});
    arr2::rotate(itm, rot);

    auto itmbb = arr2::fixed_bounding_box(itm);
    REQUIRE(std::abs(itmbb.size().norm() - bb.size().norm()) <
            SCALED_EPSILON * SCALED_EPSILON);
}

template<class It>
bool is_collision_free(const Slic3r::Range<It> &item_range)
{
    using namespace Slic3r;

    bool collision_free = true;
    foreach_combo(item_range, [&collision_free](auto &itm1, auto &itm2) {
        auto outline1 = offset(arr2::fixed_outline(itm1), -scaled<float>(EPSILON));
        auto outline2 = offset(arr2::fixed_outline(itm2), -scaled<float>(EPSILON));

        auto inters = intersection(outline1, outline2);
        collision_free = collision_free && inters.empty();
    });

    return collision_free;
}

TEST_CASE("Testing a simple arrange on cubes", "[arrange2][integration]")
{
    using namespace Slic3r;

    Model model = get_example_model_with_random_cube_objects(size_t{10});

    arr2::ArrangeSettings settings;
    settings.set_rotation_enabled(true);

    auto bed = arr2::RectangleBed{scaled(250.), scaled(210.)};

    arr2::Scene scene{arr2::SceneBuilder{}
                          .set_model(model)
                          .set_arrange_settings(settings)
                          .set_bed(bed)};

    auto task = arr2::ArrangeTask<arr2::ArrangeItem>::create(scene);

    REQUIRE(task->printable.selected.size() == arr2::model_instance_count(model));

    auto result = task->process_native(arr2::DummyCtl{});

    REQUIRE(result);

    REQUIRE(result->items.size() == task->printable.selected.size());

    bool applied = result->apply_on(scene.model());

    REQUIRE(applied);

    REQUIRE(std::all_of(result->items.begin(),
                        result->items.end(),
                        [](auto &item) { return arr2::is_arranged(item); }));

    REQUIRE(std::all_of(task->printable.selected.begin(), task->printable.selected.end(),
                        [&bed](auto &item) { return bounding_box(bed).contains(arr2::envelope_bounding_box(item)); }));

    REQUIRE(std::all_of(task->unprintable.selected.begin(), task->unprintable.selected.end(),
                        [&bed](auto &item) { return bounding_box(bed).contains(arr2::envelope_bounding_box(item)); }));

    REQUIRE(is_collision_free(range(task->printable.selected)));
}

TEST_CASE("Testing arrangement involving virtual beds", "[arrange2][integration]")
{
    using namespace Slic3r;

    Model model = get_example_model_with_arranged_primitives();
    DynamicPrintConfig cfg;
    cfg.load_from_ini(std::string(TEST_DATA_DIR PATH_SEPARATOR) + "default_fff.ini",
                      ForwardCompatibilitySubstitutionRule::Enable);
    auto bed = arr2::to_arrange_bed(get_bed_shape(cfg), Point::new_scale(10, 10));
    auto bedbb = bounding_box(bed);
    auto bedsz = unscaled(bedbb.size());

    auto strategy = GENERATE(arr2::ArrangeSettingsView::asAuto,
                             arr2::ArrangeSettingsView::asPullToCenter);

    INFO ("Strategy = " << strategy);

    auto settings = arr2::ArrangeSettings{}
                        .set_distance_from_objects(0.)
                        .set_arrange_strategy(strategy);

    arr2::Scene scene{arr2::SceneBuilder{}
                          .set_model(model)
                          .set_arrange_settings(settings)
                          .set_bed(cfg, Point::new_scale(10, 10))};

    auto itm_conv = arr2::ArrangeableToItemConverter<arr2::ArrangeItem>::create(scene);

    auto task = arr2::ArrangeTask<arr2::ArrangeItem>::create(scene, *itm_conv);

    ModelObject* new_object = model.add_object();
    new_object->name = "big_cube";
    ModelInstance *bigcube_inst = new_object->add_instance();
    TriangleMesh mesh = make_cube(bedsz.x() - 5., bedsz.y() - 5., 20.);
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    {
        arr2::ArrangeItem bigitm;
        scene.model().visit_arrangeable(bigcube_inst->id(),
                                        [&bigitm, &itm_conv](
                                            const arr2::Arrangeable &arrbl) {
                                            bigitm = itm_conv->convert(arrbl);
                                        });

        task->printable.selected.emplace_back(std::move(bigitm));
    }

    REQUIRE(task->printable.selected.size() == arr2::model_instance_count(model));

    auto result = task->process_native(arr2::DummyCtl{});

    REQUIRE(result);

    REQUIRE(result->items.size() == task->printable.selected.size());

    REQUIRE(std::all_of(result->items.begin(),
                        std::prev(result->items.end()),
                        [](auto &item) { return arr2::get_bed_index(item) == 1; }));

    REQUIRE(arr2::get_bed_index(result->items.back()) == arr2::PhysicalBedId);

    bool applied = result->apply_on(scene.model());
    REQUIRE(applied);
    store_3mf("vbed_test_result.3mf", &model, &cfg, false);

    REQUIRE(std::all_of(task->printable.selected.begin(), task->printable.selected.end(),
                        [&bed](auto &item) { return bounding_box(bed).contains(arr2::envelope_bounding_box(item)); }));

    REQUIRE(is_collision_free(Range{task->printable.selected.begin(), std::prev(task->printable.selected.end())}));
}

bool settings_eq(const Slic3r::arr2::ArrangeSettingsView &v1,
                  const Slic3r::arr2::ArrangeSettingsView &v2)
{
    return v1.is_rotation_enabled() == v2.is_rotation_enabled() &&
           v1.get_arrange_strategy() == v2.get_arrange_strategy() &&
           v1.get_distance_from_bed() == Approx(v2.get_distance_from_bed()) &&
           v1.get_distance_from_objects() == Approx(v2.get_distance_from_objects()) &&
           v1.get_geometry_handling() == v2.get_geometry_handling() &&
           v1.get_xl_alignment() == v2.get_xl_alignment();
        ;
}

namespace Slic3r { namespace arr2 {

class MocWT: public ArrangeableWipeTowerBase {
public:
    using ArrangeableWipeTowerBase::ArrangeableWipeTowerBase;
};

class MocWTH : public WipeTowerHandler {
    std::function<bool(int)> m_sel_pred;
    ObjectID m_id;

public:
    MocWTH(const ObjectID &id) : m_id{id} {}

    void visit(std::function<void(Arrangeable &)> fn) override
    {
        MocWT wt{m_id, Polygon{}, 0, m_sel_pred};
        fn(wt);
    }
    void visit(std::function<void(const Arrangeable &)> fn) const override
    {
        MocWT wt{m_id, Polygon{}, 0, m_sel_pred};
        fn(wt);
    }
    void set_selection_predicate(std::function<bool(int)> pred) override
    {
        m_sel_pred = std::move(pred);
    }

    ObjectID get_id() const override {
        return m_id;
    }
};

}} // namespace Slic3r::arr2

TEST_CASE("Test SceneBuilder", "[arrange2][integration]")
{
    using namespace Slic3r;

    GIVEN("An empty SceneBuilder")
    {
        arr2::SceneBuilder bld;

        WHEN("building an ArrangeScene from it")
        {
            arr2::Scene scene{std::move(bld)};

            THEN("The scene should still be initialized consistently with empty model")
            {
                // This would segfault if model_wt isn't initialized properly
                REQUIRE(scene.model().arrangeable_count() == 0);
                REQUIRE(settings_eq(scene.settings(), arr2::ArrangeSettings{}));
                REQUIRE(scene.selected_ids().empty());
            }

            THEN("The associated bed should be an instance of InfiniteBed")
            {
                scene.visit_bed([](auto &bed){
                    REQUIRE(std::is_convertible_v<decltype(bed), arr2::InfiniteBed>);
                });
            }
        }

        WHEN("pushing random settings into the builder")
        {
            RandomArrangeSettings settings;
            auto bld2 = arr2::SceneBuilder{}.set_arrange_settings(&settings);
            arr2::Scene scene{std::move(bld)};

            THEN("settings of the resulting scene should be equal")
            {
                REQUIRE(settings_eq(scene.settings(), settings));
            }
        }
    }

    GIVEN("An existing instance of the class Model")
    {
        auto N = random_value(1, 20);
        Model model = get_example_model_with_random_cube_objects(N);
        INFO("model object count " << N);

        WHEN("a scene is built from a builder that holds a reference to an existing model")
        {
            arr2::Scene scene{arr2::SceneBuilder{}.set_model(&model)};

            THEN("the model of the constructed scene should have the same number of arrangeables") {
                REQUIRE(scene.model().arrangeable_count() == arr2::model_instance_count(model));
            }
        }
    }

    GIVEN("An instance of DynamicPrintConfig with rectangular bed")
    {
        std::string basepath = TEST_DATA_DIR PATH_SEPARATOR;

        DynamicPrintConfig cfg;
        cfg.load_from_ini(basepath + "default_fff.ini",
                          ForwardCompatibilitySubstitutionRule::Enable);

        WHEN("a scene is built with a bed initialized from this DynamicPrintConfig")
        {
            arr2::Scene scene(arr2::SceneBuilder{}.set_bed(cfg, Point::new_scale(10, 10)));

            auto bedbb = bounding_box(get_bed_shape(cfg));

            THEN("the bed should be a rectangular bed with the same dimensions as the bed points")
            {
                scene.visit_bed([&bedbb, &scene](auto &bed) {
                    constexpr bool is_rect = std::is_convertible_v<
                        decltype(bed), arr2::RectangleBed>;

                    REQUIRE(is_rect);

                    if constexpr (is_rect) {
                        bedbb.offset(scaled(scene.settings().get_distance_from_objects() / 2.));
                        REQUIRE(bedbb.size().x() == bed.width());
                        REQUIRE(bedbb.size().y() == bed.height());
                    }
                });
            }
        }
    }

    GIVEN("A wipe tower handler that uses the builder's selection mask")
    {
        arr2::SceneBuilder bld;
        Model mdl;
        bld.set_model(mdl);

        std::vector<AnyPtr<arr2::WipeTowerHandler>> handlers;
        handlers.push_back(std::make_unique<arr2::MocWTH>(wipe_tower_instance_id(0)));
        bld.set_wipe_tower_handlers(std::move(handlers));

        WHEN("the selection mask is initialized as a fallback default in the created scene")
        {
            arr2::Scene scene{std::move(bld)};

            THEN("the wipe tower should use the fallback selmask (created after set_wipe_tower)")
            {
                // Should be the wipe tower
                REQUIRE(scene.model().arrangeable_count() == 1);

                bool wt_selected = false;
                scene.model()
                    .visit_arrangeable(wipe_tower_instance_id(0),
                                       [&wt_selected](
                                           const arr2::Arrangeable &arrbl) {
                                           wt_selected = arrbl.is_selected();
                                       });

                REQUIRE(wt_selected);
            }
        }
    }
}

TEST_CASE("Testing duplicate function to really duplicate the whole Model",
          "[arrange2][integration]")
{
    using namespace Slic3r;

    Model model = get_example_model_with_arranged_primitives();

    store_3mf("dupl_example.3mf", &model, nullptr, false);

    size_t instcnt = arr2::model_instance_count(model);

    size_t copies_num = random_value(1, 10);

    INFO("Copies: " << copies_num);

    auto bed = arr2::InfiniteBed{};
    arr2::ArrangeSettings settings;
    settings.set_arrange_strategy(arr2::ArrangeSettings::asPullToCenter);
    arr2::DuplicableModel dup_model{&model, arr2::VirtualBedHandler::create(bed), bounding_box(bed)};

    arr2::Scene scene{arr2::BasicSceneBuilder{}
                          .set_arrangeable_model(&dup_model)
                          .set_arrange_settings(&settings)
                          .set_bed(bed)};

    auto task = arr2::MultiplySelectionTask<arr2::ArrangeItem>::create(scene, copies_num);
    auto result = task->process_native(arr2::DummyCtl{});
    bool applied = result->apply_on(scene.model());
    if (applied) {
        dup_model.apply_duplicates();
        store_3mf("dupl_example_result.3mf", &model, nullptr, false);
        REQUIRE(applied);
    }

    size_t new_instcnt = arr2::model_instance_count(model);

    REQUIRE(new_instcnt == (copies_num + 1) * instcnt);

    REQUIRE(std::all_of(result->arranged_items.begin(),
                        result->arranged_items.end(),
                        [](auto &item) { return arr2::is_arranged(item); }));

    REQUIRE(std::all_of(result->to_add.begin(),
                        result->to_add.end(),
                        [](auto &item) { return arr2::is_arranged(item); }));

    REQUIRE(std::all_of(task->selected.begin(), task->selected.end(),
                        [&bed](auto &item) { return bounding_box(bed).contains(arr2::envelope_bounding_box(item)); }));

    REQUIRE(is_collision_free(range(task->selected)));
}

