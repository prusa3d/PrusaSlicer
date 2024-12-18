///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef TMARRANGEKERNEL_HPP
#define TMARRANGEKERNEL_HPP

#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/Beds.hpp>
#include <arrange/NFP/Kernels/KernelUtils.hpp>

#include <boost/geometry/index/rtree.hpp>
#include <libslic3r/BoostAdapter.hpp>

namespace Slic3r { namespace arr2 {

// Summon the spatial indexing facilities from boost
namespace bgi = boost::geometry::index;
using SpatElement = std::pair<BoundingBox, unsigned>;
using SpatIndex = bgi::rtree<SpatElement, bgi::rstar<16, 4> >;

class TMArrangeKernel {
    SpatIndex   m_rtree;        // spatial index for the normal (bigger) objects
    SpatIndex   m_smallsrtree;  // spatial index for only the smaller items
    BoundingBox m_pilebb;
    double      m_bin_area = NaNd;
    double      m_norm;
    size_t      m_rem_cnt = 0;
    size_t      m_item_cnt = 0;


    struct ItemStats { double area = 0.; BoundingBox bb; };
    std::vector<ItemStats> m_itemstats;

    // A coefficient used in separating bigger items and smaller items.
    static constexpr double BigItemTreshold = 0.02;

    template<class T> ArithmeticOnly<T, double> norm(T val) const
    {
        return double(val) / m_norm;
    }

    // Treat big items (compared to the print bed) differently
    bool is_big(double a) const { return a / m_bin_area > BigItemTreshold; }

protected:
    std::optional<Point> sink;
    std::optional<Point> item_sink;
    Point                active_sink;

    const BoundingBox & pilebb() const { return m_pilebb; }

public:
    TMArrangeKernel() = default;
    TMArrangeKernel(Vec2crd gravity_center, size_t itm_cnt, double bedarea = NaNd)
        : m_bin_area(bedarea)
        , m_item_cnt{itm_cnt}
        , sink{gravity_center}
    {}

    TMArrangeKernel(size_t itm_cnt, double bedarea = NaNd)
        : m_bin_area(bedarea), m_item_cnt{itm_cnt}
    {}

    template<class ArrItem>
    double placement_fitness(const ArrItem &item, const Vec2crd &transl) const
    {
        // Candidate item bounding box
        auto ibb = envelope_bounding_box(item);
        ibb.translate(transl);
        auto itmcntr = envelope_centroid(item);
        itmcntr += transl;

        // Calculate the full bounding box of the pile with the candidate item
        auto fullbb = m_pilebb;
        fullbb.merge(ibb);

        // The bounding box of the big items (they will accumulate in the center
        // of the pile
        BoundingBox bigbb;
        if(m_rtree.empty()) {
            bigbb = fullbb;
        }
        else {
            auto boostbb = m_rtree.bounds();
            boost::geometry::convert(boostbb, bigbb);
        }

        // Will hold the resulting score
        double score = 0;

        // Distinction of cases for the arrangement scene
        enum e_cases {
            // This branch is for big items in a mixed (big and small) scene
            // OR for all items in a small-only scene.
            BIG_ITEM,

            // For small items in a mixed scene.
            SMALL_ITEM,

            WIPE_TOWER,
        } compute_case;

        bool is_wt = is_wipe_tower(item);
        bool bigitems = is_big(envelope_area(item)) || m_rtree.empty();
        if (is_wt)
            compute_case = WIPE_TOWER;
        else if (bigitems)
            compute_case = BIG_ITEM;
        else
            compute_case = SMALL_ITEM;

        switch (compute_case) {
        case WIPE_TOWER: {
            score = (unscaled(itmcntr) - unscaled(active_sink)).squaredNorm();
            break;
        }
        case BIG_ITEM: {
            const Point& minc = ibb.min; // bottom left corner
            const Point& maxc = ibb.max; // top right corner

            // top left and bottom right corners
            Point top_left{minc.x(), maxc.y()};
            Point bottom_right{maxc.x(), minc.y()};

            // The smallest distance from the arranged pile center:
            double dist = norm((itmcntr - m_pilebb.center()).template cast<double>().norm());

            // Prepare a variable for the alignment score.
            // This will indicate: how well is the candidate item
            // aligned with its neighbors. We will check the alignment
            // with all neighbors and return the score for the best
            // alignment. So it is enough for the candidate to be
            // aligned with only one item.
            auto alignment_score = 1.;

            auto query = bgi::intersects(ibb);
            auto& index = is_big(envelope_area(item)) ? m_rtree : m_smallsrtree;

            // Query the spatial index for the neighbors
            std::vector<SpatElement> result;
            result.reserve(index.size());

            index.query(query, std::back_inserter(result));

            // now get the score for the best alignment
            for(auto& e : result) {
                auto idx = e.second;
                const ItemStats& p = m_itemstats[idx];
                auto parea = p.area;
                if(std::abs(1.0 - parea / fixed_area(item)) < 1e-6) {
                    auto bb = p.bb;
                    bb.merge(ibb);
                    auto bbarea = area(bb);
                    auto ascore = 1.0 - (area(fixed_bounding_box(item)) + area(p.bb)) / bbarea;

                    if(ascore < alignment_score)
                        alignment_score = ascore;
                }
            }

            double R = double(m_rem_cnt) / (m_item_cnt);
            R = std::pow(R, 1./3.);

            // The final mix of the score is the balance between the
            // distance from the full pile center, the pack density and
            // the alignment with the neighbors

            // Let the density matter more when fewer objects remain
            score = 0.6 * dist + 0.1 * alignment_score + (1.0 - R) * (0.3 * dist) + R * 0.3 * alignment_score;

            break;
        }
        case SMALL_ITEM: {
            // Here there are the small items that should be placed around the
            // already processed bigger items.
            // No need to play around with the anchor points, the center will be
            // just fine for small items
            score = norm((itmcntr - bigbb.center()).template cast<double>().norm());
            break;
        }
        }

        return -score;
    }

    template<class ArrItem, class Bed, class Context, class RemIt>
    bool on_start_packing(ArrItem &itm,
                          const Bed &bed,
                          const Context &packing_context,
                          const Range<RemIt> &remaining_items)
    {
        item_sink = get_gravity_sink(itm);

        if (!sink) {
            sink = bounding_box(bed).center();
        }

        if (item_sink)
            active_sink = *item_sink;
        else
            active_sink = *sink;

        auto fixed = all_items_range(packing_context);

        bool ret = find_initial_position(itm, active_sink, bed, packing_context);

        m_rem_cnt = remaining_items.size();

        if (m_item_cnt == 0)
            m_item_cnt = m_rem_cnt + fixed.size() + 1;

        if (std::isnan(m_bin_area)) {
            auto sz = bounding_box(bed).size();

            m_bin_area = scaled<double>(unscaled(sz.x()) * unscaled(sz.y()));
        }

        m_norm = std::sqrt(m_bin_area);

        m_itemstats.clear();
        m_itemstats.reserve(fixed.size());
        m_rtree.clear();
        m_smallsrtree.clear();
        m_pilebb = {active_sink, active_sink};
        unsigned idx = 0;
        for (auto &fixitem : fixed) {
            auto fixitmbb = fixed_bounding_box(fixitem);
            m_itemstats.emplace_back(ItemStats{fixed_area(fixitem), fixitmbb});
            m_pilebb.merge(fixitmbb);

            if(is_big(fixed_area(fixitem)))
                m_rtree.insert({fixitmbb, idx});

            m_smallsrtree.insert({fixitmbb, idx});
            idx++;
        }

        return ret;
    }

    template<class ArrItem>
    bool on_item_packed(ArrItem &itm) { return true; }
};

}} // namespace Slic3r::arr2

#endif // TMARRANGEKERNEL_HPP
