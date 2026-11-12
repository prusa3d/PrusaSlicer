#include <boost/geometry/index/rtree.hpp>
#include <optional>
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Biz/Arrange/Kernels/KernelUtils.hpp"
#include "Slic3r/Biz/Arrange/Kernels/TMArrangeKernel.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::Scaling::scaled;

namespace {
double fixed_area(const ArrangeItem& itm)
{
    return itm.fixed_shape().area_unscaled()
        * static_cast<double>(scaled(1.))
        * static_cast<double>(scaled(1.));
}

double movable_area(const ArrangeItem& itm)
{
    return itm.movable_shape().area_unscaled()
        * static_cast<double>(scaled(1.))
        * static_cast<double>(scaled(1.));
}

double area(const Domain::BoundingBox2crd& bb)
{
    auto bbsz = Biz::Algorithms::BoundingBox::sizes(bb);
    return double(bbsz.x()) * bbsz.y();
}
} // namespace

// Summon the spatial indexing facilities from boost
namespace bgi     = boost::geometry::index;
using SpatElement = std::pair<Domain::BoundingBox2crd, unsigned>;
using SpatIndex   = bgi::rtree<SpatElement, bgi::rstar<16, 4>>;

double TMArrangeKernel::norm(double val) const
{
    return double(val) / m_norm;
}

// Treat big items (compared to the print bed) differently
bool TMArrangeKernel::is_big(double a) const
{
    return a / m_bin_area > BigItemTreshold;
}

const Domain::BoundingBox2crd& TMArrangeKernel::pilebb() const
{
    return m_pilebb;
}

TMArrangeKernel::TMArrangeKernel(Domain::Vec2crd gravity_center, size_t itm_cnt, double bedarea) :
    m_bin_area(bedarea),
    m_item_cnt{itm_cnt},
    sink{gravity_center}
{}

TMArrangeKernel::TMArrangeKernel(size_t itm_cnt, double bedarea) :
    m_bin_area(bedarea),
    m_item_cnt{itm_cnt}
{}

double TMArrangeKernel::placement_fitness(const ArrangeItem& item, const Domain::Vec2crd& transl) const
{
    // Candidate item bounding box
    const auto ibb = Biz::Algorithms::BoundingBox::translated(
        item.movable_shape().bounding_box(),
        transl
    );

    auto itmcntr = item.movable_shape().centroid();
    itmcntr += transl;

    // Calculate the full bounding box of the pile with the candidate item
    const auto fullbb = Biz::Algorithms::BoundingBox::merge(m_pilebb, ibb);

    // The bounding box of the big items (they will accumulate in the center
    // of the pile
    Domain::BoundingBox2crd bigbb;
    if (m_rtree.empty()) {
        bigbb = fullbb;
    } else {
        auto boostbb = m_rtree.bounds();
        boost::geometry::convert(boostbb, bigbb);
    }

    // Will hold the resulting score
    double score = 0;

    // Distinction of cases for the arrangement scene
    enum e_cases
    {
        // This branch is for big items in a mixed (big and small) scene
        // OR for all items in a small-only scene.
        BIG_ITEM,

        // For small items in a mixed scene.
        SMALL_ITEM,

    } compute_case;

    bool bigitems = is_big(movable_area(item)) || m_rtree.empty();
    if (bigitems)
        compute_case = BIG_ITEM;
    else
        compute_case = SMALL_ITEM;

    switch (compute_case) {
    case BIG_ITEM: {
        const Domain::Point& minc = ibb.min; // bottom left corner
        const Domain::Point& maxc = ibb.max; // top right corner

        // top left and bottom right corners
        Domain::Point top_left{minc.x(), maxc.y()};
        Domain::Point bottom_right{maxc.x(), minc.y()};

        // The smallest distance from the arranged pile center:
        double dist = norm(
            (itmcntr - Biz::Algorithms::BoundingBox::center(m_pilebb)).template cast<double>().norm()
        );

        // Prepare a variable for the alignment score.
        // This will indicate: how well is the candidate item
        // aligned with its neighbors. We will check the alignment
        // with all neighbors and return the score for the best
        // alignment. So it is enough for the candidate to be
        // aligned with only one item.
        auto alignment_score = 1.;

        auto query  = bgi::intersects(ibb);
        auto& index = is_big(movable_area(item)) ? m_rtree : m_smallsrtree;

        // Query the spatial index for the neighbors
        std::vector<SpatElement> result;
        result.reserve(index.size());

        index.query(query, std::back_inserter(result));

        // now get the score for the best alignment
        for (auto& e : result) {
            auto idx           = e.second;
            const ItemStats& p = m_itemstats[idx];
            auto parea         = p.area;
            if (std::abs(1.0 - parea / fixed_area(item)) < 1e-6) {
                const auto bb = Biz::Algorithms::BoundingBox::merge(p.bb, ibb);
                auto bbarea   = area(bb);
                auto ascore = 1.0 - (area(item.fixed_shape().bounding_box()) + area(p.bb)) / bbarea;

                if (ascore < alignment_score)
                    alignment_score = ascore;
            }
        }

        double R = double(m_rem_cnt) / (m_item_cnt);
        R        = std::pow(R, 1. / 3.);

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
        score = norm(
            (itmcntr - Biz::Algorithms::BoundingBox::center(bigbb)).template cast<double>().norm()
        );
        break;
    }
    }

    return -score;
}

bool TMArrangeKernel::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    std::span<const ArrangeItem> remaining_items
)
{
    const std::optional<Domain::Vec2d> gravity_sink{itm.gravity_sink};
    if (gravity_sink) {
        const Domain::Vec2d dimensions{
            Algorithms::BoundingBox::sizes(bed.bounding_box()).cast<double>()
        };
        const Domain::Vec2d min{bed.bounding_box().min.cast<double>()};
        item_sink = (min + gravity_sink->cwiseProduct(dimensions)).cast<int>();
    } else {
        item_sink = std::nullopt;
    }

    if (!sink) {
        sink = Biz::Algorithms::BoundingBox::center(bed.bounding_box());
    }

    if (item_sink)
        active_sink = *item_sink;
    else
        active_sink = *sink;

    const auto& fixed = packing_context.all_items();

    bool ret = find_initial_position(itm, active_sink, bed, packing_context);

    m_rem_cnt = remaining_items.size();

    if (m_item_cnt == 0)
        m_item_cnt = m_rem_cnt + fixed.size() + 1;

    if (std::isnan(m_bin_area)) {
        auto sz = Biz::Algorithms::BoundingBox::sizes(bed.bounding_box());

        m_bin_area = static_cast<double>(Biz::Algorithms::Scaling::scaled<int64_t>(
            Biz::Algorithms::Scaling::unscaled<double>(sz.x())
            * Biz::Algorithms::Scaling::unscaled<double>(sz.y())
        ));
    }

    m_norm = std::sqrt(m_bin_area);

    m_itemstats.clear();
    m_itemstats.reserve(fixed.size());
    m_rtree.clear();
    m_smallsrtree.clear();
    m_pilebb     = {active_sink, active_sink};
    unsigned idx = 0;
    for (auto& fixitem : fixed) {
        auto fixitmbb = fixitem.fixed_shape().bounding_box();
        m_itemstats.emplace_back(ItemStats{fixed_area(fixitem), fixitmbb});
        m_pilebb = Biz::Algorithms::BoundingBox::merge(m_pilebb, fixitmbb);

        if (is_big(fixed_area(fixitem)))
            m_rtree.insert({fixitmbb, idx});

        m_smallsrtree.insert({fixitmbb, idx});
        idx++;
    }

    return ret;
}

bool TMArrangeKernel::on_item_packed(ArrangeItem& itm)
{
    return true;
}

} // namespace Slic3r::Biz::Arrange::Kernels
