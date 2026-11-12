#pragma once

#include <boost/geometry/index/rtree.hpp>
#include <optional>
#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"

#include "Slic3r/Biz/Algorithms/BoostAdapter.hpp" // IWYU pragma: keep

namespace Slic3r::Biz::Arrange::Kernels {

// Summon the spatial indexing facilities from boost
namespace bgi     = boost::geometry::index;
using SpatElement = std::pair<Domain::BoundingBox2crd, unsigned>;
using SpatIndex   = bgi::rtree<SpatElement, bgi::rstar<16, 4>>;

class TMArrangeKernel : public IKernel
{
    SpatIndex m_rtree; // spatial index for the normal (bigger) objects
    SpatIndex m_smallsrtree; // spatial index for only the smaller items
    Domain::BoundingBox2crd m_pilebb;
    double m_bin_area = std::numeric_limits<double>::quiet_NaN();
    double m_norm;
    size_t m_rem_cnt  = 0;
    size_t m_item_cnt = 0;

    struct ItemStats
    {
        double area = 0.;
        Domain::BoundingBox2crd bb;
    };

    std::vector<ItemStats> m_itemstats;

    // A coefficient used in separating bigger items and smaller items.
    static constexpr double BigItemTreshold = 0.02;

    double norm(double val) const;

    // Treat big items (compared to the print bed) differently
    bool is_big(double a) const;

protected:
    std::optional<Domain::Point> sink;
    std::optional<Domain::Point> item_sink;
    Domain::Point active_sink;

    const Domain::BoundingBox2crd& pilebb() const;

public:
    TMArrangeKernel() = default;

    TMArrangeKernel(
        Domain::Vec2crd gravity_center,
        size_t itm_cnt,
        double bedarea = std::numeric_limits<double>::quiet_NaN()
    );

    TMArrangeKernel(size_t itm_cnt, double bedarea = std::numeric_limits<double>::quiet_NaN());

    double placement_fitness(const ArrangeItem& item, const Domain::Vec2crd& transl) const final;

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> remaining_items
    ) final;

    bool on_item_packed(ArrangeItem& itm) final;
};

} // namespace Slic3r::Biz::Arrange::Kernels
