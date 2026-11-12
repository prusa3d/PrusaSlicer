#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::BoundingBox::center;
using Algorithms::Polygon::get_extents;

bool find_initial_position(
    ArrangeItem& itm,
    const Domain::Vec2crd& sink,
    const IBed& bed,
    const PackingContext& packing_context
)
{
    if (dynamic_cast<const IrregularBed*>(&bed) != nullptr) {
        return false;
    }

    bool ret = false;
    if (packing_context.all_items().empty()) {
        auto rotations = itm.allowed_rotations();
        itm.set_rotation(0.0);
        auto chull{itm.movable_shape().convex_hull()};

        for (double rot : rotations) {
            Domain::Polygon chullcpy = chull;
            chullcpy.rotate(rot);
            auto bbitm = get_extents(chullcpy);

            Domain::Vec2crd cb{sink};
            Domain::Vec2crd ci{center(bbitm)};

            Domain::Vec2crd d{cb - ci};
            bbitm = Biz::Algorithms::BoundingBox::translated(bbitm, d);

            if (bed.bounding_box().contains(bbitm)) {
                itm.set_rotation(rot);
                itm.set_translation(d);
                ret = true;
                break;
            }
        }
    }

    return ret;
}
} // namespace Slic3r::Biz::Arrange::Kernels
