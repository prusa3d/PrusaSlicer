#include "Slic3r/Biz/Arrange/Packer.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"
#include "Slic3r/Biz/Arrange/EdgeCache.hpp"

namespace Slic3r::Biz::Arrange {

namespace {
struct CornerResult
{
    size_t contour_id;
    Algorithms::Optimize::Result<1> oresult;
};
} // namespace

double Packer::pick_best_spot_on_nfp(ArrangeItem& item, const Domain::ExPolygons& nfp, const IBed& bed)
{
    namespace execution = Algorithms::Execution;
    execution::ExecutionTBB ex_policy{};

    auto score              = -std::numeric_limits<double>::infinity();
    Domain::Vec2crd orig_tr = item.get_translation();
    Domain::Vec2crd translation{0, 0};
    Domain::Vec2crd ref_v = item.movable_shape().reference_vertex();

    std::vector<EdgeCache> edge_caches;
    std::vector<std::vector<ContourLocation>> sample_sets;
    edge_caches.reserve(nfp.size());
    sample_sets.reserve(nfp.size());

    for (const Domain::ExPolygon& expoly : nfp) {
        edge_caches.emplace_back(EdgeCache{&expoly});
        edge_caches.back().sample_contour(accuracy, sample_sets.emplace_back());
    }

    auto nthreads = execution::max_concurrency(ex_policy);

    std::vector<CornerResult> gresults(edge_caches.size());

    auto resultcmp = [](auto& a, auto& b) {
        return a.oresult.score < b.oresult.score;
    };

    execution::for_each(
        ex_policy,
        size_t(0),
        edge_caches.size(),
        [&](size_t edge_cache_idx) {
            auto& ec_contour = edge_caches[edge_cache_idx];
            auto& corners    = sample_sets[edge_cache_idx];
            std::vector<CornerResult> results(corners.size());

            auto cornerfn = [&](size_t i) {
                ContourLocation cr = corners[i];
                auto objfn         = [&](Algorithms::Optimize::Input<1>& in) {
                    Domain::Vec2crd p  = ec_contour.coords(ContourLocation{cr.contour_id, in[0]});
                    Domain::Vec2crd tr = p - ref_v;

                    return kernel->placement_fitness(item, tr);
                };

                // Assuming that solver is a lightweight object
                solver.to_max();
                auto oresult = solver.optimize(objfn, Algorithms::Optimize::initvals({cr.dist}), Algorithms::Optimize::bounds({{0., 1.}}));

                results[i] = CornerResult{cr.contour_id, oresult};
            };

            execution::for_each(ex_policy, size_t(0), results.size(), cornerfn, nthreads);

            auto it = std::max_element(results.begin(), results.end(), resultcmp);

            if (it != results.end())
                gresults[edge_cache_idx] = *it;
        },
        nthreads
    );

    auto it = std::max_element(gresults.begin(), gresults.end(), resultcmp);
    if (it != gresults.end()) {
        score             = it->oresult.score;
        size_t path_id    = std::distance(gresults.begin(), it);
        size_t contour_id = it->contour_id;
        double dist       = it->oresult.optimum[0];

        Domain::Vec2crd pos = edge_caches[path_id].coords(ContourLocation{contour_id, dist});
        Domain::Vec2crd tr  = pos - ref_v;

        item.set_translation(orig_tr + tr);
    }

    return score;
}

bool Packer::pack(
    const IBed& bed,
    ArrangeItem& item,
    const PackingContext& packing_context,
    std::span<ArrangeItem> remaining_items
)
{
    // The kernel might pack the item immediately
    bool packed = kernel->on_start_packing(item, bed, packing_context, remaining_items);

    double orig_rot          = item.get_rotation();
    double final_rot         = 0.;
    double final_score       = -std::numeric_limits<double>::infinity();
    Domain::Vec2crd orig_tr  = item.get_translation();
    Domain::Vec2crd final_tr = orig_tr;

    bool cancelled        = stop_condition();
    const auto& rotations = item.allowed_rotations();

    // Check all rotations but only if item is not already packed
    for (auto rot_it = rotations.begin(); !cancelled && !packed && rot_it != rotations.end(); ++rot_it)
    {
        double rot = *rot_it;

        item.set_rotation(orig_rot + rot);
        item.set_translation(orig_tr);

        auto nfp     = item.calculate_nfp(packing_context, bed, stop_condition);
        double score = std::numeric_limits<double>::quiet_NaN();
        if (!nfp.empty()) {
            score = pick_best_spot_on_nfp(item, nfp, bed);

            cancelled = stop_condition();
            if (score > final_score) {
                final_score = score;
                final_rot   = rot;
                final_tr    = item.get_translation();
            }
        }
    }

    // If the score is not valid, and the item is not already packed, or
    // the packing was cancelled asynchronously by stop condition, then
    // discard the packing
    bool is_score_valid = !std::isnan(final_score) && !std::isinf(final_score);
    packed              = !cancelled && (packed || is_score_valid);

    if (packed) {
        item.set_translation(final_tr);
        item.set_rotation(orig_rot + final_rot);

        // Finally, consult the kernel if the packing is sane
        packed = kernel->on_item_packed(item);
    }

    return packed;
}
} // namespace Slic3r::Biz::Arrange
