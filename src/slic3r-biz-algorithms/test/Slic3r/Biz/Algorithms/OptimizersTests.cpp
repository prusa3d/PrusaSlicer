#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/Optimize/BruteforceOptimizer.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp"

#include <numbers>

void check_opt_result(double score, double ref, double abs_err, double rel_err)
{
    double abs_diff = std::abs(score - ref);
    double rel_diff = std::abs(abs_diff / std::abs(ref));

    bool abs_reached = abs_diff < abs_err;
    bool rel_reached = rel_diff < rel_err;
    bool precision_reached = abs_reached || rel_reached;
    REQUIRE(precision_reached);
}

template<class Opt> void test_sin(Opt &&opt)
{
    using namespace Slic3r::Biz::Algorithms::Optimize;

    auto optfunc = [](const auto &in) {
        auto [phi] = in;

        return std::sin(phi);
    };

    auto init = initvals({std::numbers::pi});
    auto optbounds = bounds({ {0., 2 * std::numbers::pi}});

    Result result_min = opt.to_min().optimize(optfunc, init, optbounds);
    Result result_max = opt.to_max().optimize(optfunc, init, optbounds);

    check_opt_result(result_min.score, -1., 1e-2, 1e-4);
    check_opt_result(result_max.score,  1., 1e-2, 1e-4);
}

template<class Opt> void test_sphere_func(Opt &&opt)
{
    using namespace Slic3r::Biz::Algorithms::Optimize;

    Result result = opt.to_min().optimize([](const auto &in) {
        auto [x, y] = in;

        return x * x + y * y + 1.;
    }, initvals({.6, -0.2}), bounds({{-1., 1.}, {-1., 1.}}));

    check_opt_result(result.score, 1., 1e-2, 1e-4);
}

TEST_CASE("Test brute force optimzer for basic 1D and 2D functions", "[Opt]") {
    using namespace Slic3r::Biz::Algorithms::Optimize;

    Optimizer<AlgBruteForce> opt;

    test_sin(opt);
    test_sphere_func(opt);
}
