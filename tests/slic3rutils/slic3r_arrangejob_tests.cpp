#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_utils.hpp"

#include <random>

#include "slic3r/GUI/Jobs/UIThreadWorker.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"

#include "slic3r/GUI/Jobs/ArrangeJob2.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "libslic3r/Format/3mf.hpp"

using Catch::Approx;

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

TEMPLATE_TEST_CASE("Arranging empty bed should do nothing",
                   "[arrangejob][fillbedjob]",
                   Slic3r::GUI::ArrangeJob2,
                   Slic3r::GUI::FillBedJob2)
{
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    using JobType = TestType;

    Model m;

    UIThreadWorker w;
    RandomArrangeSettings settings;

    w.push(std::make_unique<JobType>(arr2::Scene{
        arr2::SceneBuilder{}.set_model(m).set_arrange_settings(&settings)}));

    w.process_events();

    REQUIRE(m.objects.empty());
}

static void center_first_instance(Slic3r::ModelObject        *mo,
                                  const Slic3r::BoundingBox &bedbb)
{
    using namespace Slic3r;

    Vec2d d = unscaled(bedbb).center() -
              to_2d(mo->instance_bounding_box(0).center());
    auto tr = mo->instances.front()->get_transformation().get_matrix();
    tr.translate(to_3d(d, 0.));
    mo->instances.front()->set_transformation(Geometry::Transformation(tr));
}

TEST_CASE("Basic arrange with cube", "[arrangejob]") {
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    std::string basepath = TEST_DATA_DIR PATH_SEPARATOR;

    DynamicPrintConfig cfg;
    cfg.load_from_ini(basepath + "default_fff.ini",
                      ForwardCompatibilitySubstitutionRule::Enable);
    Model m = FileReader::load_model(basepath + "20mm_cube.obj");

    UIThreadWorker w;
    arr2::ArrangeSettings settings;

    Points bedpts = get_bed_shape(cfg);
    arr2::ArrangeBed bed = arr2::to_arrange_bed(bedpts, Vec2crd{0, 0});

    SECTION("Single cube needs to be centered") {
        w.push(std::make_unique<ArrangeJob2>(arr2::Scene{
            arr2::SceneBuilder{}
                .set_model(m)
                .set_arrange_settings(&settings)
                .set_bed(cfg, Vec2crd{0, 0})}));

        w.process_events();

        REQUIRE(m.objects.size() == 1);
        REQUIRE(m.objects.front()->instances.size() == 1);

        Vec3d c3 = m.objects.front()->bounding_box_exact().center();
        Point c{scaled(c3.x()), scaled(c3.y())};

        REQUIRE(c == bounding_box(bed).center());
    }

    SECTION("Selected cube needs to go beside existing") {
        REQUIRE(m.objects.size() == 1);

        ModelObject *mo = m.objects.front();

        // Center the first instance within the bed
        center_first_instance(mo, bounding_box(bed));

        m.objects.front()->add_instance();

        REQUIRE(m.objects.front()->instances.size() == 2);

        arr2::FixedSelection sel({ {false, true} });
        arr2::Scene   scene{arr2::SceneBuilder{}
                                     .set_model(m)
                                     .set_arrange_settings(&settings)
                                     .set_bed(cfg, Vec2crd{0, 0})
                                     .set_selection(&sel)};

        w.push(std::make_unique<ArrangeJob2>(std::move(scene)));
        w.process_events();

        auto bb0 = m.objects.front()->instance_bounding_box(0);
        auto bb1 = m.objects.front()->instance_bounding_box(1);

        REQUIRE(!bb0.contains(bb1));

        bb0.merge(bb1);
        Vec2d sz = to_2d(bb0.size());
        if (sz.x() > sz.y())
            std::swap(sz.x(), sz.y());

        double d_obj = settings.get_distance_from_objects();
        REQUIRE(sz.y() == Approx(2. * bb1.size().y() + d_obj));
    }

    SECTION("Selected cube (different object), needs to go beside existing") {
        REQUIRE(m.objects.size() == 1);

        ModelObject *mo = m.objects.front();

        // Center the first instance within the bed
        center_first_instance(mo, bounding_box(bed));

        ModelObject *mosel = m.add_object(*m.objects.front());

        arr2::FixedSelection sel({ {false}, {true} });
        arr2::Scene   scene{arr2::SceneBuilder{}
                                     .set_model(m)
                                     .set_arrange_settings(&settings)
                                     .set_bed(cfg, Vec2crd{0, 0})
                                     .set_selection(&sel)};

        w.push(std::make_unique<ArrangeJob2>(std::move(scene)));
        w.process_events();

        auto bb0 = mo->instance_bounding_box(0);
        auto bb1 = mosel->instance_bounding_box(0);

        REQUIRE(!bb0.contains(bb1));

        bb0.merge(bb1);
        Vec2d sz = to_2d(bb0.size());
        if (sz.x() > sz.y())
            std::swap(sz.x(), sz.y());

        double d_obj = settings.get_distance_from_objects();
        REQUIRE(sz.y() == Approx(2. * bb1.size().y() + d_obj));
    }

    SECTION("Four cubes needs to touch each other after arrange") {
        ModelObject *mo = m.objects.front();
        mo->add_instance();
        mo->add_instance();
        mo->add_instance();

        auto bedbb = unscaled<double>(bounding_box(bed));
        ModelInstance *mi = mo->instances[0];

        Vec2d d = bedbb.min - to_2d(mo->instance_bounding_box(0).center());
        auto tr = mi->get_transformation().get_matrix();
        tr.translate(to_3d(d, 0.));
        mi->set_transformation(Geometry::Transformation(tr));

        mi = mo->instances[1];
        d = Vec2d(bedbb.min.x(), bedbb.max.y()) -
            to_2d(mo->instance_bounding_box(1).center());
        tr = mi->get_transformation().get_matrix();
        tr.translate(to_3d(d, 0.));
        mi->set_transformation(Geometry::Transformation(tr));

        mi = mo->instances[2];
        d = bedbb.max - to_2d(mo->instance_bounding_box(2).center());
        tr = mi->get_transformation().get_matrix();
        tr.translate(to_3d(d, 0.));
        mi->set_transformation(Geometry::Transformation(tr));

        mi = mo->instances[3];
        d  = Vec2d(bedbb.max.x(), bedbb.min.y()) -
            to_2d(mo->instance_bounding_box(3).center());
        tr = mi->get_transformation().get_matrix();
        tr.translate(to_3d(d, 0.));
        mi->set_transformation(Geometry::Transformation(tr));

        arr2::Scene scene{arr2::SceneBuilder{}
                                     .set_model(m)
                                     .set_arrange_settings(&settings)
                                     .set_bed(cfg, Point::new_scale(10, 10))};

        w.push(std::make_unique<ArrangeJob2>(std::move(scene)));
        w.process_events();

        auto pilebb = m.objects.front()->bounding_box_exact();
        Vec3d c3 = pilebb.center();
        Point c{scaled(c3.x()), scaled(c3.y())};

        REQUIRE(c == bounding_box(bed).center());

        float d_obj = settings.get_distance_from_objects();
        REQUIRE(pilebb.size().x() == Approx(2. * 20. + d_obj));
        REQUIRE(pilebb.size().y() == Approx(2. * 20. + d_obj));
    }
}

struct DummyProgress: Slic3r::ProgressIndicator {
    int range = 100;
    int pr = 0;
    std::string statustxt;
    void set_range(int r) override { range = r; }
    void set_cancel_callback(CancelFn = CancelFn()) override {}
    void set_progress(int p) override { pr = p; }
    void set_status_text(const char *txt) override { statustxt = txt; }
    int  get_range() const override { return range; }
};

TEST_CASE("Test for modifying model during arrangement", "[arrangejob][fillbedjob]")
{
    using namespace Slic3r;
    using namespace Slic3r::GUI;

    std::string basepath = TEST_DATA_DIR PATH_SEPARATOR;

    DynamicPrintConfig cfg;
    cfg.load_from_ini(basepath + "default_fff.ini",
                      ForwardCompatibilitySubstitutionRule::Enable);

    Model m;

    ModelObject* new_object = m.add_object();
    new_object->name = "20mm_cyl";
    new_object->add_instance();
    TriangleMesh mesh = make_cylinder(10., 10.);
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = new_object->name;

    Points bedpts = get_bed_shape(cfg);
    arr2::ArrangeBed bed = arr2::to_arrange_bed(bedpts, Vec2crd{0, 0});

    BoostThreadWorker w(std::make_unique<DummyProgress>());
    RandomArrangeSettings settings;

    SECTION("Remove 10 cylinder instances during arrange") {
        for (size_t i = 1; i < 10; ++i)
            new_object->add_instance();

        arr2::Scene scene{arr2::SceneBuilder{}
                                     .set_model(m)
                                     .set_arrange_settings(&settings)
                                     .set_bed(cfg, Vec2crd{0, 0})};

        ArrangeJob2::Callbacks cbs;
        cbs.on_prepared = [&m] (auto &) {
            m.clear_objects();
        };

        w.push(std::make_unique<ArrangeJob2>(std::move(scene), cbs));
        w.wait_for_current_job();

        REQUIRE(m.objects.empty());
    }
}

//TEST_CASE("Logical bed needs to be used when physical bed is full",
//          "[arrangejob][fillbedjob]")
//{
//    using namespace Slic3r;
//    using namespace Slic3r::GUI;

//    std::string basepath = TEST_DATA_DIR PATH_SEPARATOR;

//    DynamicPrintConfig cfg;
//    cfg.load_from_ini(basepath + "default_fff.ini",
//                      ForwardCompatibilitySubstitutionRule::Enable);

//    Model m;

//    ModelObject* new_object = m.add_object();
//    new_object->name = "bigbox";
//    new_object->add_instance();
//    TriangleMesh mesh = make_cube(200., 200., 10.);
//    ModelVolume* new_volume = new_object->add_volume(mesh);
//    new_volume->name = new_object->name;

//    Points bedpts = get_bed_shape(cfg);
//    arr2::ArrangeBed bed = arr2::to_arrange_bed(bedpts);
//    auto bedbb = bounding_box(bed);

//    center_first_instance(new_object, bedbb);

//    new_object = m.add_object();
//    new_object->name = "40x20mm_box";
//    new_object->add_instance();
//    mesh = make_cube(50., 50., 50.);
//    new_volume = new_object->add_volume(mesh);
//    new_volume->name = new_object->name;

//    UIThreadWorker w(std::make_unique<DummyProgress>());
//    arr2::ArrangeSettings settings;

//    SECTION("Single cube needs to be on first logical bed") {
//        {
//            arr2::Scene scene{&m, &settings, &cfg};

//            w.push(std::make_unique<ArrangeJob2>(std::move(scene)));
//            w.process_events();
//        }

//        store_3mf("logicalbed_10mm.3mf", &m, &cfg, false);

//        REQUIRE(m.objects.size() == 2);

//        Vec3d c3 = m.objects[1]->bounding_box_exact().center();
//        Point result_center{scaled(c3.x()), scaled(c3.y())};

//        auto bedidx_ojb1 = scene.virtual_bed_handler().get_bed_index(m.objects[1]->instances[0]);
//        REQUIRE(bedidx_ojb1 == 1);
//    }
//}

