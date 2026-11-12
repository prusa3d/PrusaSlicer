
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

class WipeTowerPositioningKernel : public IKernel
{
public:
    WipeTowerPositioningKernel(
        std::unique_ptr<IKernel> child_kernel
    );

    double placement_fitness(const ArrangeItem& item, const Domain::Vec2crd& transl) const final;

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> remaining_items
    ) final;

    bool on_item_packed(ArrangeItem& itm) final;

private:
    std::unique_ptr<IKernel> m_child_kernel;
    std::optional<Domain::Vec2crd> m_packed_wipe_tower_position;
};
}
