#include <cfloat>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>

#include "libslic3r/ExtruderCandidates.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "admesh/stl.h"
#include "libslic3r/Geometry.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/SlicingInput.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/StepsInvalidation.hpp"
#include "libslic3r/ShrinkageCompensation.hpp"
#include "Slic3r/Biz/Parser/IO.hpp"

namespace Slic3r {

namespace CustomGCode = Domain::CustomGCode;

using Biz::Parser::PlaceholderParser;
using Domain::FullConfigFDMPtr;
using Domain::ModelWipeTower;
using Domain::ObjectID;
using Domain::PartialObjectConfigFDMPtr;
using Domain::PartialVolumeConfigFDM;
using Domain::PartialVolumeConfigFDMPtr;
using Domain::VolumeSettings;
using SlicingSync::AllOrSome;
using SlicingSync::AllSteps;
using SlicingSync::get_invalidated_steps;
using SlicingSync::InvalidatedSteps;
using SlicingSync::PrintAndObjectSteps;
using SlicingSync::StepsPerPrintObject;
using ParserConfig = Biz::Parser::IO::Config;
using Errors       = std::vector<Biz::Slicing::Error>;
using Biz::Slicing::get_painting_extruders;
using Domain::VirtualExtruder;
using Domain::VirtualExtruderComponent;
using Domain::VirtualExtruderGradientStop;
using Domain::VirtualExtruders;

static inline bool transform3d_lower(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv) {
        if (*lv < *rv)
            return true;
        else if (*lv > *rv)
            return false;
    }
    return false;
}

struct PrintObjectTrafoAndInstances
{
    Transform3d    	trafo;
    PrintInstances	instances;
    bool operator<(const PrintObjectTrafoAndInstances &rhs) const { return transform3d_lower(this->trafo, rhs.trafo); }
};

// Generate a list of trafos and XY offsets for instances of a ModelObject
static std::vector<PrintObjectTrafoAndInstances> print_objects_from_model_object(const Domain::ModelInstancePtrs &instances, const Vec3d &shrinkage_compensation)
{
    using namespace Slic3r::Biz;

    std::set<PrintObjectTrafoAndInstances> trafos;
    PrintObjectTrafoAndInstances           trafo;
    for (std::size_t index{}; index < instances.size(); ++index) {
        const Domain::ModelInstance& model_instance{*instances[index]};
        Geometry::Transformation model_instance_transformation = model_instance.get_transformation();
        trafo.trafo = model_instance_transformation.get_matrix_with_applied_shrinkage_compensation(shrinkage_compensation);
        Domain::Vec2big shift = Algorithms::Scaling::scaled<int64_t>(
            Vec2d(trafo.trafo.data()[12], trafo.trafo.data()[13])
        );
        // Reset the XY axes of the transformation.
        trafo.trafo.data()[12] = 0;
        trafo.trafo.data()[13] = 0;
        // Search or insert a trafo.
        auto it = trafos.emplace(trafo).first;
        const_cast<PrintObjectTrafoAndInstances&>(*it)
            .instances.emplace_back(model_instance, index, shift);
    }
    return std::vector<PrintObjectTrafoAndInstances>(trafos.begin(), trafos.end());
}

// Compare just the layer ranges and their layer heights, not the associated configs.
// Ignore the layer heights if check_layer_heights is false.
static bool layer_height_ranges_equal(const Domain::LayerConfigRanges &lr1, const Domain::LayerConfigRanges &lr2, bool check_layer_height)
{
    if (lr1.size() != lr2.size())
        return false;
    auto it2 = lr2.begin();
    for (const auto &kvp1 : lr1) {
        const auto &kvp2 = *it2 ++;
        if (std::abs(kvp1.first.first  - kvp2.first.first ) > EPSILON ||
            std::abs(kvp1.first.second - kvp2.first.second) > EPSILON ||
            (check_layer_height && std::abs(kvp1.second.find("layer_height").item->get<double>() - kvp2.second.find("layer_height").item->get<double>()) > EPSILON))
            return false;
    }
    return true;
}

// Returns true if va == vb when all CustomGCode items that are not the specified type (not_ignore_type) are ignored.
static bool custom_per_printz_gcodes_tool_changes_differ(
    const std::vector<CustomGCode::Item> &va,
    const std::vector<CustomGCode::Item> &vb,
    CustomGCode::Type                     not_ignore_type
) {
	auto it_a = va.begin();
	auto it_b = vb.begin();
	while (it_a != va.end() || it_b != vb.end()) {
		if (it_a != va.end() && it_a->type != not_ignore_type) {
			// Skip any CustomGCode items, which are not equal to not_ignore_type.
			++ it_a;
			continue;
		}
		if (it_b != vb.end() && it_b->type != not_ignore_type) {
			// Skip any CustomGCode items, which are not equal to not_ignore_type.
			++ it_b;
			continue;
		}
		if (it_a == va.end() || it_b == vb.end())
			// va or vb contains more items of not_ignore_type than the other.
			return true;
		assert(it_a->type == not_ignore_type);
		assert(it_b->type == not_ignore_type);
		if (*it_a != *it_b)
			// The two items of not_ignore_type differ.
			return true;
		++ it_a;
		++ it_b;
	}
	// There is no change in specified not_ignore_type items.
	return false;
}

// Repository for solving partial overlaps of ModelObject::layer_config_ranges.
// Here the const DynamicPrintConfig* point to the config in ModelObject::layer_config_ranges.
class LayerRanges
{
public:
    struct LayerRange {
        Domain::LayerHeightRange  layer_height_range;
        // Config is owned by the associated ModelObject.
        PartialVolumeConfigFDMPtr config;

        bool operator<(const LayerRange &rhs) const throw() { return this->layer_height_range < rhs.layer_height_range; }
    };

    LayerRanges() = default;
    LayerRanges(
        const Domain::LayerConfigRanges& in,
        const Domain::Preset::HwPrinterConfig& hw_config
    )
    {
        this->assign(in, hw_config);
    }

    // Convert input config ranges into continuous non-overlapping sorted vector of intervals and their configs.
    void assign(
        const Domain::LayerConfigRanges& in,
        const Domain::Preset::HwPrinterConfig& hw_config
    )
    {
        m_ranges.clear();
        m_ranges.reserve(in.size());
        // Input ranges are sorted lexicographically. First range trims the other ranges.
        double last_z = 0;
        for (const std::pair<const Domain::LayerHeightRange, Domain::VolumeSettings> &range : in)
            if (range.first.second > last_z) {
                double min_z = std::max(range.first.first, 0.);
                if (min_z > last_z + EPSILON) {
                    m_ranges.push_back({ Domain::LayerHeightRange(last_z, min_z) });
                    last_z = min_z;
                }
                if (range.first.second > last_z + EPSILON) {
                    const auto cfg{prepare_slicing_volume_input(range.second, hw_config, hw_config.material_slot_count())};
                    m_ranges.push_back(
                        {Domain::LayerHeightRange(last_z, range.first.second), cfg.value()}
                    );
                    last_z = range.first.second;
                }
            }
        if (m_ranges.empty())
            m_ranges.push_back({ Domain::LayerHeightRange(0, DBL_MAX) });
        else if (m_ranges.back().config == nullptr)
            m_ranges.back().layer_height_range.second = DBL_MAX;
        else
            m_ranges.push_back({ Domain::LayerHeightRange(m_ranges.back().layer_height_range.second, DBL_MAX) });
    }

    PartialVolumeConfigFDMPtr config(const Domain::LayerHeightRange &range) const {
        auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), LayerRange{ { range.first - EPSILON, range.second - EPSILON } });
        // #ys_FIXME_COLOR
        // assert(it != m_ranges.end());
        // assert(it == m_ranges.end() || std::abs(it->first.first  - range.first ) < EPSILON);
        // assert(it == m_ranges.end() || std::abs(it->first.second - range.second) < EPSILON);
        if (it == m_ranges.end() ||
            std::abs(it->layer_height_range.first - range.first) > EPSILON ||
            std::abs(it->layer_height_range.second - range.second) > EPSILON )
            return nullptr; // desired range doesn't found
        return it == m_ranges.end() ? nullptr : it->config;
    }

    std::vector<LayerRange>::const_iterator begin() const { return m_ranges.cbegin(); }
    std::vector<LayerRange>::const_iterator end  () const { return m_ranges.cend(); }
    size_t                                  size () const { return m_ranges.size(); }

private:
    // Layer ranges with their config overrides and list of volumes with their snug bounding boxes in a given layer range.
    std::vector<LayerRange>  m_ranges;
};

static inline bool model_volume_solid_or_modifier(const Domain::ModelVolume &mv)
{
    Domain::ModelVolumeType type = mv.type();
    return type == Domain::ModelVolumeType::MODEL_PART || type == Domain::ModelVolumeType::NEGATIVE_VOLUME || type == Domain::ModelVolumeType::PARAMETER_MODIFIER;
}

static inline Transform3f trafo_for_bbox(const Transform3d &object_trafo, const Transform3d &volume_trafo)
{
    ASSERT(is_approx(object_trafo.translation().x(), 0.));
    ASSERT(is_approx(object_trafo.translation().y(), 0.));
    return (object_trafo * volume_trafo).cast<float>();
}

static PrintObjectRegions::BoundingBox transformed_its_bbox2d(const indexed_triangle_set &its, const Transform3f &m, float offset)
{
    assert(! its.indices.empty());

    PrintObjectRegions::BoundingBox bbox(m * its.vertices[its.indices.front()[0]]);
    for (const stl_triangle_vertex_indices &tri : its.indices)
        for (int i = 0; i < 3; ++ i)
            bbox.extend(m * its.vertices[tri[i]]);
    bbox.min() -= Vec3f(offset, offset, float(EPSILON));
    bbox.max() += Vec3f(offset, offset, float(EPSILON));
    return bbox;
}

static void transformed_its_bboxes_in_z_ranges(
    const indexed_triangle_set                                    &its, 
    const Transform3f                                             &m,
    const std::vector<Domain::LayerHeightRange>                   &z_ranges,
    std::vector<std::pair<PrintObjectRegions::BoundingBox, bool>> &bboxes,
    const float                                                    offset)
{
    bboxes.assign(z_ranges.size(), std::make_pair(PrintObjectRegions::BoundingBox(), false));
    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { m * its.vertices[tri[0]], m * its.vertices[tri[1]], m * its.vertices[tri[2]] };
        for (size_t irange = 0; irange < z_ranges.size(); ++ irange) {
            const Domain::LayerHeightRange                   &z_range = z_ranges[irange];
            std::pair<PrintObjectRegions::BoundingBox, bool> &bbox    = bboxes[irange];
            auto bbox_extend = [&bbox](const Vec3f& p) {
                if (bbox.second) {
                    bbox.first.extend(p);
                } else {
                    bbox.first.min() = bbox.first.max() = p;
                    bbox.second = true;
                }
            };
            int iprev = 2;
            for (int iedge = 0; iedge < 3; ++ iedge) {
                const Vec3f *p1 = &pts[iprev];
                const Vec3f *p2 = &pts[iedge];
                // Sort the edge points by Z.
                if (p1->z() > p2->z())
                    std::swap(p1, p2);
                if (p2->z() <= z_range.first || p1->z() >= z_range.second) {
                    // Out of this slab.
                } else if (p1->z() < z_range.first) {
                    if (p1->z() > z_range.second) {
                        // Two intersections.
                        float zspan = p2->z() - p1->z();
                        float t1 = (z_range.first - p1->z())  / zspan;
                        float t2 = (z_range.second - p1->z()) / zspan;
                        Vec2f p = to_2d(*p1);
                        Vec2f v(p2->x() - p1->x(), p2->y() - p1->y());
                        bbox_extend(to_3d((p + v * t1).eval(), float(z_range.first)));
                        bbox_extend(to_3d((p + v * t2).eval(), float(z_range.second)));
                    } else {
                        // Single intersection with the lower limit.
                        float t = (z_range.first - p1->z()) / (p2->z() - p1->z());
                        Vec2f v(p2->x() - p1->x(), p2->y() - p1->y());
                        bbox_extend(to_3d((to_2d(*p1) + v * t).eval(), float(z_range.first)));
                        bbox_extend(*p2);
                    }
                } else if (p2->z() > z_range.second) {
                    // Single intersection with the upper limit.
                    float t = (z_range.second - p1->z()) / (p2->z() - p1->z());
                    Vec2f v(p2->x() - p1->x(), p2->y() - p1->y());
                    bbox_extend(to_3d((to_2d(*p1) + v * t).eval(), float(z_range.second)));
                    bbox_extend(*p1);
                } else {
                    // Both points are inside.
                    bbox_extend(*p1);
                    bbox_extend(*p2);
                }
                iprev = iedge;
            }
        }
    }

    for (std::pair<PrintObjectRegions::BoundingBox, bool> &bbox : bboxes) {
        bbox.first.min() -= Vec3f(offset, offset, float(EPSILON));
        bbox.first.max() += Vec3f(offset, offset, float(EPSILON));
    }
}

// Find a bounding box of a volume's part intersecting layer_range. Such a bounding box will likely be smaller in XY than the full bounding box,
// thus it will intersect with lower number of other volumes.
const PrintObjectRegions::BoundingBox* find_volume_extents(const PrintObjectRegions::LayerRangeRegions &layer_range, const Domain::ModelVolume &volume)
{
    auto it = lower_bound_by_predicate(layer_range.volumes.begin(), layer_range.volumes.end(), [&volume](const PrintObjectRegions::VolumeExtents &l){ return l.volume_id < volume.id(); });
    return it != layer_range.volumes.end() && it->volume_id == volume.id() ? &it->bbox : nullptr;
}

// Find a bounding box of a topmost printable volume referenced by this modifier given this_region_id.
PrintObjectRegions::BoundingBox find_modifier_volume_extents(const PrintObjectRegions::LayerRangeRegions &layer_range, const int this_region_id)
{
    // Find the top-most printable volume of this modifier, or the printable volume itself.
    const PrintObjectRegions::VolumeRegion &this_region = layer_range.volume_regions[this_region_id];
    const PrintObjectRegions::BoundingBox *this_extents = find_volume_extents(layer_range, *this_region.model_volume);
    assert(this_extents);
    PrintObjectRegions::BoundingBox out { *this_extents };
    if (! this_region.model_volume->is_model_part())
        for (int parent_region_id = this_region.parent;;) {
            assert(parent_region_id >= 0);
            const PrintObjectRegions::VolumeRegion &parent_region  = layer_range.volume_regions[parent_region_id];
            const PrintObjectRegions::BoundingBox  *parent_extents = find_volume_extents(layer_range, *parent_region.model_volume);
            assert(parent_extents);
            out.clamp(*parent_extents);
            assert(! out.isEmpty());
            if (parent_region.model_volume->is_model_part())
                break;
            parent_region_id = parent_region.parent;
        }
    return out;
}

// Update caches of volume bounding boxes.
void update_volume_bboxes(
    std::vector<PrintObjectRegions::LayerRangeRegions>  &layer_ranges,
    std::vector<Domain::ObjectID>                       &cached_volume_ids,
    Domain::ModelVolumePtrs                              model_volumes,
    const Transform3d                                   &object_trafo, 
    const float                                          offset)
{
    // output will be sorted by the order of model_volumes sorted by their ObjectIDs.
    model_volumes_sort_by_id(model_volumes);

    if (layer_ranges.size() == 1) {
        PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front();
        std::vector<PrintObjectRegions::VolumeExtents> volumes_old(std::move(layer_range.volumes));
        layer_range.volumes.reserve(model_volumes.size());
        for (const Domain::ModelVolume *model_volume : model_volumes)
            if (model_volume_solid_or_modifier(*model_volume)) {
                if (std::binary_search(cached_volume_ids.begin(), cached_volume_ids.end(), model_volume->id())) {
                    auto it = lower_bound_by_predicate(volumes_old.begin(), volumes_old.end(), [model_volume](PrintObjectRegions::VolumeExtents &l) { return l.volume_id < model_volume->id(); });
                    if (it != volumes_old.end() && it->volume_id == model_volume->id())
                        layer_range.volumes.emplace_back(*it);
                } else {
                    const auto ch                    = model_volume->get_convex_hull_shared_ptr();
                    const Domain::TriangleMesh* mesh = (ch != nullptr && !ch->its.indices.empty()) ?
                        ch.get() :
                        &model_volume->mesh();
                    layer_range.volumes.push_back({ model_volume->id(),
                        transformed_its_bbox2d(mesh->its, trafo_for_bbox(object_trafo, model_volume->get_matrix()), offset) });
                }
            }
    } else {
        std::vector<std::vector<PrintObjectRegions::VolumeExtents>> volumes_old;
        if (cached_volume_ids.empty())
            for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                layer_range.volumes.clear();
        else {
            volumes_old.reserve(layer_ranges.size());
            for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                volumes_old.emplace_back(std::move(layer_range.volumes));
        }

        std::vector<std::pair<PrintObjectRegions::BoundingBox, bool>> bboxes;
        std::vector<Domain::LayerHeightRange>                         ranges;
        ranges.reserve(layer_ranges.size());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges) {
            Domain::LayerHeightRange r = layer_range.layer_height_range;
            r.first  -= EPSILON;
            r.second += EPSILON;
            ranges.emplace_back(r);
        }
        for (const Domain::ModelVolume *model_volume : model_volumes)
            if (model_volume_solid_or_modifier(*model_volume)) {
                if (std::binary_search(cached_volume_ids.begin(), cached_volume_ids.end(), model_volume->id())) {
                    for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges) {
                        const auto &vold = volumes_old[&layer_range - layer_ranges.data()];
                        auto it = lower_bound_by_predicate(vold.begin(), vold.end(), [model_volume](const PrintObjectRegions::VolumeExtents &l) { return l.volume_id < model_volume->id(); });
                        if (it != vold.end() && it->volume_id == model_volume->id())
                            layer_range.volumes.emplace_back(*it);
                    }
                } else {
                    transformed_its_bboxes_in_z_ranges(model_volume->mesh().its, trafo_for_bbox(object_trafo, model_volume->get_matrix()), ranges, bboxes, offset);
                    for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                        if (auto &bbox = bboxes[&layer_range - layer_ranges.data()]; bbox.second)
                            layer_range.volumes.push_back({ model_volume->id(), bbox.first });
                }
            }
    }

    cached_volume_ids.clear();
    cached_volume_ids.reserve(model_volumes.size());
    for (const Domain::ModelVolume *v : model_volumes)
        if (model_volume_solid_or_modifier(*v))
            cached_volume_ids.emplace_back(v->id());
}

Domain::ModelInstancePtrs Print::deep_copy_instances(const Domain::ModelInstancePtrs& instances, Domain::ModelObject* model_object)
{
    Domain::ModelInstancePtrs result;
    for (Domain::ModelInstance* model_instance : instances) {
        result.emplace_back(new Domain::ModelInstance(*model_instance));
        result.back()->set_model_object(model_object);
    }
    return result;
}

bool Print::invalidate_object_steps(
    const InvalidatedSteps& steps
) {
    bool invalidated{false};

    if (std::holds_alternative<FDMPrintSteps>(steps.print)) {
        const auto print_steps{std::get<FDMPrintSteps>(steps.print)};
        for (const FDMPrintStep& step : print_steps) {
            if (this->invalidate_step(step)) {
                invalidated = true;
            }
        }
    } else {
        if (this->invalidate_all_steps()) {
            invalidated = true;
        }
    }

    const std::set<PrintObject*> existing_objects{m_objects.begin(), m_objects.end()};
    for (const auto& [print_object, invalidated_steps] : steps.object) {
        if (!existing_objects.contains(print_object)) {
            continue;
        }
        if (std::holds_alternative<FDMPrintObjectSteps>(invalidated_steps)) {
            const auto object_steps{std::get<FDMPrintObjectSteps>(invalidated_steps)};
            for (const FDMPrintObjectStep& step : object_steps) {
                if (print_object->invalidate_step(step)) {
                    invalidated = true;
                }
            }
        } else {
            if (print_object->invalidate_all_steps()) {
                invalidated = true;
            }
        }
    }

    return invalidated;
}


namespace {

PrintRegionConfigView create_mm_painted_region_config(
    const PrintRegionConfigView& parent_config,
    const int painted_extruder_id
)
{
    Domain::VolumeSettings volume_settings;
    volume_settings.overrides.set("infill_extruder", painted_extruder_id);
    volume_settings.overrides.set("perimeter_extruder", painted_extruder_id);
    volume_settings.overrides.set("solid_infill_extruder", painted_extruder_id);

    PrintRegionConfigView painted_region_cfg = parent_config;
    const PartialVolumeConfigFDM squashed_volume_config{
        volume_settings,
        parent_config.hw_config()
    };
    painted_region_cfg.add_override(
        std::make_shared<PartialVolumeConfigFDM>(squashed_volume_config)
    );

    return painted_region_cfg;
};

PrintRegionConfigView create_fuzzy_skin_painted_region_config(
    const PrintRegionConfigView& parent_config
)
{
    Domain::VolumeSettings volume_settings;
    volume_settings.overrides.set("fuzzy_skin", Domain::FuzzySkinType::All);

    PrintRegionConfigView painted_region_cfg = parent_config;
    const PartialVolumeConfigFDM squashed_volume_config{
        volume_settings,
        parent_config.hw_config()
    };
    painted_region_cfg.add_override(
        std::make_shared<PartialVolumeConfigFDM>(squashed_volume_config)
    );

    return painted_region_cfg;
};

// Generate PrintRegions from scratch.
tl::expected<std::shared_ptr<PrintObjectRegions>, Errors>
generate_print_object_regions(
    std::shared_ptr<PrintObjectRegions> print_object_regions_old,
    const Domain::ModelVolumePtrs& model_volumes,
    const LayerRanges& model_layer_ranges,
    const PartialObjectConfigFDMPtr& new_object_settings,
    const FullConfigFDMPtr& new_full_config,
    const Transform3d& trafo,
    size_t num_physical_extruders,
    const VirtualExtruders& virtual_extruders,
    const float xy_size_compensation,
    const std::vector<unsigned int>& painting_extruders,
    const bool has_painted_fuzzy_skin
)
{
    // Reuse the old object or generate a new one.
    auto out = std::make_shared<PrintObjectRegions>();
    auto &all_regions          = out->all_regions;
    auto &layer_ranges_regions = out->layer_ranges;

    all_regions.clear();

    out->trafo_bboxes = trafo;
    layer_ranges_regions.reserve(model_layer_ranges.size());
    for (const auto &range : model_layer_ranges)
        layer_ranges_regions.push_back({ range.layer_height_range, range.config });

    const bool is_mm_painted = num_physical_extruders > 1 && std::any_of(model_volumes.cbegin(), model_volumes.cend(), [](const Domain::ModelVolume *mv) { return mv->is_mm_painted(); });
    update_volume_bboxes(layer_ranges_regions, out->cached_volume_ids, model_volumes, out->trafo_bboxes, is_mm_painted ? 0.f : std::max(0.f, xy_size_compensation));

    std::vector<PrintRegion*> region_set;
    auto get_create_region = [&region_set, &all_regions](
        const PrintRegionConfigView &config,
        std::optional<unsigned int> source_virtual = std::nullopt) -> PrintRegion* {
        size_t hash = config.hash();
        auto it     = Slic3r::lower_bound_by_predicate(
            region_set.begin(),
            region_set.end(),
            [hash, source_virtual](const PrintRegion* l)
            {
                if (l->config_hash() != hash) {
                    return l->config_hash() < hash;
                }

                return l->source_virtual_extruder_id() < source_virtual;
            }
        );

        if (it != region_set.end()
            && (*it)->config_hash() == hash
            && (*it)->config() == config
            && (*it)->source_virtual_extruder_id() == source_virtual)
        {
            return *it;
        }

        // Insert into a sorted array, it has O(n) complexity, but the calling algorithm has an O(n^2*log(n)) complexity anyways.
        all_regions.emplace_back(std::make_unique<PrintRegion>(std::move(config), hash, int(all_regions.size())));
        PrintRegion *region = all_regions.back().get();
        region->set_source_virtual_extruder_id(source_virtual);
        region_set.emplace(it, region);
        return region;
    };

    // Chain the regions in the order they are stored in the volumes list.
    for (int volume_id = 0; volume_id < int(model_volumes.size()); ++ volume_id) {
        const Domain::ModelVolume &volume = *model_volumes[volume_id];
        if (model_volume_solid_or_modifier(volume)) {
            for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges_regions)
                if (const PrintObjectRegions::BoundingBox *bbox = find_volume_extents(layer_range, volume); bbox) {
                    if (volume.is_model_part()) {
                        std::vector<PartialVolumeConfigFDMPtr> new_volume_settings;
                        if (layer_range.config) {
                            new_volume_settings.push_back(std::make_shared<const PartialVolumeConfigFDM>(*layer_range.config));
                        }
                        const auto squashed_settings{prepare_slicing_volume_input(
                            volume.volume_settings,
                            new_full_config->hw_config(),
                            new_full_config->hw_config().material_slot_count()
                        )};
                        if (!squashed_settings.has_value()) {
                            return tl::unexpected{squashed_settings.error()};
                        }
                        new_volume_settings.push_back(squashed_settings.value());

                        const PrintRegionConfigView new_config{
                            new_full_config,
                            new_object_settings,
                            new_volume_settings
                        };

                        // Add a model volume, assign an existing region or generate a new one.
                        layer_range.volume_regions.push_back({
                            &volume, -1,
                            get_create_region(new_config, new_config.source_virtual_extruder()),
                            bbox
                        });
                    } else if (volume.is_negative_volume()) {
                        // Add a negative (subtractor) volume. Such volume has neither region nor parent volume assigned.
                        layer_range.volume_regions.push_back({ &volume, -1, nullptr, bbox });
                    } else {
                        assert(volume.is_modifier());
                        // Modifiers may be chained one over the other. Check for overlap, merge DynamicPrintConfigs.
                        bool added = false;
                        int  parent_model_part_id = -1;
                        for (int parent_region_id = int(layer_range.volume_regions.size()) - 1; parent_region_id >= 0; -- parent_region_id) {
                            const PrintObjectRegions::VolumeRegion &parent_region = layer_range.volume_regions[parent_region_id];
                            const Domain::ModelVolume              &parent_volume = *parent_region.model_volume;
                            if (parent_volume.is_model_part() || parent_volume.is_modifier())
                                if (PrintObjectRegions::BoundingBox parent_bbox = find_modifier_volume_extents(layer_range, parent_region_id); parent_bbox.intersects(*bbox)) {
                                    PrintRegionConfigView new_config{parent_region.region->config()};

                                    const auto squashed_settings{
                                        prepare_slicing_volume_input(
                                            volume.volume_settings,
                                            new_full_config->hw_config(),
                                            new_full_config->hw_config().material_slot_count()
                                        )
                                    };
                                    if (!squashed_settings.has_value()) {
                                        return tl::unexpected{squashed_settings.error()};
                                    }
                                    new_config.add_override(squashed_settings.value());
                                    // Only create new region for a modifier, which actually modifies config of it's parent.
                                    if (new_config != parent_region.region->config()) {
                                        added = true;
                                        layer_range.volume_regions.push_back({ &volume, parent_region_id, get_create_region(new_config, new_config.source_virtual_extruder()), bbox });
                                    } else if (parent_model_part_id == -1 && parent_volume.is_model_part())
                                        parent_model_part_id = parent_region_id;
                                }
                        }
                        if (! added && parent_model_part_id >= 0)
                            // This modifier does not override any printable volume's configuration, however it may in the future.
                            // Store it so that verify_update_print_object_regions() will handle this modifier correctly if its configuration changes.
                            layer_range.volume_regions.push_back({ &volume, parent_model_part_id, layer_range.volume_regions[parent_model_part_id].region, bbox });
                    }
                }
            }
    }

    // Create PaintedRegions for each physical extruder a virtual extruder can
    // resolve to. Both MM segmentation and remap_virtual_region_slices_to_physical
    // need these target regions to exist.
    for (PrintObjectRegions::LayerRangeRegions& layer_range : layer_ranges_regions) {
        const int num_volume_regions = int(layer_range.volume_regions.size());
        for (int volume_region_id = 0; volume_region_id < num_volume_regions; ++volume_region_id) {
            const PrintObjectRegions::VolumeRegion& volume_region =
                layer_range.volume_regions[volume_region_id];
            if (!volume_region.model_volume->is_model_part()
                && !volume_region.model_volume->is_modifier())
            {
                continue;
            }

            const std::optional<unsigned int> source_virtual =
                volume_region.region->source_virtual_extruder_id();
            if (!source_virtual.has_value()) {
                continue;
            }

            const unsigned int virtual_extruder_id = *source_virtual;
            // Find the matching virtual extruder definition.
            for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
                if (virtual_extruder.id != virtual_extruder_id) {
                    continue;
                }

                std::set<unsigned int> distinct_physical_ids;
                if (virtual_extruder.gradient.has_value()) {
                    for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops)
                    {
                        distinct_physical_ids.insert(stop.extruder_id);
                    }
                } else {
                    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
                        distinct_physical_ids.insert(component.extruder_id);
                    }
                }

                for (unsigned int physical_id : distinct_physical_ids) {
                    assert(physical_id >= 1 && physical_id <= num_physical_extruders);
                    PrintRegionConfigView painted_region_cfg =
                        create_mm_painted_region_config(volume_region.region->config(), static_cast<int>(physical_id));
                    layer_range.painted_regions.push_back(
                        {physical_id, volume_region_id, get_create_region(painted_region_cfg)}
                    );
                }

                break;
            }
        }
    }

    // Finally add painting regions.
    for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges_regions) {
        for (unsigned int painted_extruder_id : painting_extruders)
            for (int parent_region_id = 0; parent_region_id < int(layer_range.volume_regions.size()); ++ parent_region_id)
                if (const PrintObjectRegions::VolumeRegion &parent_region = layer_range.volume_regions[parent_region_id]; parent_region.model_volume->is_model_part() || parent_region.model_volume->is_modifier()) {
                    // The virtual extruder expansion above may have already created the target region for this pair.
                    const auto it_painted_region = std::ranges::find_if(
                        layer_range.painted_regions,
                        [&](const PrintObjectRegions::PaintedRegion& painted_region)
                        {
                            return painted_region.parent == parent_region_id
                                && painted_region.extruder_id == painted_extruder_id;
                        }
                    );
                    PrintRegion* target_region =
                        it_painted_region != layer_range.painted_regions.end() ?
                        it_painted_region->region :
                        get_create_region(create_mm_painted_region_config(
                            parent_region.region->config(),
                            static_cast<int>(painted_extruder_id)
                        ));

                    layer_range.painted_regions.push_back(
                        {painted_extruder_id, parent_region_id, target_region}
                    );
                }
        // Sort the regions by parent region::print_object_region_id() and extruder_id to help the slicing algorithm when applying MM segmentation.
        std::sort(layer_range.painted_regions.begin(), layer_range.painted_regions.end(), [&layer_range](auto &l, auto &r) {
            int lid = layer_range.volume_regions[l.parent].region->print_object_region_id();
            int rid = layer_range.volume_regions[r.parent].region->print_object_region_id();
            return lid < rid || (lid == rid && l.extruder_id < r.extruder_id); });
    }

    if (has_painted_fuzzy_skin) {
        using FuzzySkinParentType = PrintObjectRegions::FuzzySkinPaintedRegion::ParentType;

        for (PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges_regions) {
            // FuzzySkinPaintedRegion can override different parts of the Layer than PaintedRegions,
            // so FuzzySkinPaintedRegion has to point to both VolumeRegion and PaintedRegion.
            for (int parent_volume_region_id = 0; parent_volume_region_id < int(layer_range.volume_regions.size()); ++parent_volume_region_id) {
                if (const PrintObjectRegions::VolumeRegion &parent_volume_region = layer_range.volume_regions[parent_volume_region_id]; parent_volume_region.model_volume->is_model_part() || parent_volume_region.model_volume->is_modifier()) {
                    const PrintRegionConfigView painted_region_cfg = create_fuzzy_skin_painted_region_config(parent_volume_region.region->config());
                    layer_range.fuzzy_skin_painted_regions.push_back({FuzzySkinParentType::VolumeRegion, parent_volume_region_id, get_create_region(painted_region_cfg)});
                }
            }

            for (int parent_painted_regions_id = 0; parent_painted_regions_id < int(layer_range.painted_regions.size()); ++parent_painted_regions_id) {
                const PrintObjectRegions::PaintedRegion &parent_painted_region = layer_range.painted_regions[parent_painted_regions_id];
                const PrintRegionConfigView painted_region_cfg = create_fuzzy_skin_painted_region_config(parent_painted_region.region->config());
                layer_range.fuzzy_skin_painted_regions.push_back({FuzzySkinParentType::PaintedRegion, parent_painted_regions_id, get_create_region(painted_region_cfg)});
            }

            // Sort the regions by parent region::print_object_region_id() to help the slicing algorithm when applying fuzzy skin segmentation.
            std::sort(layer_range.fuzzy_skin_painted_regions.begin(), layer_range.fuzzy_skin_painted_regions.end(), [&layer_range](auto &l, auto &r) {
                return l.parent_print_object_region_id(layer_range) < r.parent_print_object_region_id(layer_range);
            });
        }
    }

    for (std::unique_ptr<PrintRegion>& region : all_regions) {
        region->finalize_config();
    }

    return out;
}

std::vector<Biz::Slicing::Warning> validate_print_config_change(const PrintConfigView &old_config, const PrintConfigView &new_config)
{
    std::vector<Biz::Slicing::Warning> warnings;
    if (
        old_config.get<int>("bed_temperature_extruder") > 0
        && old_config.get<int>("bed_temperature_extruder")
        == new_config.get<int>("bed_temperature_extruder")
    ) {
        // Bed temperature extruder is set, and it didn't change with the new config.
        if (old_config.get<std::vector<int>>("bed_temperature") != new_config.get<std::vector<int>>("bed_temperature")
         || old_config.get<std::vector<int>>("first_layer_bed_temperature") != new_config.get<std::vector<int>>("first_layer_bed_temperature")) {
            // When any bed temperature changes, we warn the user that the bed temperature extruder may need to be changed.
            warnings.emplace_back(Biz::Slicing::Warning{Biz::Slicing::WarningCode::BedTempsChanged});
        }
    }
    return warnings;
}

FDMPrintSteps get_custom_gcode_invalidated_steps(
    const std::optional<CustomGCode::Info>& current,
    const std::optional<CustomGCode::Info>& next,
    const std::size_t num_extruders
)
{
    if (current == next) {
        return {};
    }
    const std::optional<CustomGCode::Mode> current_mode{
        current ? std::optional{current->mode} : std::nullopt
    };
    const std::optional<CustomGCode::Mode> next_mode{next ? std::optional{next->mode} : std::nullopt};

    const bool multi_extruder_differ = (current_mode == next_mode)
        && (current_mode == CustomGCode::Mode::MultiExtruder || next_mode == CustomGCode::Mode::MultiExtruder);

    // The Tool Ordering and the Wipe Tower are no more valid.
    // Because G-code export (PlaceholderParser) accesses the first layer convex hull, we need to also invalidate psSkirtBrim.
    const FDMPrintSteps order_differs_invalidated_steps{psGCodeExport, psWipeTower, psSkirtBrim};

    if (multi_extruder_differ) {
        return order_differs_invalidated_steps;
    }

    if (num_extruders > 1) {
        // For multi-extruder printers, we perform a tool change before a color change.
        // So, in that case, we must invalidate tool ordering and wipe tower even if custom color change g-codes differ.
        if (!current || !next) {
            return order_differs_invalidated_steps;
        }
        const bool color_change_differ{custom_per_printz_gcodes_tool_changes_differ(
            current->gcodes,
            next->gcodes,
            CustomGCode::Type::ColorChange
        )};

        if (next_mode == CustomGCode::Mode::MultiExtruder && color_change_differ) {
            return order_differs_invalidated_steps;
        }

        // Tool change G-codes are applied as color changes for a single extruder printer, no need to invalidate tool ordering.
        // FIXME The tool ordering may be invalidated unnecessarily if the custom_gcode_per_print_z.mode is not applicable
        // to the active print / model state, and then it is reset, so it is being applicable, but empty, thus the effect is the same.
        if (custom_per_printz_gcodes_tool_changes_differ(
                current->gcodes,
                next->gcodes,
                CustomGCode::Type::ToolChange
            )) {
            return order_differs_invalidated_steps;
        }
    }

    return {psGCodeExport};
}

PlaceholderParser init_placeholder_parser(
    const ParserConfig& new_config,
    const std::optional<ModelWipeTower>& wipe_tower,
    const Domain::Preset::SelectedPresetMetadata& metadata
)
{
    PlaceholderParser parser{new_config};

    // set preset IDs to be compatible with legacy printers
    parser.set("printer_settings_id", metadata.printer.name);
    parser.set("print_settings_id", metadata.print.name);
    {
        std::vector<std::string> names;
        for (const auto& mm : metadata.materials) {
            names.push_back(mm.name);
        }
        parser.set("filament_settings_id", names);
    }

    if (wipe_tower) {
        parser.set("wipe_tower_x", wipe_tower->position.x());
        parser.set("wipe_tower_y", wipe_tower->position.y());
        parser.set("wipe_tower_rotation_angle", wipe_tower->rotation);
    }

    return parser;
}

bool get_solid_or_modifier_differ(
    const Domain::ModelObject& model_object,
    const Domain::ModelObject& model_object_new,
    const bool num_extruders_changed
)
{
    const std::initializer_list<Domain::ModelVolumeType> solid_or_modifier_types{
        Domain::ModelVolumeType::MODEL_PART,
        Domain::ModelVolumeType::NEGATIVE_VOLUME,
        Domain::ModelVolumeType::PARAMETER_MODIFIER
    };
    if (model_volume_list_changed(model_object, model_object_new, solid_or_modifier_types)) {
        return true;
    }

    if (model_mmu_segmentation_data_changed(model_object, model_object_new)) {
        return true;
    }

    const bool painting_changed{model_object_new.is_mm_painted() && num_extruders_changed};
    if (painting_changed) {
        return true;
    }

    if (model_fuzzy_skin_data_changed(model_object, model_object_new)) {
        return true;
    }
    return false;
}

bool get_layers_or_translation_differ(
    const Domain::ModelObject& model_object,
    const Domain::ModelObject& model_object_new
)
{
    const bool layer_height_ranges_differ = !layer_height_ranges_equal(
        model_object.layer_config_ranges,
        model_object_new.layer_config_ranges,
        model_object_new.layer_height_profile.empty()
    );

    const bool model_origin_translation_differ = model_object.origin_translation
        != model_object_new.origin_translation;

    const bool layer_height_profile_differ = !model_object.layer_height_profile.timestamp_matches(
        model_object_new.layer_height_profile
    );

    return model_origin_translation_differ
        || layer_height_ranges_differ
        || layer_height_profile_differ;
}

bool instance_ids_equal(const Domain::ModelInstancePtrs& current, const Domain::ModelInstancePtrs& next)
{
    if (current.size() != next.size()) {
        return false;
    }
    return std::equal(current.begin(), current.end(), next.begin(), [](auto l, auto r) {
        return l->id() == r->id();
    });
}


using ModelObjectId = ObjectID;
using ObjectMap = std::multimap<ModelObjectId, PrintObject*>;

ObjectMap map_objects(const PrintObjectPtrs& objects)
{
    std::multimap<ModelObjectId, PrintObject*> result;
    for (PrintObject* object : objects) {
        result.insert({object->model_object()->id(), object});
    }
    return result;
}

PrintObjectPtrs get_object_range(const std::pair<ObjectMap::iterator, ObjectMap::iterator>& range)
{
    PrintObjectPtrs result;
    std::transform(
        range.first,
        range.second,
        std::back_inserter(result),
        [](const std::pair<ModelObjectId, PrintObject*>& pair) { return pair.second; }
    );
    return result;
}

// Currently the PrintObjects are uniquely identified by a Transform3d.
// It is what it is.
struct PrintObjectUniqueId
{
    PrintObjectUniqueId(const Transform3d& transform, const ModelObjectId& model_object_id):
        m_transform(transform),
        m_model_object_id{model_object_id}
    {}

    bool operator<(const PrintObjectUniqueId& rhs) const
    {
        if (m_model_object_id == rhs.m_model_object_id) {
            return transform3d_lower(m_transform, rhs.m_transform);
        }
        return m_model_object_id < rhs.m_model_object_id;
    }

private:
    Transform3d m_transform;
    ModelObjectId m_model_object_id;
};

PrintAndObjectSteps get_model_invalidated_steps(
    const Domain::ModelObject& model_object,
    const Domain::ModelObject& model_object_new,
    const std::size_t num_extruders_changed
)
{
    const bool solid_or_modifier_differ{
        get_solid_or_modifier_differ(model_object, model_object_new, num_extruders_changed)
    };

    // Check whether a model part volume was added or removed, their transformations or order changed.
    // Only volume IDs, volume types, transformation matrices and their order are checked, configuration and other parameters are NOT checked.
    if (solid_or_modifier_differ || get_layers_or_translation_differ(model_object, model_object_new)) {
        return {AllSteps{}, AllSteps{}};
    }

    const bool supports_differ{
        model_volume_list_changed(model_object, model_object_new, Domain::ModelVolumeType::SUPPORT_BLOCKER)
        || model_volume_list_changed(model_object, model_object_new, Domain::ModelVolumeType::SUPPORT_ENFORCER)
    };


    FDMPrintSteps print_steps;
    FDMPrintObjectSteps object_steps;

    if (supports_differ || model_custom_supports_data_changed(model_object, model_object_new)) {
        const std::vector<SlicingSync::Step> steps{SlicingSync::propagate(posSupportMaterial)};
        for (const SlicingSync::Step& step : steps) {
            std::visit(Domain::overloaded{
                [&](const FDMPrintObjectStep& step){
                    object_steps.insert(step);
                },
                [&](const FDMPrintStep& step){
                    print_steps.insert(step);
                },
            }, step);
        }
    }
    if (model_custom_seam_data_changed(model_object, model_object_new)) {
        print_steps.insert(psGCodeExport);
    }

    if (!solid_or_modifier_differ) {
        if (model_object.name != model_object_new.name) {
            print_steps.insert(psGCodeExport);
        }

        if (!instance_ids_equal(model_object.instances, model_object_new.instances)) {
            // G-code generator accesses model_object.instances to generate sequential print ordering matching the Plater object list.
            // WipingExtrusions::mark_wiping_extrusions() precalculate data based on the number of instances when wiping into infill/object is enabled.
            print_steps.insert({psGCodeExport, psWipeTower});
        }
    }
    return {print_steps, object_steps};
}

using StepsPerModelObjectId = std::map<ModelObjectId, PrintAndObjectSteps>;

struct ModelObjectsSyncResult {
    Domain::ModelObjectPtrs objects;
    StepsPerModelObjectId reused_objects;
};

ModelObjectsSyncResult sync_model_objects(
    const Domain::ModelObjectPtrs& new_objects,
    const std::map<ObjectID, Domain::ModelObject*>& reuse_candidates,
    const bool num_extruders_changed
)
{
    ModelObjectsSyncResult result;

    for (Domain::ModelObject* model_object_new : new_objects) {
        const auto current_object_it{reuse_candidates.find(model_object_new->id())};

        if (current_object_it == reuse_candidates.end()) {
            result.objects.push_back(Domain::ModelObject::new_copy(*model_object_new));
            continue;
        }

        Domain::ModelObject* model_object{current_object_it->second};

        const PrintAndObjectSteps invalidated_steps{
            get_model_invalidated_steps(*model_object, *model_object_new, num_extruders_changed)
        };

        if (std::holds_alternative<AllSteps>(invalidated_steps.first)
            && std::holds_alternative<AllSteps>(invalidated_steps.second)) {
            result.objects.push_back(Domain::ModelObject::new_copy(*model_object_new));
            continue;
        }

        result.reused_objects.insert({model_object->id(), invalidated_steps});

        model_object->assign_copy(*model_object_new);
        result.objects.push_back(model_object);
    }

    return result;
}

std::map<PrintObjectUniqueId, PrintObject*> get_reuse_candidates(
    const ModelObjectsSyncResult& model_objects_sync_result,
    std::multimap<ModelObjectId, PrintObject*>& current_print_objects
)
{
    std::map<PrintObjectUniqueId, PrintObject*> result;
    for (Domain::ModelObject* model_object : model_objects_sync_result.objects) {
        if (model_objects_sync_result.reused_objects.count(model_object->id()) == 0) {
            continue;
        }

        const std::vector<PrintObject*> print_objects_range{
            get_object_range(current_print_objects.equal_range(model_object->id()))
        };
        for (PrintObject* print_object : print_objects_range) {
            result.insert(
                {{print_object->trafo(), print_object->model_object()->id()}, print_object}
            );
        }
    }

    return result;
}

struct PrintObjectsSyncResult {
    std::vector<PrintObject*> objects;
    InvalidatedSteps invalidated_steps;
    std::set<PrintObject*> reused_objects;
};

tl::expected<PrintObjectsSyncResult, Errors> sync_print_objects(
    const Domain::ModelObjectPtrs& model_objects,
    const std::map<PrintObjectUniqueId, PrintObject*>& reuse_candidates,
    const FullConfigFDMPtr& new_full_config,
    const std::size_t num_extruders,
    const Vec3d& shrinkage_compensation,
    Print* print
)
{
    PrintObjectsSyncResult result;

    for (Domain::ModelObject* model_object : model_objects) {
        std::vector<PrintObjectTrafoAndInstances> print_instances = print_objects_from_model_object(
            model_object->instances,
            shrinkage_compensation
        );

        const auto object_settings{prepare_slicing_object_input(
            model_object->object_settings,
            new_full_config->hw_config(),
            new_full_config->hw_config().material_slot_count()
        )};
        if (!object_settings.has_value()) {
            return tl::unexpected{object_settings.error()};
        }
        const PrintObjectConfigView new_config{new_full_config, object_settings.value()};

        for (PrintObjectTrafoAndInstances& new_instances : print_instances) {
            const auto current_instace_it{
                reuse_candidates.find({new_instances.trafo, model_object->id()})
            };

            if (current_instace_it != reuse_candidates.end()) {
                PrintObject* print_object{current_instace_it->second};
                print_object->set_config(new_config);
                result.invalidated_steps.print = SlicingSync::merge(
                    result.invalidated_steps.print,
                    AllOrSome<FDMPrintSteps>{print_object->set_instances(std::move(new_instances.instances))}
                );
                result.reused_objects.insert(print_object);
                result.objects.push_back(print_object);
            } else {
                // This is a new instance (or a set of instances with the same trafo). Just add it.
                PrintObject* print_object = new PrintObject(
                    print,
                    model_object,
                    new_config,
                    new_instances.trafo,
                    std::move(new_instances.instances)
                );
                result.objects.push_back(print_object);
            }
        }
    }

    return result;
}

struct RegionsSyncResult
{
    InvalidatedSteps invalidated_steps;
    std::vector<std::pair<PrintObject*, std::shared_ptr<PrintObjectRegions>>> regions;
};

tl::expected<RegionsSyncResult, Errors> sync_regions(
    const PrintObjectPtrs& print_objects,
    const std::size_t num_extruders,
    const VirtualExtruders& virtual_extruders,
    const FullConfigFDMPtr& new_full_config
)
{
    RegionsSyncResult result;

    for (auto it_print_object = print_objects.begin(); it_print_object != print_objects.end();) {
        const PrintObject& print_object = *(*it_print_object);

        const std::vector<unsigned int> painting_extruders{
            num_extruders > 1 ?
                get_painting_extruders(*print_object.model_object(), num_extruders, virtual_extruders) :
                std::vector<unsigned int>{}
        };

        const auto new_regions{generate_print_object_regions(
            nullptr,
            print_object.model_object()->volumes,
            LayerRanges(print_object.model_object()->layer_config_ranges, new_full_config->hw_config()),
            print_object.config().object_settings(),
            new_full_config,
            print_object.trafo(),
            num_extruders,
            virtual_extruders,
            print_object.is_mm_painted() ? 0.f : float(print_object.config().get<double>("xy_size_compensation")),
            painting_extruders,
            print_object.is_fuzzy_skin_painted()
        )};
        if (!new_regions) {
            return tl::unexpected{new_regions.error()};
        }

        const PrintObjectRegions empty_regions;
        const PrintObjectRegions& current_regions{
            print_object.shared_regions() != nullptr ? *print_object.shared_regions() : empty_regions
        };

        const PrintAndObjectSteps invalidated_steps{
            get_invalidated_steps(current_regions, **new_regions)
        };

        (*new_regions)->generated_support_points = current_regions.generated_support_points;

        const auto print_objects_range_begin{it_print_object};
        const auto print_objects_range_end{
            std::find_if(it_print_object, print_objects.end(), [&](const PrintObject* object) {
                return object->model_object()->id() != print_object.model_object()->id();
            })
        };

        FDMPrintSteps* result_print_steps{std::get_if<FDMPrintSteps>(&result.invalidated_steps.print)};
        if (result_print_steps != nullptr) {
            std::visit(Domain::overloaded{
                [&](const AllSteps&){
                    result.invalidated_steps.print = AllSteps{};
                },
                [&](FDMPrintSteps invalidated_print_steps){
                    result_print_steps->merge(std::move(invalidated_print_steps));
                }
            }, invalidated_steps.first);
        }

        for (auto it = print_objects_range_begin; it != print_objects_range_end; ++it) {
            result.regions.push_back({*it, *new_regions});
            result.invalidated_steps.object.insert({*it, invalidated_steps.second});
        }

        it_print_object = print_objects_range_end;
    }

    return result;
}


PrintRegionPtrs update_print_region_ids(
    const std::vector<std::pair<PrintObject*, std::shared_ptr<PrintObjectRegions>>>& regions
) {
    PrintRegionPtrs result;

    PrintObjectRegions *print_object_regions = nullptr;
    for (const auto& [print_object, shared_regions] : regions) {
        if (print_object_regions != shared_regions.get()) {
            print_object_regions = shared_regions.get();
            for (std::unique_ptr<Slic3r::PrintRegion> &print_region : print_object_regions->all_regions) {
                const int print_region_id{static_cast<int>(result.size())};
                result.emplace_back(print_region.get());
                print_region->set_print_region_id(print_region_id);
            }
        }
    }
    return result;
}

InvalidatedSteps remap_invalidated_steps(
    const StepsPerModelObjectId& model_invalidation,
    std::multimap<ModelObjectId, PrintObject*>& object_map
)
{
    InvalidatedSteps result;
    for (const auto& [model_object_id, invalidated_steps] : model_invalidation) {
        result.print = SlicingSync::merge(result.print, invalidated_steps.first);

        const std::vector<PrintObject*> print_objects_range{
            get_object_range(object_map.equal_range(model_object_id))
        };
        for (PrintObject* print_object : print_objects_range) {
            result.object.insert({print_object, invalidated_steps.second});
        }
    }

    return result;
}

using Regions = std::vector<std::pair<PrintObject *, std::shared_ptr<PrintObjectRegions>>>;

struct ModelSyncResult {
    PrintObjectPtrs print_objects;
    Domain::ModelObjectPtrs model_objects;
    Regions regions;
    InvalidatedSteps invalidated_steps;
};

tl::expected<ModelSyncResult, Errors> sync_model(
    const Domain::Model& current_model,
    const Domain::Model& new_model,
    const PrintObjectPtrs& current_objects,
    const FullConfigFDMPtr& new_full_config,

    // TODO: Get rid of all these arguments. This is madness.
    Print* print,
    const Vec3d& shrinkage_compensation,
    std::size_t num_extruders,
    const VirtualExtruders& virtual_extruders,
    bool num_extruders_changed
)
{
    std::map<ObjectID, Domain::ModelObject*> current_model_objects;

    if (current_model.id() == new_model.id()) {
        for (Domain::ModelObject* model_object : current_model.objects) {
            current_model_objects.insert({model_object->id(), model_object});
        }
    }

    const ModelObjectsSyncResult model_objects_sync_result{
        sync_model_objects(new_model.objects, current_model_objects, num_extruders_changed)
    };

    using ModelObjectId = ObjectID;
    std::multimap<ModelObjectId, PrintObject*> object_map{map_objects(current_objects)};

    const InvalidatedSteps model_objects_invalidated_steps{
        remap_invalidated_steps(model_objects_sync_result.reused_objects, object_map)
    };

    const std::map<PrintObjectUniqueId, PrintObject*> reuse_candidates{
        get_reuse_candidates(model_objects_sync_result, object_map)
    };

    const auto print_objects_sync_result{sync_print_objects(
        model_objects_sync_result.objects,
        reuse_candidates,
        new_full_config,
        num_extruders,
        shrinkage_compensation,
        print
    )};
    if (!print_objects_sync_result) {
        return tl::unexpected{print_objects_sync_result.error()};
    }

    const auto regions_sync_result{
        sync_regions(print_objects_sync_result->objects, num_extruders, virtual_extruders, new_full_config)
    };
    if (!regions_sync_result) {
        return tl::unexpected{regions_sync_result.error()};
    }

    const InvalidatedSteps invalidated_steps{merge(
        merge(model_objects_invalidated_steps, print_objects_sync_result->invalidated_steps),
        regions_sync_result->invalidated_steps
    )};

    return ModelSyncResult{
        print_objects_sync_result->objects,
        model_objects_sync_result.objects,
        regions_sync_result->regions,
        invalidated_steps
    };
}

void delete_old_model_objects(const Domain::ModelObjectPtrs& old_objects, const Domain::ModelObjectPtrs& new_objects) {
    const std::set<Domain::ModelObject*> model_objects_set{
        new_objects.begin(),
        new_objects.end()
    };
    for (Domain::ModelObject* model_object : old_objects) {
        if (model_objects_set.count(model_object) == 0) {
            delete model_object;
        }
    }
}

InvalidatedSteps delete_old_print_objects(const PrintObjectPtrs& old_objects, const PrintObjectPtrs& new_objects) {
    InvalidatedSteps result;
    const std::set<PrintObject*> print_objects_set{
        new_objects.begin(),
        new_objects.end()
    };
    for (PrintObject* print_object : old_objects) {
        if (print_objects_set.count(print_object) == 0) {
            result.print = AllSteps{};
            delete print_object;
        }
    }
    return result;
}
}

bool InvalidatedSteps::empty() const {
    if (std::holds_alternative<AllSteps>(print)) {
        return false;
    }
    if (!std::get<FDMPrintSteps>(print).empty()) {
        return false;
    }

    for (const auto& [_, steps] : object) {
        if (std::holds_alternative<AllSteps>(steps)) {
            return false;
        }
        if (!std::get<FDMPrintObjectSteps>(steps).empty()) {
            return false;
        }
    }
    return true;
}

InvalidatedSteps sync_hw_config(
    const PrintObjectPtrs& print_objects,
    const Domain::Preset::HwPrinterConfig& old_config,
    const Domain::Preset::HwPrinterConfig& new_config
)
{
    bool gcode_export_invalidated{false};
    bool slicing_invalidated{false};

    if (new_config.tools.size() != old_config.tools.size()) {
        slicing_invalidated = true;
    } else {
        ASSERT(new_config.tools.size() == old_config.tools.size());
        for (std::size_t extruder_index{}; extruder_index < new_config.tools.size();
             extruder_index++)
        {
            const Domain::Preset::HwToolConfig new_tool{new_config.tools.at(extruder_index)};
            const Domain::Preset::HwToolConfig old_tool{old_config.tools.at(extruder_index)};
            const bool old_tool_high_flow{
                Domain::Preset::get_feature<bool>(old_tool.features, "nozzle_high_flow")
                    .value_or(false)
            };
            const bool new_tool_high_flow{
                Domain::Preset::get_feature<bool>(new_tool.features, "nozzle_high_flow")
                    .value_or(false)
            };
            if (old_tool_high_flow != new_tool_high_flow) {
                gcode_export_invalidated = true;
            }

            const std::optional<double> old_tool_diameter{
                Domain::Preset::get_feature<double>(old_tool.features, "nozzle_diameter")
            };
            const std::optional<double> new_tool_diameter{
                Domain::Preset::get_feature<double>(new_tool.features, "nozzle_diameter")
            };

            if ((!old_tool_diameter || !new_tool_diameter)
                || !Domain::fuzzy_compare(*old_tool_diameter, *new_tool_diameter))
            {
                slicing_invalidated = true;
                break;
            }
        }
    }

    std::vector<SlicingSync::Step> invalid_steps;
    if (gcode_export_invalidated) {
        const std::vector<SlicingSync::Step> steps{SlicingSync::propagate(psGCodeExport)};
        invalid_steps.insert(invalid_steps.end(), steps.begin(), steps.end());
    }
    if (slicing_invalidated) {
        const std::vector<SlicingSync::Step> steps{SlicingSync::propagate(posSlice)};
        invalid_steps.insert(invalid_steps.end(), steps.begin(), steps.end());
    }

    InvalidatedSteps result;
    for (const auto& step : invalid_steps) {
        std::visit(
            Domain::overloaded{
                [&](const FDMPrintStep& step) { std::get<FDMPrintSteps>(result.print).insert(step); },
                [&](const FDMPrintObjectStep& step)
                {
                    for (PrintObject* object : print_objects) {
                        auto& object_steps{std::get<FDMPrintObjectSteps>(result.object[object])};
                        object_steps.insert(step);
                    }
                },
            },
            step
        );
    }

    return result;
}

InvalidatedSteps invalidate_for_virtual_extruders_change(const PrintObjectPtrs& print_objects)
{
    InvalidatedSteps result;
    for (const SlicingSync::Step& step : SlicingSync::propagate(posSlice)) {
        std::visit(
            Domain::overloaded{
                [&](const FDMPrintStep& print_step)
                { std::get<FDMPrintSteps>(result.print).insert(print_step); },
                [&](const FDMPrintObjectStep& object_step)
                {
                    for (PrintObject* object : print_objects) {
                        std::get<FDMPrintObjectSteps>(result.object[object]).insert(object_step);
                    }
                },
            },
            step
        );
    }

    return result;
}

Biz::Slicing::ApplyStatus::Status Print::apply(
    const Domain::Model& model,
    const FullConfigFDMPtr& new_full_config_ptr,
    const Domain::Preset::SelectedPresetMetadata& metadata,
    const MetadataSerializeFn& serializer,
    const Domain::ModelWipeTower& wipe_tower,
    const std::optional<Domain::CustomGCode::Info>& custom_gcode,
    const std::vector<unsigned>& extruder_candidates
)
{
    const bool could_have_had_wipe_tower{can_have_wipe_tower()};

    PrintConfigView new_print_config{new_full_config_ptr};

    // Check if the print config change will produce any warnings.
    std::vector<Biz::Slicing::Warning> warnings{
        validate_print_config_change(m_config, new_print_config)
    };

    // Grab the lock for the Print / PrintObject milestones.
    std::scoped_lock<std::mutex> lock(this->state_mutex());

    const size_t num_extruders{new_print_config.hw_config().material_slot_count()};
    const bool num_extruders_changed{
        m_config.hw_config().material_slot_count()
        != num_extruders
    };

    const VirtualExtruders& virtual_extruders = new_full_config_ptr->virtual_extruders();
    const bool virtual_extruders_differ       = virtual_extruders != m_virtual_extruders;
    m_virtual_extruders                       = virtual_extruders;

    const InvalidatedSteps hw_config_invalidated_steps{sync_hw_config(
        m_objects,
        m_config.hw_config(),
        new_print_config.hw_config()
    )};

    if (model.id() != m_model.id()) {
        m_model.copy_id(model);
    }

    const std::optional<Vec3d> shrinkage_compensation_optional{
        Biz::Slicing::get_shrinkage_compensation(extruder_candidates, new_print_config)
    };
    if (!shrinkage_compensation_optional) {
        warnings.emplace_back(
            Biz::Slicing::Warning{Biz::Slicing::WarningCode::FilamentShrinkageDiffer}
        );
    }
    const Vec3d shrinkage_compensation{
        shrinkage_compensation_optional.value_or(Vec3d{1.0, 1.0, 1.0})
    };
    const auto model_sync_result{sync_model(
        m_model,
        model,
        m_objects,
        new_full_config_ptr,
        this,
        shrinkage_compensation,
        num_extruders,
        virtual_extruders,
        num_extruders_changed
    )};
    if (!model_sync_result) {
        return Biz::Slicing::ApplyStatus::InvalidData{model_sync_result.error()};
    }

    m_extruder_candidates = extruder_candidates;
    m_shrinkage_compensation = shrinkage_compensation;
    m_metadata_serializer = serializer;
    m_config = new_print_config;

    m_placeholder_parser = init_placeholder_parser(
        Biz::Parser::IO::get_parser_config(new_print_config),
        wipe_tower,
        metadata
    );

    InvalidatedSteps wipe_tower_invalidated_steps;
    if (can_have_wipe_tower()) {
        // Check the position and rotation of the wipe tower.
        if (wipe_tower != m_wipe_tower) {
            for (const auto& step : SlicingSync::propagate(psSkirtBrim)) {
                std::visit(
                    Domain::overloaded{
                        [&](const FDMPrintStep& step)
                        { std::get<FDMPrintSteps>(wipe_tower_invalidated_steps.print).insert(step); },
                        [&](const FDMPrintObjectStep& step)
                        { PANIC("No object steps can be invalidated by this!"); },
                    },
                    step
                );
            }
        }
        m_wipe_tower = wipe_tower;
    } else {
        m_wipe_tower = std::nullopt;
    }

    const InvalidatedSteps custom_gcode_invalidated_steps{get_custom_gcode_invalidated_steps(
        m_custom_gcode,
        custom_gcode,
        num_extruders
    )};

    m_custom_gcode = custom_gcode;


    delete_old_model_objects(m_model.objects, model_sync_result->model_objects);
    m_model.objects = model_sync_result->model_objects;

    const InvalidatedSteps deleted_objects_invalidated_steps{
        delete_old_print_objects(m_objects, model_sync_result->print_objects)
    };

    InvalidatedSteps changed_objects_invalidated_steps;
    if (m_objects != model_sync_result->print_objects) {
        changed_objects_invalidated_steps.print = AllSteps{};
        m_objects = model_sync_result->print_objects;
    }

    // Modifies regions!
    m_print_regions = update_print_region_ids(model_sync_result->regions);

    for (const auto& [print_object, new_regions] : model_sync_result->regions) {
        print_object->set_shared_regions(new_regions);
    }

    const InvalidatedSteps virtual_extruders_invalidated_steps{
        virtual_extruders_differ ?
            invalidate_for_virtual_extruders_change(m_objects) :
            InvalidatedSteps{}
    };

    const InvalidatedSteps invalidated_steps{SlicingSync::merge(
        {hw_config_invalidated_steps,
         wipe_tower_invalidated_steps,
         custom_gcode_invalidated_steps,
         model_sync_result->invalidated_steps,
         changed_objects_invalidated_steps,
         deleted_objects_invalidated_steps,
         virtual_extruders_invalidated_steps}
    )};

    if (can_have_wipe_tower()) {
        ASSERT(m_wipe_tower);
        const bool wipe_tower_invalidated{std::visit(
            Domain::overloaded{
                [&](const std::set<FDMPrintStep>& steps)
                { return steps.contains(FDMPrintStep::psWipeTower); },
                [&](AllSteps) { return true; },
            },
            invalidated_steps.print
        )};

        if (wipe_tower_invalidated || !could_have_had_wipe_tower) {
            m_on_wipe_tower_geometry(
                Biz::Slicing::WipeTowerGeometry{
                    .depths   = {},
                    .fallback_depth = 7.0,
                    .fallback_height = 25.0,
                    .width = new_full_config_ptr->get<double>("wipe_tower_width"),
                    .cone_radius = 5.0,
                    .cone_x_scale = 1.0,
                    .brim_width = new_full_config_ptr->get<double>("wipe_tower_brim_width")
                }
            );
        }
    }
    if (!can_have_wipe_tower() && could_have_had_wipe_tower) {
        m_on_wipe_tower_geometry(std::nullopt);
    }

    const bool changed{!invalidated_steps.empty()};
    this->invalidate_object_steps(invalidated_steps);

    // Update SlicingParameters for each object where the SlicingParameters is not valid.
    // If it is not valid, then it is ensured that PrintObject.m_slicing_params is not in use
    // (posSlicing and posSupportMaterial was invalidated).
    for (PrintObject* object : m_objects) {
        object->update_slicing_parameters();
    }

    if (changed) {
        this->cleanup();
        return Biz::Slicing::ApplyStatus::Changed{warnings};
    }

    return Biz::Slicing::ApplyStatus::Unchanged{};
}

void Print::cleanup()
{
    // Invalidate data of a single ModelObject shared by multiple PrintObjects.
    // Find spans of PrintObjects sharing the same PrintObjectRegions.
    std::vector<PrintObject*> all_objects(m_objects);
    std::sort(all_objects.begin(), all_objects.end(), [](const PrintObject *l, const PrintObject *r){ return l->shared_regions() < r->shared_regions(); } );
    for (auto it = all_objects.begin(); it != all_objects.end();) {
        PrintObjectRegions *shared_regions = (*it)->m_shared_regions.get();
        auto it_begin = it;
        for (; it != all_objects.end() && shared_regions == (*it)->shared_regions(); ++ it)
            // Let the PrintObject clean up its data with invalidated milestones.
            (*it)->cleanup();
        auto this_objects = SpanOfConstPtrs<PrintObject>(const_cast<const PrintObject* const* const>(&(*it_begin)), it - it_begin);
        if (! Print::is_shared_print_object_step_valid_unguarded(this_objects, posSupportSpotsSearch))
            shared_regions->generated_support_points.reset();
    }
}

bool Print::is_shared_print_object_step_valid_unguarded(SpanOfConstPtrs<PrintObject> print_objects, FDMPrintObjectStep print_object_step)
{
    return std::any_of(print_objects.begin(), print_objects.end(), [print_object_step](auto po){ return po->is_step_done_unguarded(print_object_step); });
}

} // namespace Slic3r
