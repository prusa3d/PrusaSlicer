#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include "test_data.hpp"

#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamAligned.hpp"
#include "libslic3r/GCode/SeamRear.hpp"
#include "libslic3r/GCode/SeamRandom.hpp"

TEST_CASE_METHOD(Slic3r::Test::SeamsFixture, "Seam benchmarks", "[Seams][.Benchmarks]") {
    BENCHMARK_ADVANCED("Create extrusions benchy")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] { return Slic3r::Seams::Geometry::get_extrusions(print_object->layers()); });
    };

    using namespace Slic3r::Seams;

    BENCHMARK_ADVANCED("Create shells benchy")(Catch::Benchmark::Chronometer meter) {
        std::vector<Perimeters::LayerPerimeters> inputs;
        inputs.reserve(meter.runs());
        std::generate_n(std::back_inserter(inputs), meter.runs(), [&]() {
            return Slic3r::Seams::Perimeters::create_perimeters(
                projected, layer_infos, painting, params.perimeter
            );
        });
        meter.measure([&](const int i) {
            return Shells::create_shells(std::move(inputs[i]), params.max_distance);
        });
    };


    BENCHMARK_ADVANCED("Get layer infos benchy")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            return Perimeters::get_layer_infos(
                print_object->layers(), params.perimeter.elephant_foot_compensation
            );
        });
    };

    BENCHMARK_ADVANCED("Create perimeters benchy")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            return Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter);
        });
    };

    BENCHMARK_ADVANCED("Generate aligned seam benchy")(Catch::Benchmark::Chronometer meter) {
        std::vector<Shells::Shells<>> inputs;
        inputs.reserve(meter.runs());
        std::generate_n(std::back_inserter(inputs), meter.runs(), [&]() {
            Slic3r::Seams::Perimeters::LayerPerimeters perimeters{
                Slic3r::Seams::Perimeters::create_perimeters(
                    projected, layer_infos, painting, params.perimeter
                )};
            return Shells::create_shells(
                std::move(perimeters), params.max_distance
            );
        });
        meter.measure([&](const int i) {
            return Aligned::get_object_seams(
                std::move(inputs[i]), visibility_calculator, params.aligned
            );
        });
    };

    BENCHMARK_ADVANCED("Visibility constructor")(Catch::Benchmark::Chronometer meter) {
        using Visibility = Slic3r::ModelInfo::Visibility;
        std::vector<Catch::Benchmark::storage_for<Visibility>> storage(meter.runs());
        meter.measure([&](const int i) {
            storage[i].construct(transformation, volumes, params.visibility, []() {});
        });
    };

    BENCHMARK_ADVANCED("Generate rear seam benchy")(Catch::Benchmark::Chronometer meter) {
        std::vector<Perimeters::LayerPerimeters> inputs;
        inputs.reserve(meter.runs());
        std::generate_n(std::back_inserter(inputs), meter.runs(), [&]() {
            return Slic3r::Seams::Perimeters::create_perimeters(
                projected, layer_infos, painting, params.perimeter
            );
        });
        meter.measure([&](const int i) {
            return Rear::get_object_seams(std::move(inputs[i]), params.rear_tolerance, params.rear_y_offset);
        });
    };

    BENCHMARK_ADVANCED("Generate random seam benchy")(Catch::Benchmark::Chronometer meter) {
        std::vector<Perimeters::LayerPerimeters> inputs;
        inputs.reserve(meter.runs());
        std::generate_n(std::back_inserter(inputs), meter.runs(), [&]() {
            return Slic3r::Seams::Perimeters::create_perimeters(
                projected, layer_infos, painting, params.perimeter
            );
        });
        meter.measure([&](const int i) {
            return Random::get_object_seams(std::move(inputs[i]), params.random_seed);
        });
    };

    Placer placer;
    BENCHMARK_ADVANCED("Init seam placer aligned")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            return placer.init(print->objects(), params, [](){});
        });
    };

    SECTION("Place seam"){
        using namespace Slic3r;
        Placer placer;
        placer.init(print->objects(), params, [](){});
        std::vector<std::tuple<const Layer*, const ExtrusionLoop*, const PrintRegion *>> loops;

        const PrintObject* object{print->objects().front()};
        for (const Layer* layer :object->layers()) {
            for (const LayerSlice& lslice : layer->lslices_ex) {
                for (const LayerIsland &island : lslice.islands) {
                    const LayerRegion &layer_region = *layer->get_region(island.perimeters.region());
                    const PrintRegion &region = print->get_print_region(layer_region.region().print_region_id());
                    for (uint32_t perimeter_id : island.perimeters) {
                        const auto *entity_collection{static_cast<const ExtrusionEntityCollection*>(layer_region.perimeters().entities[perimeter_id])};
                        if (entity_collection != nullptr) {
                            for (const ExtrusionEntity *entity : *entity_collection) {
                                const auto loop{static_cast<const ExtrusionLoop*>(entity)};
                                if (loop == nullptr) {
                                    continue;
                                }
                                loops.emplace_back(layer, loop, &region);
                            }
                        }
                    }
                }
            }
        }
        BENCHMARK_ADVANCED("Place seam benchy")(Catch::Benchmark::Chronometer meter) {
            meter.measure([&] {
                for (const auto &[layer, loop, region] : loops) {
                    placer.place_seam(layer, region, *loop, false, {0, 0});
                }
            });
        };
    }
}
