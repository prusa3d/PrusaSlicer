#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdlib>

#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/libslic3r.h"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Catch;

static inline Slic3r::Point random_point(float LO=-50, float HI=50) 
{
    Vec2f pt = Vec2f(LO, LO) + (Vec2d(rand(), rand()) * (HI-LO) / RAND_MAX).cast<float>();
	return pt.cast<coord_t>();
}

// build a sample extrusion entity collection with random start and end points.
static Slic3r::ExtrusionPath random_path(size_t length = 20, float LO = -50, float HI = 50)
{
    ExtrusionPath t{ ExtrusionAttributes{ ExtrusionRole::Perimeter, ExtrusionFlow{ 1.0, 1.0, 1.0 } } };
    for (size_t j = 0; j < length; ++ j)
        t.polyline.append(random_point(LO, HI));
    return t;
}

static Slic3r::ExtrusionPaths random_paths(size_t count = 10, size_t length = 20, float LO = -50, float HI = 50)
{
    Slic3r::ExtrusionPaths p;
    for (size_t i = 0; i < count; ++ i)
        p.push_back(random_path(length, LO, HI));
    return p;
}

SCENARIO("ExtrusionPath", "[ExtrusionEntity]") {
    GIVEN("Simple path") {
        Slic3r::ExtrusionPath path{ { { 100, 100 }, { 200, 100 }, { 200, 200 } },
            ExtrusionAttributes{ ExtrusionRole::ExternalPerimeter, ExtrusionFlow{ 1., -1.f, -1.f } } };
        THEN("first point") {
            REQUIRE(path.first_point() == path.polyline.front());
        }
        THEN("cloned") {
            auto cloned = std::unique_ptr<ExtrusionEntity>(path.clone());
            REQUIRE(cloned->role() == path.role());
        }
    }
}

static ExtrusionPath new_extrusion_path(const Polyline &polyline, ExtrusionRole role, double mm3_per_mm)
{
    return { polyline, ExtrusionAttributes{ role, ExtrusionFlow{ mm3_per_mm, -1.f, -1.f } } };
}

SCENARIO("ExtrusionLoop", "[ExtrusionEntity]") 
{
    GIVEN("Square") {
        Polygon square { { 100, 100 }, { 200, 100 }, { 200, 200 }, { 100, 200 } };

        ExtrusionLoop loop;
        loop.paths.emplace_back(new_extrusion_path(square.split_at_first_point(), ExtrusionRole::ExternalPerimeter, 1.));
        THEN("polygon area") {
            REQUIRE(loop.polygon().area() == Approx(square.area()));
            REQUIRE(loop.area() == Approx(square.area()));
        }
        THEN("loop length") {
            REQUIRE(loop.length() == Approx(square.length()));
        }

        WHEN("cloned") {
            auto loop2 = std::unique_ptr<ExtrusionLoop>(dynamic_cast<ExtrusionLoop*>(loop.clone()));
            THEN("cloning worked") {
                REQUIRE(loop2 != nullptr);
            }
            THEN("loop contains one path") {
                REQUIRE(loop2->paths.size() == 1);
            }
            THEN("cloned role") {
                REQUIRE(loop2->paths.front().role() == ExtrusionRole::ExternalPerimeter);
            }
        }
        WHEN("cloned and split") {
            auto loop2 = std::unique_ptr<ExtrusionLoop>(dynamic_cast<ExtrusionLoop*>(loop.clone()));
            loop2->split_at_vertex(square.points[2]);
            THEN("splitting a single-path loop results in a single path") {
                REQUIRE(loop2->paths.size() == 1);
            }
            THEN("path has correct number of points") {
                REQUIRE(loop2->paths.front().size() == 5);
            }
            THEN("expected point order") {
                REQUIRE(loop2->paths.front().polyline[0] == square.points[2]);
                REQUIRE(loop2->paths.front().polyline[1] == square.points[3]);
                REQUIRE(loop2->paths.front().polyline[2] == square.points[0]);
                REQUIRE(loop2->paths.front().polyline[3] == square.points[1]);
                REQUIRE(loop2->paths.front().polyline[4] == square.points[2]);
            }
        }
    }

    GIVEN("Loop with two pieces") {
        Polyline polyline1 { { 100, 100 }, { 200, 100 }, { 200, 200 } };
        Polyline polyline2 { { 200, 200 }, { 100, 200 }, { 100, 100 } };
        ExtrusionLoop loop;
        loop.paths.emplace_back(new_extrusion_path(polyline1, ExtrusionRole::ExternalPerimeter, 1.));
        loop.paths.emplace_back(new_extrusion_path(polyline2, ExtrusionRole::OverhangPerimeter, 1.));

        THEN("area") {
            REQUIRE(loop.area() == Approx(loop.polygon().area()));
        }
        double tot_len = polyline1.length() + polyline2.length();
        THEN("length") {
            REQUIRE(loop.length() == Approx(tot_len));
        }

        WHEN("splitting at intermediate point") {
            auto loop2 = std::unique_ptr<ExtrusionLoop>(dynamic_cast<ExtrusionLoop*>(loop.clone()));
            loop2->split_at_vertex(polyline1.points[1]);
            THEN("length after splitting is unchanged") {
                REQUIRE(loop2->length() == Approx(tot_len));
            }
            THEN("loop contains three paths after splitting") {
                REQUIRE(loop2->paths.size() == 3);
            }
            THEN("expected starting point") {
                REQUIRE(loop2->paths.front().polyline.front() == polyline1.points[1]);
            }
            THEN("expected ending point") {
                REQUIRE(loop2->paths.back().polyline.back() == polyline1.points[1]);
            }
            THEN("paths have common point") {
                REQUIRE(loop2->paths.front().polyline.back() == loop2->paths[1].polyline.front());
                REQUIRE(loop2->paths[1].polyline.back() == loop2->paths[2].polyline.front());
            }
            THEN("expected order after splitting") {
                REQUIRE(loop2->paths.front().role() == ExtrusionRole::ExternalPerimeter);
                REQUIRE(loop2->paths[1].role() == ExtrusionRole::OverhangPerimeter);
                REQUIRE(loop2->paths[2].role() == ExtrusionRole::ExternalPerimeter);
            }
            THEN("path has correct number of points") {
                REQUIRE(loop2->paths.front().polyline.size() == 2);
                REQUIRE(loop2->paths[1].polyline.size() == 3);
                REQUIRE(loop2->paths[2].polyline.size() == 2);
            }
            THEN("clipped path has expected length") {
                double l = loop2->length();
                ExtrusionPaths paths;
                loop2->clip_end(3, &paths);
                double l2 = 0;
                for (const ExtrusionPath &p : paths)
                    l2 += p.length();
                REQUIRE(l2 == Approx(l - 3.));
            }
        }
        
        WHEN("splitting at endpoint") {
            auto loop2 = std::unique_ptr<ExtrusionLoop>(dynamic_cast<ExtrusionLoop*>(loop.clone()));
            loop2->split_at_vertex(polyline2.points.front());
            THEN("length after splitting is unchanged") {
                REQUIRE(loop2->length() == Approx(tot_len));
            }
            THEN("loop contains two paths after splitting") {
                REQUIRE(loop2->paths.size() == 2);
            }
            THEN("expected starting point") {
                REQUIRE(loop2->paths.front().polyline.front() == polyline2.points.front());
            }
            THEN("expected ending point") {
                REQUIRE(loop2->paths.back().polyline.back() == polyline2.points.front());
            }
            THEN("paths have common point") {
                REQUIRE(loop2->paths.front().polyline.back() == loop2->paths[1].polyline.front());
                REQUIRE(loop2->paths[1].polyline.back() == loop2->paths.front().polyline.front());
            }
            THEN("expected order after splitting") {
                REQUIRE(loop2->paths.front().role() == ExtrusionRole::OverhangPerimeter);
                REQUIRE(loop2->paths[1].role() == ExtrusionRole::ExternalPerimeter);
            }
            THEN("path has correct number of points") {
                REQUIRE(loop2->paths.front().polyline.size() == 3);
                REQUIRE(loop2->paths[1].polyline.size() == 3);
            }
        }
        
        WHEN("splitting at an edge") {
            Point point(250, 150);
            auto loop2 = std::unique_ptr<ExtrusionLoop>(dynamic_cast<ExtrusionLoop*>(loop.clone()));
            loop2->split_at(point, false, 0);
            THEN("length after splitting is unchanged") {
                REQUIRE(loop2->length() == Approx(tot_len));
            }
            Point expected_start_point(200, 150);
            THEN("expected starting point") {
                REQUIRE(loop2->paths.front().polyline.front() == expected_start_point);
            }
            THEN("expected ending point") {
                REQUIRE(loop2->paths.back().polyline.back() == expected_start_point);
            }
        }
    }

    GIVEN("Loop with four pieces") {
        Polyline polyline1 { { 59312736, 4821067 }, { 64321068, 4821067 }, { 64321068, 4821067 }, { 64321068, 9321068 }, { 59312736, 9321068 } };
        Polyline polyline2 { { 59312736, 9321068 }, { 9829401, 9321068 } };
        Polyline polyline3 { { 9829401, 9321068 }, { 4821067, 9321068 }, { 4821067, 4821067 }, { 9829401, 4821067 } };
        Polyline polyline4 { { 9829401, 4821067 }, { 59312736,4821067 } };
        ExtrusionLoop loop;
        loop.paths.emplace_back(new_extrusion_path(polyline1, ExtrusionRole::ExternalPerimeter, 1.));
        loop.paths.emplace_back(new_extrusion_path(polyline2, ExtrusionRole::OverhangPerimeter, 1.));
        loop.paths.emplace_back(new_extrusion_path(polyline3, ExtrusionRole::ExternalPerimeter, 1.));
        loop.paths.emplace_back(new_extrusion_path(polyline4, ExtrusionRole::OverhangPerimeter, 1.));
        double len = loop.length();
        THEN("area") {
            REQUIRE(loop.area() == Approx(loop.polygon().area()));
        }
        WHEN("splitting at vertex") {
            Point point(4821067, 9321068);
            if (! loop.split_at_vertex(point))
                loop.split_at(point, false, 0);
            THEN("total length is preserved after splitting") {
                REQUIRE(loop.length() == Approx(len));
            }
            THEN("order is correctly preserved after splitting") {
                REQUIRE(loop.paths.front().role() == ExtrusionRole::ExternalPerimeter);
                REQUIRE(loop.paths[1].role() == ExtrusionRole::OverhangPerimeter);
                REQUIRE(loop.paths[2].role() == ExtrusionRole::ExternalPerimeter);
                REQUIRE(loop.paths[3].role() == ExtrusionRole::OverhangPerimeter);
            }
        }
    }

    GIVEN("Some complex loop") {
        ExtrusionLoop loop;
        loop.paths.emplace_back(new_extrusion_path(
            Polyline { { 15896783, 15868739 }, { 24842049, 12117558 }, { 33853238, 15801279 }, { 37591780, 24780128 }, { 37591780, 24844970 }, 
                       { 33853231, 33825297 }, { 24842049, 37509013 }, { 15896798, 33757841 }, { 12211841, 24812544 }, { 15896783, 15868739 } },
            ExtrusionRole::ExternalPerimeter, 1.));
        THEN("area") {
            REQUIRE(loop.area() == Approx(loop.polygon().area()));
        }
        double len = loop.length();
        THEN("split_at() preserves total length") {
            loop.split_at({ 15896783, 15868739 }, false, 0);
            REQUIRE(loop.length() == Approx(len));
        }
    }
}

SCENARIO("ExtrusionEntityCollection: Basics", "[ExtrusionEntity]") 
{
    Polyline        polyline { { 100, 100 }, { 200, 100 }, { 200, 200 } };
    ExtrusionPath   path = new_extrusion_path(polyline, ExtrusionRole::ExternalPerimeter, 1.);
    ExtrusionLoop   loop;
    loop.paths.emplace_back(new_extrusion_path(Polygon(polyline.points).split_at_first_point(), ExtrusionRole::InternalInfill, 1.));
    ExtrusionEntityCollection collection;
    collection.append(path);
    THEN("no_sort is false by default") {
        REQUIRE(! collection.no_sort);
    }
    collection.append(collection);
    THEN("append ExtrusionEntityCollection") {
        REQUIRE(collection.entities.size() == 2);
    }
    collection.append(path);
    THEN("append ExtrusionPath") {
        REQUIRE(collection.entities.size() == 3);
    }
    collection.append(loop);
    THEN("append ExtrusionLoop") {
        REQUIRE(collection.entities.size() == 4);
    }
    THEN("appended collection was duplicated") {
        REQUIRE(dynamic_cast<ExtrusionEntityCollection*>(collection.entities[1])->entities.size() == 1);
    }
    WHEN("cloned") {
        auto coll2 = std::unique_ptr<ExtrusionEntityCollection>(dynamic_cast<ExtrusionEntityCollection*>(collection.clone()));
        THEN("expected no_sort value") {
            assert(! coll2->no_sort);
        }
        coll2->no_sort = true;
        THEN("no_sort is kept after clone") {
            auto coll3 = std::unique_ptr<ExtrusionEntityCollection>(dynamic_cast<ExtrusionEntityCollection*>(coll2->clone()));
            assert(coll3->no_sort);
        }
    }
}

SCENARIO("ExtrusionEntityCollection: Polygon flattening", "[ExtrusionEntity]") 
{
    srand(0xDEADBEEF); // consistent seed for test reproducibility.

    // Generate one specific random path set and save it for later comparison
    Slic3r::ExtrusionPaths nosort_path_set = random_paths();

    Slic3r::ExtrusionEntityCollection sub_nosort;
    sub_nosort.append(nosort_path_set);
    sub_nosort.no_sort = true;

    Slic3r::ExtrusionEntityCollection sub_sort;
    sub_sort.no_sort = false;
    sub_sort.append(random_paths());

    GIVEN("A Extrusion Entity Collection with a child that has one child that is marked as no-sort") {
        Slic3r::ExtrusionEntityCollection sample;
        Slic3r::ExtrusionEntityCollection output;

        sample.append(sub_sort);
        sample.append(sub_nosort);
        sample.append(sub_sort);

        WHEN("The EEC is flattened with default options (preserve_order=false)") {
			output = sample.flatten();
            THEN("The output EEC contains no Extrusion Entity Collections") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 0);
            }
        }
        WHEN("The EEC is flattened with preservation (preserve_order=true)") {
			output = sample.flatten(true);
            THEN("The output EECs contains one EEC.") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 1);
            }
            AND_THEN("The ordered EEC contains the same order of elements than the original") {
                // find the entity in the collection
                for (auto e : output.entities)
                    if (e->is_collection()) {
                        ExtrusionEntityCollection *temp = dynamic_cast<ExtrusionEntityCollection*>(e);
                        // check each Extrusion path against nosort_path_set to see if the first and last match the same
                        CHECK(nosort_path_set.size() == temp->entities.size());
                        for (size_t i = 0; i < nosort_path_set.size(); ++ i) {
                            CHECK(temp->entities[i]->first_point() == nosort_path_set[i].first_point());
                            CHECK(temp->entities[i]->last_point() == nosort_path_set[i].last_point());
                        }
                    }
            }
        }
    }
}

TEST_CASE("ExtrusionEntityCollection: Chained path", "[ExtrusionEntity]") {
    struct Test {
        Polylines unchained;
        Polylines chained;
        Point     initial_point;
    };
    std::vector<Test> tests { 
        {
            { 
                { {0,15}, {0,18}, {0,20} },
                { {0,10}, {0,8}, {0,5} }
            },
            {
                { {0,20}, {0,18}, {0,15} },
                { {0,10}, {0,8}, {0,5} }
            },
            { 0, 30 }
        },
        {
            { 
                { {4,0}, {10,0}, {15,0} },
                { {10,5}, {15,5}, {20,5} }
            },
            {
                { {20,5}, {15,5}, {10,5} },
                { {15,0}, {10,0}, {4,0} }
            },
            { 30, 0 }
        },
        {
            { 
                { {15,0}, {10,0}, {4,0} },
                { {10,5}, {15,5}, {20,5} }
            },
            {
                { {20,5}, {15,5}, {10,5} },
                { {15,0}, {10,0}, {4,0} }
            },
            { 30, 0 }
        },
    };
    for (const Test &test : tests) {
        Polylines chained = chain_polylines(test.unchained, &test.initial_point);
        REQUIRE(chained == test.chained);
        ExtrusionEntityCollection unchained_extrusions;
        extrusion_entities_append_paths(unchained_extrusions.entities, test.unchained,
            ExtrusionAttributes{ ExtrusionRole::InternalInfill, ExtrusionFlow{ 0., 0.4f, 0.3f } });
        THEN("Chaining works") {
            ExtrusionEntityReferences chained_extrusions = chain_extrusion_references(unchained_extrusions, &test.initial_point);
            REQUIRE(chained_extrusions.size() == test.chained.size());
            for (size_t i = 0; i < chained_extrusions.size(); ++ i) {
                const Points &p1 = test.chained[i].points;
                Points        p2 = chained_extrusions[i].cast<ExtrusionPath>()->polyline.points;
                if (chained_extrusions[i].flipped())
                    std::reverse(p2.begin(), p2.end());
                REQUIRE(p1 == p2);
            }
        }
        THEN("Chaining produces no change with no_sort") {
            unchained_extrusions.no_sort = true;
            ExtrusionEntityReferences chained_extrusions = chain_extrusion_references(unchained_extrusions, &test.initial_point);
            REQUIRE(chained_extrusions.size() == test.unchained.size());
            for (size_t i = 0; i < chained_extrusions.size(); ++ i) {
                const Points &p1 = test.unchained[i].points;
                Points        p2 = chained_extrusions[i].cast<ExtrusionPath>()->polyline.points;
                if (chained_extrusions[i].flipped())
                    std::reverse(p2.begin(), p2.end());
                REQUIRE(p1 == p2);
            }
        }
    }
}

TEST_CASE("ExtrusionEntityCollection: Chained path with no explicit starting point", "[ExtrusionEntity]") {
    auto polylines = Polylines { { { 0, 15 }, {0, 18}, {0, 20} }, { { 0, 10 }, {0, 8}, {0, 5} } };
    auto target    = Polylines { { {0, 5}, {0, 8}, { 0, 10 } }, { { 0, 15 }, {0, 18}, {0, 20} } };
    auto chained   = chain_polylines(polylines);
    REQUIRE(chained == target);
}
