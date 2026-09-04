#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::Biz::Algorithms::Point {

bool has_consecutive_duplicate_points(const Domain::Points& points)
{
    return std::adjacent_find(points.begin(), points.end()) != points.end();
}

bool remove_consecutive_duplicate_points(Domain::Points& points, const bool check_first_and_last)
{
    if (points.empty())
        return false;

    auto last_it = std::unique(points.begin(), points.end());

    // If requested, check if the first and last points are duplicates and remove the last one if true.
    if (check_first_and_last && last_it != points.begin() && points.front() == *(last_it - 1)) {
        --last_it;
    }

    if (last_it == points.end())
        return false;

    points.erase(last_it, points.end());
    return true;
}

Domain::Points scaled(const Domain::Vec2ds& points)
{
    Domain::Points ret;
    ret.reserve(points.size());
    std::ranges::transform(points, std::back_inserter(ret), [](const auto& p) {
        return Scaling::scaled(p);
    });
    return ret;
}

Domain::Vec2ds unscaled(const Domain::Points& points)
{
    Domain::Vec2ds ret;
    ret.reserve(points.size());
    std::ranges::transform(points, std::back_inserter(ret), [](const auto& p) {
        return Scaling::unscaled<double>(p);
    });
    return ret;
}

Domain::Points collect_duplicates(Domain::Points pts /* Copy */)
{
    std::sort(pts.begin(), pts.end());
    Domain::Points duplicits;
    const Domain::Point *prev = &pts.front();
    for (size_t i = 1; i < pts.size(); ++i) {
        const Domain::Point *act = &pts[i];
        if (*prev == *act) {
            // duplicit point
            if (!duplicits.empty() && duplicits.back() == *act)
                continue; // only unique duplicits
            duplicits.push_back(*act);
        }
        prev = act;
    }
    return duplicits;
}
} // namespace Slic3r::Biz::Algorithms::Point
