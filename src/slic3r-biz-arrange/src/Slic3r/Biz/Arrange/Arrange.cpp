#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/Kernels/GravityKernel.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Arrange/Kernels/RectangleOverfitKernelWrapper.hpp"
#include "Slic3r/Biz/Arrange/Kernels/WipeTowerPositioningKernel.hpp"
#include "Slic3r/Biz/Arrange/Kernels/SVGDebugOutputKernelWrapper.hpp"
#include "Slic3r/Biz/Arrange/Kernels/TMArrangeKernel.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Arrange/Packer.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "Slic3r/Log.hpp"

#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Biz::Arrange {

#ifndef NDEBUG
constexpr bool use_debug_kernel{true};
#else
constexpr bool use_debug_kernel{false};
#endif

using Algorithms::BoundingBox::center;
using Algorithms::BoundingBox::construct;
using Algorithms::BoundingBox::merge;
using Algorithms::BoundingBox::sizes;
using Algorithms::BoundingBox::unscaled;
using Algorithms::Scaling::scaled;
using Arrange::ArbitraryShape;
using Arrange::ArrangeItem;
using Arrange::ArrangeResult;
using Arrange::CircleBed;
using Arrange::IBed;
using Arrange::InfiniteBed;
using Arrange::InputShape;
using Arrange::IrregularBed;
using Arrange::RectangleBed;
using Arrange::Settings;
using Domain::BoundingBox2crd;
using Domain::BoundingBox2d;
using Domain::BoundingBox3d;
using Domain::ExPolygon;
using Domain::ExPolygons;
using Domain::Index2;
using Domain::Points;
using Domain::Polygon;
using Domain::Polygons;
using Domain::SCALED_EPSILON;
using Domain::Transform3d;
using Domain::Vec2crd;
using Domain::Vec2d;
using Domain::Vec2ds;
using Domain::Vec3d;

namespace {
Domain::BoundingBox2crd get_extents(const std::vector<ArrangeItem>& items)
{
    ASSERT(!items.empty());

    BoundingBox2crd result{items.front().fixed_shape().bounding_box()};
    for (const ArrangeItem& item : std::span{items}.subspan(1)) {
        result = merge(result, item.fixed_shape().bounding_box());
    }
    return result;
}

BoundingBox2d get_occupied_segments_bb(
    const BoundingBox2d& bed_bounding_box,
    const Vec2d& occupied_segments_bb_sizes,
    const PivotPoint pivot_point
)
{
    switch (pivot_point) {
    case PivotPoint::Center: {
        return {
            Vec2d{center(bed_bounding_box) - occupied_segments_bb_sizes / 2.0},
            Vec2d{center(bed_bounding_box) + occupied_segments_bb_sizes / 2.0}
        };
    }
    case PivotPoint::BottomLeft: {
        return {bed_bounding_box.min, bed_bounding_box.min + occupied_segments_bb_sizes};
    }
    case PivotPoint::BottomRight: {
        const Vec2d right_bottom_point{bed_bounding_box.min + Vec2d{sizes(bed_bounding_box).x(), 0.0}};
        return {
            right_bottom_point - Vec2d{occupied_segments_bb_sizes.x(), 0.0},
            right_bottom_point + Vec2d{0.0, occupied_segments_bb_sizes.y()}
        };
    }
    case PivotPoint::TopLeft: {
        const Vec2d top_left_point{bed_bounding_box.min + Vec2d{0.0, sizes(bed_bounding_box).y()}};
        return {
            top_left_point - Vec2d{0.0, occupied_segments_bb_sizes.y()},
            top_left_point + Vec2d{occupied_segments_bb_sizes.x(), 0.0}
        };
    }
    case PivotPoint::TopRight: {
        return {bed_bounding_box.max - occupied_segments_bb_sizes, bed_bounding_box.max};
    }
    }
    PANIC("Unknown pivot point");
}

void align_pile(std::vector<ArrangeItem>& items, const RectangleBed& bed)
{
    if (items.empty()) {
        return;
    }

    const BoundingBox2d bed_bounding_box{unscaled<double>(bed.bounding_box())};
    BoundingBox2d pile_bounding_box{unscaled<double>(get_extents(items))};
    const Vec2d segment_size{
        sizes(bed_bounding_box).array() / Vec2d{bed.segments().x_count, bed.segments().y_count}.array()
    };

    const Domain::Index2 occupied_segments_count{
        std::min(
            static_cast<int>(std::ceil(sizes(pile_bounding_box).x() / segment_size.x())),
            static_cast<int>(bed.segments().x_count)
        ),
        std::min(
            static_cast<int>(std::ceil(sizes(pile_bounding_box).y() / segment_size.y())),
            static_cast<int>(bed.segments().y_count)
        )
    };

    const Vec2d occupied_segments_bb_sizes{
        occupied_segments_count[0] * segment_size.x(),
        occupied_segments_count[1] * segment_size.y(),
    };

    const BoundingBox2d occupied_segments_bb{
        get_occupied_segments_bb(bed_bounding_box, occupied_segments_bb_sizes, bed.pivot_point())
    };

    const Vec2d translation{center(occupied_segments_bb) - center(pile_bounding_box)};

    for (ArrangeItem& item : items) {
        item.set_translation(item.get_translation() + scaled(translation));
    }
}

double poly_area(const Points& pts)
{
    Polygon poly(pts);
    return std::abs(poly.area());
}

double distance_to(const Vec2crd& p1, const Vec2crd& p2)
{
    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();
    return std::sqrt(dx * dx + dy * dy);
}

std::optional<CircleBed> to_circle(const Vec2crd& center, const Points& points)
{
    std::vector<double> vertex_distances;
    double avg_dist{0};

    for (const Vec2crd& pt : points) {
        double distance = distance_to(center, pt);
        vertex_distances.push_back(distance);
        avg_dist += distance;
    }

    avg_dist /= vertex_distances.size();

    for (auto el : vertex_distances) {
        if (std::abs(el - avg_dist) > 10 * SCALED_EPSILON) {
            return std::nullopt;
        }
    }

    return CircleBed{center, avg_dist};
}

std::unique_ptr<IBed> guess_bed(const Points& bed_shape, const Settings& settings)
{
    ASSERT(bed_shape.size() > 2);

    const RectangleBed rectangle_bed{construct(bed_shape)};
    std::optional<CircleBed> circle_bed{to_circle(center(rectangle_bed.bounding_box()), bed_shape)};
    const double bed_shape_area{poly_area(bed_shape)};

    if ((1.0 - bed_shape_area / rectangle_bed.area()) < 1e-3) {
        ASSERT(settings.bed_pivot_point.has_value() == settings.bed_segments.has_value());
        if (settings.bed_pivot_point) {
            return std::make_unique<RectangleBed>(
                rectangle_bed.bounding_box(),
                *settings.bed_pivot_point,
                *settings.bed_segments
            );
        }
        return std::make_unique<RectangleBed>(rectangle_bed);
    }

    if (settings.bed_pivot_point) {
        SPDLOG_ERROR("Bed pivot is not supported for non-rectangular bed!");
    }
    if (!settings.bed_segments) {
        SPDLOG_ERROR("Bed segments are not supported for non-rectangular bed!");
    }

    if (circle_bed && (1.0 - bed_shape_area / circle_bed->area()) < 1e-2) {
        return std::make_unique<CircleBed>(std::move(*circle_bed));
    }
    return std::make_unique<IrregularBed>(ExPolygons{ExPolygon(bed_shape)});
}

} // namespace

std::vector<ArrangeItem> to_arrange_items(const std::vector<InputShape>& items, const Settings& settings)
{
    std::vector<ArrangeItem> result;
    for (const InputShape& shape : items) {
        result.push_back(ArrangeItem{shape, settings});
    }
    return result;
}

std::optional<ArrangeResult> arrange(
    const Domain::Points& bed_contour,
    std::vector<ArrangeItem>& items,
    const std::vector<ArrangeItem>& fixed_items,
    const Settings& settings,
    StopCondition stop_condition
)
{
    const std::unique_ptr<IBed> bed{guess_bed(bed_contour, settings)};
    if (settings.allow_rotations) {
        for (ArrangeItem& item : items) {
            item.allow_rotations(*bed);
        }
    }

    std::unique_ptr<Kernels::IKernel> base_kernel;
    if (dynamic_cast<const CircleBed*>(bed.get()) != nullptr)
    {
        base_kernel = std::make_unique<Kernels::GravityKernel>();
    } else {
        base_kernel = std::make_unique<Kernels::TMArrangeKernel>(items.size(), bed->area());
    }

    std::unique_ptr<Kernels::IKernel> final_kernel;
    const InfiniteBed infinite_bed{center(bed->bounding_box())};
    const IBed* final_bed{nullptr};

    const bool use_overfit{
        settings.strategy == Strategy::Overfit
        && dynamic_cast<const RectangleBed*>(bed.get()) != nullptr
        && fixed_items.empty()
    };

    if (use_overfit) {
        final_kernel = std::make_unique<Kernels::RectangleOverfitKernelWrapper>(
            std::move(base_kernel),
            bed->bounding_box()
        );
        final_bed = &infinite_bed;
    } else {
        final_kernel = std::move(base_kernel);
        final_bed    = bed.get();
    }

    const auto wipe_tower_it{std::ranges::find_if(items, [](const ArrangeItem& item){
        return item.is_wipe_tower;
    })};

    if (use_overfit && wipe_tower_it != items.end()) {
        final_kernel =
            std::make_unique<Kernels::WipeTowerPositioningKernel>(std::move(final_kernel));
    }

    if constexpr (use_debug_kernel) {
        final_kernel = std::make_unique<Kernels::SVGDebugOutputKernelWrapper>(
            bed->bounding_box(),
            std::move(final_kernel)
        );
    }

    constexpr double accuracy{1.0};
    Packer packer{
        std::move(final_kernel),
        accuracy,
        Algorithms::Optimize::Optimizer<Algorithms::Optimize::AlgNLoptSubplex>{},
        stop_condition
    };

    ArrangeResult result;
    PackingContext context;
    context.add_fixed_items(fixed_items);
    for (std::size_t i{}; i < items.size(); ++i) {
        ArrangeItem& item{items[i]};
        const bool packed{packer.pack(*final_bed, item, context, std::span{items}.subspan(i + 1))};
        if (packed) {
            context.add_packed_item(item);
            result.packed.push_back(item);
        } else {
            if (stop_condition()) {
                return std::nullopt;
            }
            result.not_packed.push_back(item);
        }
    }

    if (use_overfit) {
        align_pile(result.packed, dynamic_cast<const RectangleBed&>(*bed));
    }
    return result;
}

InstanceTransforms arrange_instances(
    const std::vector<const Domain::ModelInstance*>& instances,
    const Points& bed_contour_scaled,
    const Settings& settings
)
{
    std::vector<InputShape> input_shapes;
    for (const Domain::ModelInstance* instance : instances) {
        Domain::Transform3d instance_trafo{instance->get_matrix()};
        instance_trafo.translation() = Vec3d::Zero();

        ArbitraryShape outline;
        for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
            if (!volume->is_model_part()) {
                continue;
            }
            const Domain::Polygons vol_outline{
                project_mesh(volume->mesh_ptr()->its, instance_trafo * volume->get_matrix(), []() {})
            };
            outline = Algorithms::ClipperUtils::union_ex(outline, vol_outline);
        }

        const Domain::ElementRef instance_ref{
            instance->get_object()->id().id,
            instance->id().id
        };

        input_shapes.push_back({instance_ref, std::move(outline)});
    }

    std::vector<ArrangeItem> items{to_arrange_items(input_shapes, settings)};
    const StopCondition never_stop{[]() { return false; }};

    std::optional<ArrangeResult> result{
        arrange(bed_contour_scaled, items, {}, settings, never_stop)
    };

    InstanceTransforms transforms;
    if (!result) {
        return transforms;
    }

    transforms.reserve(result->packed.size());
    for (const ArrangeItem& item : result->packed) {
        transforms.push_back({
            .instance_ref    = item.get_element_ref(),
            .absolute_offset = Algorithms::Scaling::unscaled<double>(item.get_translation()),
            .rotation_delta  = item.get_rotation()
        });
    }

    return transforms;
}



void arrange_model_in_place(Domain::Model& model, const Points& bed_contour_scaled, const Settings& arrange_settings)
{
    std::vector<const Domain::ModelInstance*> instances;
    for (const Domain::ModelObject* object : model.objects) {
        for (const Domain::ModelInstance* instance : object->instances) {
            instances.push_back(instance);
        }
    }
    const auto transforms = arrange_instances(instances, bed_contour_scaled, arrange_settings);
    for (Domain::ModelObject* object : model.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            if (auto it = std::find_if(transforms.cbegin(), transforms.cend(), [instance](const auto& trafo) {
                return instance->get_object()->id().id == trafo.instance_ref.object_id
                    && instance->id().id == trafo.instance_ref.instance_id;
            }); it != transforms.cend()) {
                Domain::Transform3d m{instance->get_transformation().get_matrix()};
                m.translation().x() = it->absolute_offset.x();
                m.translation().y() = it->absolute_offset.y();
                auto rot{Domain::Transform3d::Identity()};
                rot.rotate(Eigen::AngleAxisd(it->rotation_delta, Eigen::Vector3d::UnitZ()));
                instance->set_transformation(Domain::Transformation{m * rot});
            }
        }
    }
}

} // namespace Slic3r::Biz::Arrange
