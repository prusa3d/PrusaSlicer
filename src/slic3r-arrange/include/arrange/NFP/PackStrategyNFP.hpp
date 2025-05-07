///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef PACKSTRATEGYNFP_HPP
#define PACKSTRATEGYNFP_HPP

#include <arrange/ArrangeBase.hpp>

#include <arrange/NFP/EdgeCache.hpp>
#include <arrange/NFP/Kernels/KernelTraits.hpp>
#include <arrange/NFP/NFPArrangeItemTraits.hpp>

#include "libslic3r/Optimize/NLoptOptimizer.hpp"
#include "libslic3r/Execution/ExecutionSeq.hpp"

namespace Slic3r { namespace arr2 {

struct NFPPackingTag{};

struct DummyArrangeKernel
{
    template<class ArrItem>
    double placement_fitness(const ArrItem &itm, const Vec2crd &dest_pos) const
    {
        return NaNd;
    }

    template<class ArrItem, class Bed, class Context, class RemIt>
    bool on_start_packing(ArrItem            &itm,
                          const Bed          &bed,
                          const Context      &packing_context,
                          const Range<RemIt> &remaining_items)
    {
        return true;
    }

    template<class ArrItem> bool on_item_packed(ArrItem &itm) { return true; }
};

template<class Strategy> using OptAlg = typename Strategy::OptAlg;

template<class ArrangeKernel = DummyArrangeKernel,
         class ExecPolicy = ExecutionSeq,
         class OptMethod = opt::AlgNLoptSubplex,
         class StopCond  = DefaultStopCondition>
struct PackStrategyNFP {
    using OptAlg = OptMethod;

    ArrangeKernel kernel;
    ExecPolicy ep;
    double accuracy = 1.;
    opt::Optimizer<OptMethod> solver;
    StopCond stop_condition;

    PackStrategyNFP(opt::Optimizer<OptMethod> slv,
                    ArrangeKernel k = {},
                    ExecPolicy execpolicy = {},
                    double accur = 1.,
                    StopCond stop_cond = {})
        : kernel{std::move(k)},
          ep{std::move(execpolicy)},
          accuracy{accur},
          solver{std::move(slv)},
          stop_condition{std::move(stop_cond)}
    {}

    PackStrategyNFP(ArrangeKernel k = {},
                    ExecPolicy execpolicy = {},
                    double accur = 1.,
                    StopCond stop_cond = {})
        : PackStrategyNFP{opt::Optimizer<OptMethod>{}, std::move(k),
                          std::move(execpolicy), accur, std::move(stop_cond)}
    {
        // Defaults for AlgNLoptSubplex
        auto iters = static_cast<unsigned>(std::floor(1000 * accuracy));
        auto optparams =
            opt::StopCriteria{}.max_iterations(iters).rel_score_diff(
                1e-20) /*.abs_score_diff(1e-20)*/;

        solver.set_criteria(optparams);
    }
};

template<class...Args>
struct PackStrategyTag_<PackStrategyNFP<Args...>>
{
    using Tag = NFPPackingTag;
};


template<class ArrItem, class Bed, class PStrategy>
double pick_best_spot_on_nfp_verts_only(ArrItem            &item,
                                        const ExPolygons   &nfp,
                                        const Bed          &bed,
                                        const PStrategy    &strategy)
{
    using KernelT = KernelTraits<decltype(strategy.kernel)>;

    auto    score   = -std::numeric_limits<double>::infinity();
    Vec2crd orig_tr = get_translation(item);
    Vec2crd translation{0, 0};

    auto eval_fitness = [&score, &strategy, &item, &translation,
                         &orig_tr](const Vec2crd &p) {
        set_translation(item, orig_tr);
        Vec2crd ref_v = reference_vertex(item);
        Vec2crd tr    = p - ref_v;
        double fitness = KernelT::placement_fitness(strategy.kernel, item, tr);
        if (fitness > score) {
            score       = fitness;
            translation = tr;
        }
    };

    for (const ExPolygon &expoly : nfp) {
        for (const Point &p : expoly.contour) {
            eval_fitness(p);
        }

        for (const Polygon &h : expoly.holes)
            for (const Point &p : h.points)
                eval_fitness(p);
    }

    set_translation(item, orig_tr + translation);

    return score;
}

struct CornerResult
{
    size_t         contour_id;
    opt::Result<1> oresult;
};

template<class ArrItem, class Bed, class... Args>
double pick_best_spot_on_nfp(ArrItem                        &item,
                             const ExPolygons               &nfp,
                             const Bed                      &bed,
                             const PackStrategyNFP<Args...> &strategy)
{
    auto &ex_policy = strategy.ep;
    using KernelT = KernelTraits<decltype(strategy.kernel)>;

    auto score = -std::numeric_limits<double>::infinity();
    Vec2crd orig_tr = get_translation(item);
    Vec2crd translation{0, 0};
    Vec2crd ref_v = reference_vertex(item);

    auto edge_caches = reserve_vector<EdgeCache>(nfp.size());
    auto sample_sets = reserve_vector<std::vector<ContourLocation>>(
        nfp.size());

    for (const ExPolygon &expoly : nfp) {
        edge_caches.emplace_back(EdgeCache{&expoly});
        edge_caches.back().sample_contour(strategy.accuracy,
                                          sample_sets.emplace_back());
    }

    auto nthreads = execution::max_concurrency(ex_policy);

    std::vector<CornerResult> gresults(edge_caches.size());

    auto resultcmp = [](auto &a, auto &b) {
        return a.oresult.score < b.oresult.score;
    };

    execution::for_each(
        ex_policy, size_t(0), edge_caches.size(),
        [&](size_t edge_cache_idx) {
            auto &ec_contour = edge_caches[edge_cache_idx];
            auto &corners = sample_sets[edge_cache_idx];
            std::vector<CornerResult> results(corners.size());

            auto cornerfn = [&](size_t i) {
                ContourLocation cr = corners[i];
                auto objfn = [&](opt::Input<1> &in) {
                    Vec2crd p = ec_contour.coords(ContourLocation{cr.contour_id, in[0]});
                    Vec2crd tr = p - ref_v;

                    return KernelT::placement_fitness(strategy.kernel, item, tr);
                };

                // Assuming that solver is a lightweight object
                auto solver = strategy.solver;
                solver.to_max();
                auto oresult = solver.optimize(objfn,
                                               opt::initvals({cr.dist}),
                                               opt::bounds({{0., 1.}}));

                results[i] = CornerResult{cr.contour_id, oresult};
            };

            execution::for_each(ex_policy, size_t(0), results.size(),
                                cornerfn, nthreads);

            auto it = std::max_element(results.begin(), results.end(),
                                       resultcmp);

            if (it != results.end())
                gresults[edge_cache_idx] = *it;
        },
        nthreads);

    auto it = std::max_element(gresults.begin(), gresults.end(), resultcmp);
    if (it != gresults.end()) {
        score = it->oresult.score;
        size_t path_id = std::distance(gresults.begin(), it);
        size_t contour_id = it->contour_id;
        double dist = it->oresult.optimum[0];

        Vec2crd pos = edge_caches[path_id].coords(ContourLocation{contour_id, dist});
        Vec2crd tr = pos - ref_v;

        set_translation(item, orig_tr + tr);
    }

    return score;
}

template<class Strategy, class ArrItem, class Bed, class RemIt>
bool pack(Strategy &strategy,
          const Bed &bed,
          ArrItem &item,
          const PackStrategyContext<Strategy, ArrItem> &packing_context,
          const Range<RemIt> &remaining_items,
          const NFPPackingTag &)
{
    using KernelT = KernelTraits<decltype(strategy.kernel)>;

    // The kernel might pack the item immediately
    bool packed = KernelT::on_start_packing(strategy.kernel, item, bed,
                                            packing_context, remaining_items);

    double  orig_rot    = get_rotation(item);
    double  final_rot   = 0.;
    double  final_score = -std::numeric_limits<double>::infinity();
    Vec2crd orig_tr     = get_translation(item);
    Vec2crd final_tr    = orig_tr;

    bool cancelled = strategy.stop_condition();
    const auto & rotations = allowed_rotations(item);

    // Check all rotations but only if item is not already packed
    for (auto rot_it = rotations.begin();
         !cancelled && !packed && rot_it != rotations.end(); ++rot_it) {

        double rot = *rot_it;

        set_rotation(item, orig_rot + rot);
        set_translation(item, orig_tr);

        auto nfp = calculate_nfp(item, packing_context, bed,
                                 strategy.stop_condition);
        double score = NaNd;
        if (!nfp.empty()) {
            score = pick_best_spot_on_nfp(item, nfp, bed, strategy);

            cancelled = strategy.stop_condition();
            if (score > final_score) {
                final_score = score;
                final_rot   = rot;
                final_tr    = get_translation(item);
            }
        }
    }

    // If the score is not valid, and the item is not already packed, or
    // the packing was cancelled asynchronously by stop condition, then
    // discard the packing
    bool is_score_valid = !std::isnan(final_score) && !std::isinf(final_score);
    packed = !cancelled && (packed || is_score_valid);

    if (packed) {
        set_translation(item, final_tr);
        set_rotation(item, orig_rot + final_rot);

        // Finally, consult the kernel if the packing is sane
        packed = KernelT::on_item_packed(strategy.kernel, item);
    }

    return packed;
}

}} // namespace Slic3r::arr2

#endif // PACKSTRATEGYNFP_HPP
