///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEIMPL_HPP
#define ARRANGEIMPL_HPP

#include <random>
#include <map>

#include <libslic3r/Execution/ExecutionTBB.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>

#include <arrange/ArrangeBase.hpp>
#include <arrange/ArrangeFirstFit.hpp>
#include <arrange/NFP/PackStrategyNFP.hpp>
#include <arrange/NFP/Kernels/TMArrangeKernel.hpp>
#include <arrange/NFP/Kernels/GravityKernel.hpp>
#include <arrange/NFP/RectangleOverfitPackingStrategy.hpp>
#include <arrange/Beds.hpp>

#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Items/MutableItemTraits.hpp>
#include <arrange-wrapper/SegmentedRectangleBed.hpp>


#ifndef NDEBUG
#include <arrange/NFP/Kernels/SVGDebugOutputKernelWrapper.hpp>
#endif

namespace Slic3r { namespace arr2 {

// arrange overload for SegmentedRectangleBed which is exactly what is used
// by XL printers.
template<class It,
         class ConstIt,
         class SelectionStrategy,
         class PackStrategy, class...SBedArgs>
void arrange(SelectionStrategy &&selstrategy,
             PackStrategy &&packingstrategy,
             const Range<It> &items,
             const Range<ConstIt> &fixed,
             const SegmentedRectangleBed<SBedArgs...> &bed)
{
    // Dispatch:
    arrange(std::forward<SelectionStrategy>(selstrategy),
            std::forward<PackStrategy>(packingstrategy), items, fixed,
            RectangleBed{bed.bb, bed.gap}, SelStrategyTag<SelectionStrategy>{});

    std::vector<int> bed_indices = get_bed_indices(items, fixed);
    std::map<int, BoundingBox> pilebb;
    std::map<int, bool> bed_occupied;

    for (auto &itm : items) {
        auto bedidx = get_bed_index(itm);
        if (bedidx >= 0) {
            pilebb[bedidx].merge(fixed_bounding_box(itm));
            if (is_wipe_tower(itm))
                bed_occupied[bedidx] = true;
        }
    }

    for (auto &fxitm : fixed) {
        auto bedidx = get_bed_index(fxitm);
        if (bedidx >= 0)
            bed_occupied[bedidx] = true;
    }

    auto bedbb = bounding_box(bed);
    auto piecesz = unscaled(bedbb).size();
    piecesz.x() /= bed.segments_x();
    piecesz.y() /= bed.segments_y();

    using Pivots = RectPivots;

    Pivots pivot = bed.alignment();

    for (int bedidx : bed_indices) {
        if (auto occup_it = bed_occupied.find(bedidx);
            occup_it != bed_occupied.end() && occup_it->second)
            continue;

        BoundingBox bb;
        auto pilesz = unscaled(pilebb[bedidx]).size();
        bb.max.x() = scaled(std::ceil(pilesz.x() / piecesz.x()) * piecesz.x());
        bb.max.y() = scaled(std::ceil(pilesz.y() / piecesz.y()) * piecesz.y());

        switch (pivot) {
        case Pivots::BottomLeft:
            bb.translate(bedbb.min - bb.min);
            break;
        case Pivots::TopRight:
            bb.translate(bedbb.max - bb.max);
            break;
        case Pivots::BottomRight: {
            Point bedref{bedbb.max.x(), bedbb.min.y()};
            Point bbref {bb.max.x(), bb.min.y()};
            bb.translate(bedref - bbref);
            break;
        }
        case Pivots::TopLeft: {
            Point bedref{bedbb.min.x(), bedbb.max.y()};
            Point bbref {bb.min.x(), bb.max.y()};
            bb.translate(bedref - bbref);
            break;
        }
        case Pivots::Center: {
            bb.translate(bedbb.center() - bb.center());
            break;
        }
        default:
            ;
        }

        Vec2crd d = bb.center() - pilebb[bedidx].center();

        auto pilebbx = pilebb[bedidx];
        pilebbx.translate(d);

        Point corr{0, 0};
        corr.x() = -std::min(0, pilebbx.min.x() - bedbb.min.x())
                   -std::max(0, pilebbx.max.x() - bedbb.max.x());
        corr.y() = -std::min(0, pilebbx.min.y() - bedbb.min.y())
                   -std::max(0, pilebbx.max.y() - bedbb.max.y());

        d += corr;

        for (auto &itm : items)
            if (get_bed_index(itm) == static_cast<int>(bedidx) && !is_wipe_tower(itm))
                translate(itm, d);
    }
}


using VariantKernel =
    boost::variant<TMArrangeKernel, GravityKernel>;

template<> struct KernelTraits_<VariantKernel> {
    template<class ArrItem>
    static double placement_fitness(const VariantKernel &kernel,
                                    const ArrItem &itm,
                                    const Vec2crd &transl)
    {
        double ret = NaNd;
        boost::apply_visitor(
            [&](auto &k) { ret = k.placement_fitness(itm, transl); }, kernel);

        return ret;
    }

    template<class ArrItem, class Bed, class Ctx, class RemIt>
    static bool on_start_packing(VariantKernel &kernel,
                                 ArrItem &itm,
                                 const Bed &bed,
                                 const Ctx &packing_context,
                                 const Range<RemIt> &remaining_items)
    {
        bool ret = false;

        boost::apply_visitor([&](auto &k) {
            ret = k.on_start_packing(itm, bed, packing_context, remaining_items);
        }, kernel);

        return ret;
    }

    template<class ArrItem>
    static bool on_item_packed(VariantKernel &kernel, ArrItem &itm)
    {
        bool ret = false;
        boost::apply_visitor([&](auto &k) { ret = k.on_item_packed(itm); },
                             kernel);

        return ret;
    }
};

template<class ArrItem>
struct firstfit::ItemArrangedVisitor<ArrItem, DataStoreOnly<ArrItem>> {
    template<class Bed, class PIt, class RIt>
    static void on_arranged(ArrItem &itm,
                            const Bed &bed,
                            const Range<PIt> &packed,
                            const Range<RIt> &remaining)
    {
        using OnArrangeCb = std::function<void(StripCVRef<ArrItem> &)>;

        auto cb = get_data<OnArrangeCb>(itm, "on_arranged");

        if (cb) {
            (*cb)(itm);
        }
    }
};

inline RectPivots xlpivots_to_rect_pivots(ArrangeSettingsView::XLPivots xlpivot)
{
    if (xlpivot == arr2::ArrangeSettingsView::xlpRandom) {
        // means it should be random
        std::random_device rd{};
        std::mt19937 rng(rd());
        std::uniform_int_distribution<std::mt19937::result_type>
            dist(0, arr2::ArrangeSettingsView::xlpRandom - 1);
        xlpivot = static_cast<ArrangeSettingsView::XLPivots>(dist(rng));
    }

    RectPivots rectpivot = RectPivots::Center;

    switch(xlpivot) {
    case arr2::ArrangeSettingsView::xlpCenter: rectpivot = RectPivots::Center; break;
    case arr2::ArrangeSettingsView::xlpFrontLeft: rectpivot = RectPivots::BottomLeft; break;
    case arr2::ArrangeSettingsView::xlpFrontRight: rectpivot = RectPivots::BottomRight; break;
    case arr2::ArrangeSettingsView::xlpRearLeft: rectpivot = RectPivots::TopLeft; break;
    case arr2::ArrangeSettingsView::xlpRearRight: rectpivot = RectPivots::TopRight; break;
    default:
        ;
    }

    return rectpivot;
}

template<class It, class Bed>
void fill_rotations(const Range<It>           &items,
                    const Bed                 &bed,
                    const ArrangeSettingsView &settings)
{
    if (!settings.is_rotation_enabled())
        return;

    for (auto &itm : items) {
        if (is_wipe_tower(itm)) // Rotating the wipe tower is currently problematic
            continue;

        // Use the minimum bounding box rotation as a starting point.
        auto minbbr = get_min_area_bounding_box_rotation(itm);
        std::vector<double> rotations =
            {minbbr,
             minbbr + PI / 4., minbbr + PI / 2.,
             minbbr + PI,      minbbr + 3 * PI / 4.};

        // Add the original rotation of the item if minbbr
        // is not already the original rotation (zero)
        if (std::abs(minbbr) > 0.)
            rotations.emplace_back(0.);

        // Also try to find the rotation that fits the item
        // into a rectangular bed, given that it cannot fit,
        // and there exists a rotation which can fit.
        if constexpr (std::is_convertible_v<Bed, RectangleBed>) {
            double fitbrot = get_fit_into_bed_rotation(itm, bed);
            if (std::abs(fitbrot) > 0.)
                rotations.emplace_back(fitbrot);
        }

        set_allowed_rotations(itm, rotations);
    }
}

// An arranger put together to fulfill all the requirements of PrusaSlicer based
// on the supplied ArrangeSettings
template<class ArrItem>
class DefaultArranger: public Arranger<ArrItem> {
    ArrangeSettings m_settings;

    static constexpr auto Accuracy = 1.;

    template<class It, class FixIt, class Bed>
    void arrange_(
        const Range<It>     &items,
        const Range<FixIt>  &fixed,
        const Bed &bed,
        ArrangerCtl<ArrItem> &ctl)
    {
        auto cmpfn = [](const auto &itm1, const auto &itm2) {
            int pa = get_priority(itm1);
            int pb = get_priority(itm2);

            return pa == pb ? area(envelope_convex_hull(itm1)) > area(envelope_convex_hull(itm2)) :
                              pa > pb;
        };

        auto on_arranged = [&ctl](auto &itm, auto &bed, auto &ctx, auto &rem) {
            ctl.update_status(rem.size());

            ctl.on_packed(itm);

            firstfit::DefaultOnArrangedFn{}(itm, bed, ctx, rem);
        };

        auto stop_cond = [&ctl] { return ctl.was_canceled(); };

        firstfit::SelectionStrategy sel{cmpfn, on_arranged, stop_cond};

        constexpr auto ep = ex_tbb;

        VariantKernel basekernel;
        switch (m_settings.get_arrange_strategy()) {
        default:
            [[fallthrough]];
        case ArrangeSettingsView::asAuto:
            if constexpr (std::is_convertible_v<Bed, CircleBed>){
                basekernel = GravityKernel{};
            } else {
                basekernel = TMArrangeKernel{items.size(), area(bed)};
            }
            break;
        case ArrangeSettingsView::asPullToCenter:
            basekernel = GravityKernel{};
            break;
        }

#ifndef NDEBUG
        SVGDebugOutputKernelWrapper<VariantKernel> kernel{bounding_box(bed), basekernel};
#else
        auto & kernel = basekernel;
#endif

        fill_rotations(items, bed, m_settings);

        bool with_wipe_tower = std::any_of(items.begin(), items.end(),
                                           [](auto &itm) {
                                               return is_wipe_tower(itm);
                                           });

        // With rectange bed, and no fixed items, let's use an infinite bed
        // with RectangleOverfitKernelWrapper. It produces better results than
        // a pure RectangleBed with inner-fit polygon calculation.
        if (!with_wipe_tower &&
            m_settings.get_arrange_strategy() == ArrangeSettingsView::asAuto &&
            IsRectangular<Bed>) {
            PackStrategyNFP base_strategy{std::move(kernel), ep, Accuracy, stop_cond};

            RectangleOverfitPackingStrategy final_strategy{std::move(base_strategy)};

            arr2::arrange(sel, final_strategy, items, fixed, bed);
        } else {
            PackStrategyNFP ps{std::move(kernel), ep, Accuracy, stop_cond};

            arr2::arrange(sel, ps, items, fixed, bed);
        }
    }

public:
    explicit DefaultArranger(const ArrangeSettingsView &settings)
    {
        m_settings.set_from(settings);
    }

    void arrange(
        std::vector<ArrItem> &items,
        const std::vector<ArrItem> &fixed,
        const ExtendedBed &bed,
        ArrangerCtl<ArrItem> &ctl) override
    {
        visit_bed([this, &items, &fixed, &ctl](auto rawbed) {

            if constexpr (IsSegmentedBed<decltype(rawbed)>)
                rawbed.pivot = xlpivots_to_rect_pivots(
                    m_settings.get_xl_alignment());

            arrange_(range(items), crange(fixed), rawbed, ctl);
        }, bed);
    }
};

template<class ArrItem>
std::unique_ptr<Arranger<ArrItem>> Arranger<ArrItem>::create(
    const ArrangeSettingsView &settings)
{
    // Currently all that is needed is handled by DefaultArranger
    return std::make_unique<DefaultArranger<ArrItem>>(settings);
}

template<class ArrItem>
ArrItem ConvexItemConverter<ArrItem>::convert(const Arrangeable &arrbl,
                                              coord_t offs) const
{
    auto bed_index = arrbl.get_bed_index();
    Polygon outline = arrbl.convex_outline();

    if (outline.empty())
        throw EmptyItemOutlineError{};

    Polygon envelope = arrbl.convex_envelope();

    coord_t infl = offs + coord_t(std::ceil(this->safety_dist() / 2.));

    if (infl != 0) {
        outline = Geometry::convex_hull(offset(outline, infl));
        if (! envelope.empty())
            envelope = Geometry::convex_hull(offset(envelope, infl));
    }

    ArrItem ret;
    set_convex_shape(ret, outline);
    if (! envelope.empty())
        set_convex_envelope(ret, envelope);

    set_bed_index(ret, bed_index);
    set_priority(ret, arrbl.priority());
    set_bed_constraint(ret, arrbl.bed_constraint());

    imbue_id(ret, arrbl.id());
    if constexpr (IsWritableDataStore<ArrItem>)
        arrbl.imbue_data(AnyWritableDataStore{ret});

    return ret;
}

template<class ArrItem>
ArrItem AdvancedItemConverter<ArrItem>::convert(const Arrangeable &arrbl,
                                                coord_t offs) const
{
    auto bed_index = arrbl.get_bed_index();
    ArrItem ret = get_arritem(arrbl, offs);

    set_bed_index(ret, bed_index);
    set_priority(ret, arrbl.priority());
    set_bed_constraint(ret, arrbl.bed_constraint());
    imbue_id(ret, arrbl.id());
    if constexpr (IsWritableDataStore<ArrItem>)
        arrbl.imbue_data(AnyWritableDataStore{ret});

    return ret;
}

template<class ArrItem>
ArrItem AdvancedItemConverter<ArrItem>::get_arritem(const Arrangeable &arrbl,
                                                    coord_t    offs) const
{
    coord_t infl = offs + coord_t(std::ceil(this->safety_dist() / 2.));

    auto outline = arrbl.full_outline();

    if (outline.empty())
        throw EmptyItemOutlineError{};

    auto envelope = arrbl.full_envelope();

    if (infl != 0) {
        outline = offset_ex(outline, infl);
        if (! envelope.empty())
            envelope = offset_ex(envelope, infl);
    }

    auto simpl_tol = static_cast<double>(this->simplification_tolerance());

    if (simpl_tol > 0.)
    {
        outline = expolygons_simplify(outline, simpl_tol);
        if (!envelope.empty())
            envelope = expolygons_simplify(envelope, simpl_tol);
    }

    ArrItem ret;
    set_shape(ret, outline);
    if (! envelope.empty())
        set_envelope(ret, envelope);

    return ret;
}

template<class ArrItem>
ArrItem BalancedItemConverter<ArrItem>::get_arritem(const Arrangeable &arrbl,
                                                    coord_t    offs) const
{
    ArrItem ret = AdvancedItemConverter<ArrItem>::get_arritem(arrbl, offs);
    set_convex_envelope(ret, envelope_convex_hull(ret));

    return ret;
}

template<class ArrItem>
std::unique_ptr<ArrangeableToItemConverter<ArrItem>>
ArrangeableToItemConverter<ArrItem>::create(
    ArrangeSettingsView::GeometryHandling gh,
    coord_t safety_d)
{
    std::unique_ptr<ArrangeableToItemConverter<ArrItem>> ret;

    constexpr coord_t SimplifyTol = scaled(.2);

    switch(gh) {
    case arr2::ArrangeSettingsView::ghConvex:
        ret = std::make_unique<ConvexItemConverter<ArrItem>>(safety_d);
        break;
    case arr2::ArrangeSettingsView::ghBalanced:
        ret = std::make_unique<BalancedItemConverter<ArrItem>>(safety_d, SimplifyTol);
        break;
    case arr2::ArrangeSettingsView::ghAdvanced:
        ret = std::make_unique<AdvancedItemConverter<ArrItem>>(safety_d, SimplifyTol);
        break;
    default:
        ;
    }

    return ret;
}

}} // namespace Slic3r::arr2

#endif // ARRANGEIMPL_HPP
