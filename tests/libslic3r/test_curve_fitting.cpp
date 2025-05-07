#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include <libslic3r/Geometry/Curves.hpp>
#include <libslic3r/Utils.hpp>
#include <libslic3r/SVG.hpp>

using namespace Catch;

TEST_CASE("Curves: cubic b spline fit test", "[Curves]") {
    using namespace Slic3r;
    using namespace Slic3r::Geometry;

    auto fx = [&](size_t index) {
        return float(index) / 200.0f;
    };

    auto fy = [&](size_t index) {
        return 1.0f;
    };

    std::vector<Vec<1, float>> observations { };
    std::vector<float> observation_points { };
    std::vector<float> weights { };
    for (size_t index = 0; index < 200; ++index) {
        observations.push_back(Vec<1, float> { fy(index) });
        observation_points.push_back(fx(index));
        weights.push_back(1);
    }

    Vec2f fmin { fx(0), fy(0) };
    Vec2f fmax { fx(200), fy(200) };

    auto bspline = fit_cubic_bspline(observations, observation_points, weights, 1);

    Approx ap(1.0f);
    ap.epsilon(0.1f);

    for (int p = 0; p < 200; ++p) {
        float fitted_val = bspline.get_fitted_value(fx(p))(0);
        float expected = fy(p);

        REQUIRE(fitted_val == ap(expected));

    }
}

TEST_CASE("Curves: quadratic f cubic b spline fit test", "[Curves]") {
    using namespace Slic3r;
    using namespace Slic3r::Geometry;

    auto fx = [&](size_t index) {
        return float(index) / 100.0f;
    };

    auto fy = [&](size_t index) {
        return (fx(index) - 1) * (fx(index) - 1);
    };

    std::vector<Vec<1, float>> observations { };
    std::vector<float> observation_points { };
    std::vector<float> weights { };
    for (size_t index = 0; index < 200; ++index) {
        observations.push_back(Vec<1, float> { fy(index) });
        observation_points.push_back(fx(index));
        weights.push_back(1);
    }

    Vec2f fmin { fx(0), fy(0) };
    Vec2f fmax { fx(200), fy(200) };

    auto bspline = fit_cubic_bspline(observations, observation_points, weights, 10);

    for (int p = 0; p < 200; ++p) {
        float fitted_val = bspline.get_fitted_value(fx(p))(0);
        float expected = fy(p);

        auto check = [](float a, float b) {
            return abs(a - b) < 0.2f;
        };
        //Note: checking is problematic, splines will not perfectly align
        REQUIRE(check(fitted_val, expected));

    }
}

TEST_CASE("Curves: polynomial fit test", "[Curves]") {
    using namespace Slic3r;
    using namespace Slic3r::Geometry;

    auto fx = [&](size_t index) {
        return float(index) / 100.0f;
    };

    auto fy = [&](size_t index) {
        return (fx(index) - 1) * (fx(index) - 1);
    };

    std::vector<Vec<1, float>> observations { };
    std::vector<float> observation_points { };
    std::vector<float> weights { };
    for (size_t index = 0; index < 200; ++index) {
        observations.push_back(Vec<1, float> { fy(index) });
        observation_points.push_back(fx(index));
        weights.push_back(1);
    }

    Vec2f fmin { fx(0), fy(0) };
    Vec2f fmax { fx(200), fy(200) };

    Approx ap(1.0f);
    ap.epsilon(0.1f);

    auto poly = fit_polynomial(observations, observation_points, weights, 2);

    REQUIRE(poly.coefficients(0, 0) == ap(1));
    REQUIRE(poly.coefficients(0, 1) == ap(-2));
    REQUIRE(poly.coefficients(0, 2) == ap(1));
}

