#include "Slic3r/Biz/Algorithms/FacetsAnnotation.hpp"
#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Brim.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/I18N_private.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/ConflictChecker.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include "libslic3r/PrePreview.hpp"
#include "libslic3r/ModelUtils.hpp"
#include "libslic3r/SlicingInput.hpp"
#include "libslic3r/CustomParametersHandling.hpp"
#include "libslic3r/InstanceTransformations.hpp"
#include "libslic3r/ExtruderCandidates.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <float.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <Slic3r/Log.hpp>
#include <boost/regex.hpp>
#include <ranges>

#include "libslic3r/ModelUtils.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

using SlicingSync::PrintAndObjectSteps;
using ParserConfig = Biz::Parser::IO::Config;
using Biz::Parser::PlaceholderParser;
using Domain::ConfigPack;
using Domain::ConfigPackFDM;
using Domain::GCodeFlavor;
using Slic3r::Biz::Slicing::GeneratedSupportPoint;
using Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot;
using Slic3r::Biz::Slicing::IThumbnailImageGenerator;
using Slic3r::Biz::Slicing::ObjectSupportPoints;
using Slic3r::Biz::Slicing::SliceUntilStep;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigOverrides;
using Slic3r::Domain::Model;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::SupportMode;
using Slic3r::Domain::VolumeSettings;

using Slic3r::Biz::Algorithms::LayerHeight::check_object_layers_fixed;
using Slic3r::Biz::Algorithms::VirtualExtruder::is_virtual_extruder;

template class PrintState<FDMPrintStep, psCount>;
template class PrintState<FDMPrintObjectStep, posCount>;

PrintInstance::PrintInstance(const Domain::ModelInstance& model_instance, std::size_t model_instance_index, const Domain::Vec2big& shift)
    : print_object(nullptr)
    , model_instance(model_instance)
    , model_instance_index(model_instance_index)
    , m_shift(shift)
{}

Domain::Point PrintInstance::shift() const
{
    ASSERT(std::in_range<coord_t>(m_shift.x()) && std::in_range<coord_t>(m_shift.y()));
    return m_shift.cast<coord_t>();
}

Print::Print() :
    m_on_fdm_result([](Biz::libpgcode::ProcessorResult&&) {}),
    m_on_wipe_tower_geometry([](Biz::Slicing::OptWipeTowerGeometry&&) {}),
    m_on_extruder_candidates([](std::vector<unsigned>) {}),
    m_on_generated_support_points([](GeneratedSupportPointsSnapshot&&) {})
{}

Print::Print(
    const OnFdmResult& on_fdm_result,
    const OnWipeTowerGeometry& on_wipe_tower_geometry,
    const OnExtruderCandidates& on_extruder_candidates,
    const OnGeneratedSupportPoints& on_generated_support_points
) :
    m_on_fdm_result(on_fdm_result),
    m_on_wipe_tower_geometry(on_wipe_tower_geometry),
    m_on_extruder_candidates(on_extruder_candidates),
    m_on_generated_support_points(on_generated_support_points)
{}

Print::~Print() { this->clear(); }

void Print::clear()
{
	std::scoped_lock<std::mutex> lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    m_print_regions.clear();
    m_model.clear_objects();
}

using Domain::FullConfigFDMPtr;

// Extruders are indexed from 1.
bool in_range(const Domain::ConfigItem& extruder_item, int min, int max)
{
    const int extruder{extruder_item.get<int>()};
    return min <= extruder && extruder <= max;
}

static std::optional<Biz::Slicing::Error>
check_extruder_offset(const Domain::Model& model, const Domain::ConfigPackFDM& config) {
    const size_t tool_count{config.tool.size()};
    const auto extruder_offset{config.printer.items.opt("extruder_offset").get<std::vector<Vec2d>>()};
    if (extruder_offset.size() != tool_count) {
        using Biz::Slicing::Error;
        using Biz::Slicing::ErrorCode;
        return Error{ErrorCode::InvalidExtruderOffset, {"extruder_offset"}};
    }
    return std::nullopt;
}

/**
 * @brief Report infill roles printed by a different virtual extruder than the perimeter.
 *
 * Checked per settings scope (print -> object -> volume or height range), violating
 * role keys are appended to result.
 */
static void check_virtual_extruder_roles(
    const Model& model,
    const ConfigPackFDM& config,
    std::vector<std::string>& result
)
{
    using ExtruderIdsByRole = std::array<int, 3>;

    const std::array<std::string, 3> extruder_role_keys{
        "perimeter_extruder",
        "infill_extruder",
        "solid_infill_extruder"
    };

    const auto resolve_extruder_ids =
        [&](const ConfigOverrides& overrides, ExtruderIdsByRole parent_extruder_ids)
    {
        for (size_t i = 0; i < extruder_role_keys.size(); ++i) {
            const std::string& extruder_role_key              = extruder_role_keys[i];
            const std::optional<ConfigItem> extruder_override = overrides.get(extruder_role_key);

            if (extruder_override.has_value()) {
                parent_extruder_ids[i] = extruder_override->get<int>();
            }
        }

        return parent_extruder_ids;
    };

    const auto check_extruder_ids = [&](const ExtruderIdsByRole& extruder_ids)
    {
        const int first_extruder_id = extruder_ids[0];

        for (size_t i = 1; i < extruder_role_keys.size(); ++i) {
            const int extruder_id = extruder_ids[i];

            if (extruder_id != first_extruder_id
                && is_virtual_extruder(
                    static_cast<unsigned int>(extruder_id),
                    config.virtual_extruders
                ))
            {
                result.push_back(extruder_role_keys[i]);
            }
        }
    };

    ExtruderIdsByRole print_extruder_ids;
    for (size_t i = 0; i < extruder_role_keys.size(); ++i) {
        print_extruder_ids[i] = config.print.items.opt(extruder_role_keys[i]).get<int>();
    }

    check_extruder_ids(print_extruder_ids);

    for (const ModelObject* object : model.objects) {
        const ExtruderIdsByRole object_extruder_ids =
            resolve_extruder_ids(object->object_settings.overrides, print_extruder_ids);

        check_extruder_ids(object_extruder_ids);

        for (const ModelVolume* volume : object->volumes) {
            check_extruder_ids(
                resolve_extruder_ids(volume->volume_settings.overrides, object_extruder_ids)
            );
        }

        for (const VolumeSettings& height_range_settings :
             object->layer_config_ranges | std::views::values)
        {
            check_extruder_ids(
                resolve_extruder_ids(height_range_settings.overrides, object_extruder_ids)
            );
        }
    }
}

static std::optional<Biz::Slicing::Error> check_extruders(
    const Domain::Model& model,
    const Domain::ConfigPackFDM& config,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    const int slot_count{static_cast<int>(hw_config.material_slot_count())};

    const auto accepts_extruder = [&](const ConfigItem& item, const int min_value)
    {
        return in_range(item, min_value, slot_count)
            || is_virtual_extruder(
                   static_cast<unsigned int>(item.get<int>()),
                   config.virtual_extruders
            );
    };

    std::vector<std::string> result;

    for (const Domain::ModelObject* object : model.objects) {
        const auto object_extruder{object->object_settings.items.opt("extruder")};
        if (!accepts_extruder(object_extruder, 0)) {
            result.push_back("extruder");
        }
        for (const Domain::ModelVolume* volume : object->volumes) {
            const auto volume_extruder{volume->volume_settings.overrides.get("extruder")};
            if (volume_extruder && !accepts_extruder(*volume_extruder, 0)) {
                result.push_back("extruder");
            }
        }

        for (const VolumeSettings& height_range_settings :
             object->layer_config_ranges | std::views::values)
        {
            const auto height_range_extruder{height_range_settings.overrides.get("extruder")};
            if (height_range_extruder && !accepts_extruder(*height_range_extruder, 0)) {
                result.push_back("extruder");
            }
        }
    }

    for (const char* key : {"perimeter_extruder", "infill_extruder", "solid_infill_extruder"}) {
        if (!accepts_extruder(config.print.items.opt(key), 1)) {
            result.push_back(key);
        }

        for (const Domain::ModelObject* object : model.objects) {
            const auto object_extruder{object->object_settings.overrides.get(key)};
            if (object_extruder && !accepts_extruder(*object_extruder, 1)) {
                result.push_back(key);
            }
            for (const Domain::ModelVolume* volume : object->volumes) {
                const auto volume_extruder{volume->volume_settings.overrides.get(key)};
                if (volume_extruder && !accepts_extruder(*volume_extruder, 1)) {
                    result.push_back(key);
                }
            }

            for (const VolumeSettings& height_range_settings :
                 object->layer_config_ranges | std::views::values)
            {
                const auto height_range_extruder{height_range_settings.overrides.get(key)};
                if (height_range_extruder && !accepts_extruder(*height_range_extruder, 1)) {
                    result.push_back(key);
                }
            }
        }
    }

    check_virtual_extruder_roles(model, config, result);

    if (!in_range(config.print.items.opt("wipe_tower_extruder"), 0, slot_count)) {
        result.push_back("wipe_tower_extruder");
    }

    for (const char* key : {"support_material_extruder", "support_material_interface_extruder"}) {
        if (!in_range(config.print.items.opt(key), 0, slot_count)) {
            result.push_back(key);
        }
        for (const Domain::ModelObject* object : model.objects) {
            const auto object_extruder{object->object_settings.overrides.get(key)};
            if (object_extruder && !in_range(*object_extruder, 0, slot_count)) {
                result.push_back(key);
            }
        }
    }

    if (result.empty()) {
        return std::nullopt;
    }

    sort_remove_duplicates(result);

    using Biz::Slicing::Error;
    using Biz::Slicing::ErrorCode;
    return Error{ErrorCode::InvalidExtruders, result};
}

std::vector<Biz::Slicing::Error> validate_input(
    const Domain::Model& model,
    const Domain::ConfigPackFDM& config,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    using Biz::Slicing::Error;
    using Biz::Slicing::ErrorCode;

    const bool multi_extruder{
        Domain::Preset::get_feature<bool>(hw_config.features, "multi_extruder").value_or(false)
    };
    if (!multi_extruder) {
        if (config.tool.size() != 1) {
            return {Error{ErrorCode::InconsistentConfig}};
        }
        if (!config.tool.front().overrides.empty()) {
            return {Error{ErrorCode::ToolOverridesInSingleToolPrint}};
        }
    }

    std::vector<Biz::Slicing::Error> errors;
    if (auto error{check_extruder_offset(model, config)}) {
        errors.push_back(std::move(*error));
    }
    if (hw_config.material_slot_count() != 1) {
        if (auto error{check_extruders(model, config, hw_config)}) {
            errors.push_back(std::move(*error));
        }
    }
    return errors;
}

static bool is_any_printable(const std::vector<Domain::ModelObject*>& objects)
{
    for (const Domain::ModelObject* object : objects) {
        for (const Domain::ModelInstance* instance : object->instances) {
            if (instance->printable) {
                return true;
            }
        }
    }
    return false;
}

Biz::Slicing::ApplyStatus::Status Print::update(
    Domain::Model& model,
    const ConfigPack& config,
    const Domain::BedInstance& bed,
    const Domain::Preset::SelectedPresetMetadata& metadata,
    const MetadataSerializeFn& serializer
)
{
    namespace ApplyStatus = Biz::Slicing::ApplyStatus;

    ApplyStatus::Status result{ApplyStatus::Unchanged{}};
    Biz::Slicing::with_limited_instances(model, bed.model_instances, [&]() {
        const InstanceTransformations original_transformations{
            transform_instances(model, bed.transformation.get_matrix().inverse())
        };
        ScopeGuard guard{
            [&]() {
                restore_instance_transformations(model, original_transformations);
            }
        };

        const auto& config_fdm{std::get<ConfigPackFDM>(config)};

        // Extruder candidates need to be checked before validation, to update the UI
        // even in case of InvalidData.
        const std::vector<unsigned> extruder_candidates{
            metadata.hw_config.material_slot_count() == 1 ?
                std::vector<unsigned>{0} :
                Biz::Slicing::get_extruder_candidates(model, config_fdm, bed, config_fdm.virtual_extruders)
        };
        // Backend needs extruder_candidates for MMU, but in Biz/App extruder candidates
        // has to correspond to tool count
        m_on_extruder_candidates(
            metadata.hw_config.tool_count == 1 ? std::vector<unsigned>{0} : extruder_candidates
        );

        std::vector<Biz::Slicing::Error> errors{
            validate_input(model, config_fdm, metadata.hw_config)
        };
        if (!errors.empty()) {
            m_invalid = true;
            result = ApplyStatus::InvalidData{std::move(errors)};
            return;
        }

        const auto slicing_input{
            prepare_slicing_input(config_fdm, extruder_candidates, metadata.hw_config)
        };
        if (!slicing_input.has_value()) {
            m_invalid = true;
            result = ApplyStatus::InvalidData{std::move(slicing_input.error())};
            return;
        }

        result = this->apply(
            model,
            slicing_input.value(),
            metadata,
            serializer,
            bed.wipe_tower,
            bed.custom_gcode,
            extruder_candidates
        );
        if (!is_any_printable(model.objects)) {
            result = ApplyStatus::Empty{};
            return;
        }
        if (
            std::holds_alternative<ApplyStatus::Changed>(result)
            || (m_invalid && std::holds_alternative<ApplyStatus::Unchanged>(result))
        ) {
            Biz::Slicing::ValidationResult validation_result{validate()};
            if (!validation_result.errors.empty()) {
                result = ApplyStatus::InvalidData{std::move(validation_result.errors)};
                return;
            }
            for (const Biz::Slicing::Warning& warning : validation_result.warnings) {
                std::get<ApplyStatus::Changed>(result).warrnings.push_back(warning);
            }
        }
    });

    if (std::holds_alternative<ApplyStatus::InvalidData>(result)) {
        m_invalid = true;
    } else {
        if (m_invalid && std::holds_alternative<ApplyStatus::Unchanged>(result)) {
            result = ApplyStatus::Changed{};
        }
        m_invalid = false;
    }

    if (std::holds_alternative<ApplyStatus::Changed>(result) && m_pre_preview) {
        if (m_pre_preview->invalidate(*this)) {
            m_on_fdm_result(m_pre_preview->generate_result());
        }
    } else if (!std::holds_alternative<ApplyStatus::Unchanged>(result)) {
        m_on_fdm_result({});
    }

    return result;
}

bool Print::invalidate_step(FDMPrintStep step)
{
	return Inherited::invalidate_step(step);
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(FDMPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(m_print_regions.size() * m_objects.size() * 3);
    for (const PrintObject *object : m_objects)
		for (const PrintRegion &region : object->all_regions())
        	region.collect_object_printing_extruders(*this, extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;
    auto num_extruders = (unsigned int)m_config.hw_config().material_slot_count();

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
        	assert(object->config().get<int>("support_material_extruder") >= 0);
            if (object->config().get<int>("support_material_extruder") == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().get<int>("support_material_extruder") - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        	assert(object->config().get<int>("support_material_interface_extruder") >= 0);
            if (object->config().get<int>("support_material_interface_extruder") == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().get<int>("support_material_interface_extruder") - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        }
    }

    if (support_uses_current_extruder)
        // Add all object extruders to the support extruders as it is not know which one will be used to print supports.
        append(extruders, this->object_extruders());
    
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::extruders() const
{
    std::vector<unsigned int> extruders = this->object_extruders();
    append(extruders, this->support_material_extruders());
    sort_remove_duplicates(extruders);

    // The wipe tower extruder can also be set. When the wipe tower is enabled and it will be generated,
    // append its extruder into the list too.
    if (can_have_wipe_tower() && config().get<int>("wipe_tower_extruder") != 0 && extruders.size() > 1) {
        assert(
            config().get<int>("wipe_tower_extruder") > 0
            && config().get<int>("wipe_tower_extruder")
                <= int(config().hw_config().material_slot_count())
        );
        extruders.emplace_back(config().get<int>("wipe_tower_extruder") - 1); // the config value is 1-based
        sort_remove_duplicates(extruders);
    }

    return extruders;
}

unsigned int Print::num_object_instances() const
{
	unsigned int instances = 0;
    for (const PrintObject *print_object : m_objects)
        instances += (unsigned int)print_object->instances().size();
    return instances;
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max{};
    for (unsigned int extruder_id : this->extruders()) {
        const double nozzle_diameter{
            Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), extruder_id)
        };
        nozzle_diameter_max = std::max(nozzle_diameter_max, nozzle_diameter);
    }
    return nozzle_diameter_max;
}

std::vector<Domain::ObjectID> Print::print_object_ids() const
{ 
    std::vector<Domain::ObjectID> out;
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const PrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

bool Print::has_infinite_skirt() const
{
    return (m_config.get<Domain::DraftShield>("draft_shield") == Domain::DraftShield::dsEnabled && m_config.get<int>("skirts") > 0)/* || (m_config.ooze_prevention && this->extruders().size() > 1)*/;
}

bool Print::has_skirt() const
{
    return (m_config.get<int>("skirt_height") > 0 && m_config.get<int>("skirts") > 0) || has_infinite_skirt();
    // case dsLimited should only be taken into account when skirt_height and skirts are positive,
    // so it is covered by the first condition.
}

bool Print::has_brim() const
{
    return std::any_of(m_objects.begin(), m_objects.end(), [](PrintObject *object) { return object->has_brim(); });
}


// Matches "G92 E0" with various forms of writing the zero and with an optional comment.
boost::regex regex_g92e0 { "^[ \\t]*[gG]92[ \\t]*[eE](0(\\.0*)?|\\.0+)[ \\t]*(;.*)?$" };

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
Biz::Slicing::ValidationResult Print::validate() const
{
    using Biz::Slicing::Error;
    using Biz::Slicing::ErrorCode;
    using Biz::Slicing::Warning;
    using Biz::Slicing::WarningCode;

    Biz::Slicing::ValidationResult result;

    std::vector<Warning>& warnings{result.warnings};
    std::vector<Error>& errors{result.errors};

    std::vector<unsigned int> extruders = this->extruders();

    const std::size_t material_slot_count{
        config().hw_config().material_slot_count()
    };
    for (const unsigned extruder : extruders) {
        // Invalid extruders should be hard rejected right away, this is just a sanity check.
        ASSERT(0 <= extruder && extruder <= material_slot_count);
    }

    if (m_config.get<int>("bed_temperature_extruder") == 0) {
        for (size_t a = 0; a < extruders.size(); ++a) {
            for (size_t b = a + 1; b < extruders.size(); ++b) {
                if (std::abs(
                        m_config.get<std::vector<int>>("bed_temperature").at(extruders[a])
                        - m_config.get<std::vector<int>>("bed_temperature").at(extruders[b])
                    ) > 15
                    || std::abs(
                           m_config.get<std::vector<int>>("first_layer_bed_temperature")
                               .at(extruders[a])
                           - m_config.get<std::vector<int>>("first_layer_bed_temperature")
                                 .at(extruders[b])
                       ) > 15)
                {
                    warnings.emplace_back(Warning{WarningCode::BedTempsDiffer});
                    goto DONE;
                }
            }
        }

DONE:;
    }

    if (extruders.empty() && !m_objects.empty()) {
        errors.push_back(Error{ErrorCode::NoExtruders});
    }

    if (m_config.get<bool>("avoid_crossing_perimeters")
        && m_config.get<bool>("avoid_crossing_curled_overhangs"))
    {
        errors.push_back(
            Error{
                ErrorCode::AvoidCrossingPerimetersAndAvoidCurledOverhangs,
                {"avoid_crossing_perimeters", "avoid_crossing_curled_overhangs"}
            }
        );
    }

    if (m_config.get<bool>("spiral_vase")) {
        size_t total_copies_count = 0;
        for (const PrintObject* object : m_objects)
            total_copies_count += object->instances().size();
        // #4043
        if (total_copies_count > 1 && !m_config.get<bool>("complete_objects")) {
            errors.push_back(Error{ErrorCode::SpiralVaseMultipleObjects, {"spiral_vase"}});
        }
        ASSERT(m_objects.size() == 1);
        if (m_objects.front()->all_regions().size() > 1) {
            errors.push_back(Error{ErrorCode::SpiralVaseMultipleMaterials, {"spiral_vase"}});
        }
    }

    if (m_config.get<Domain::MachineLimitsUsage>("machine_limits_usage")
            == Domain::MachineLimitsUsage::EmitToGCode
        && m_config.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfKlipper)
    {
        errors.push_back(
            Error{ErrorCode::MachineLimitsWithKlipper, {"machine_limits_usage", "gcode_flavor"}}
        );
    }

    if (! check_custom_parameters(m_config.get<std::string>("custom_parameters_print"), m_config.get<std::string>("custom_parameters_printer"), m_config.get<std::vector<std::string>>("custom_parameters_filament")))
        errors.push_back(Error{ErrorCode::FailedToParseCustomParameters});

    // Cache of layer height profiles for checking:
    // 1) Whether all layers are synchronized if printing with wipe tower and / or unsynchronized supports.
    // 2) Whether layer height is constant for Organic supports.
    // 3) Whether build volume Z is not violated.
    std::vector<Domain::ZHeightPairs> layer_height_profiles;
    auto layer_height_profile =
        [this, &layer_height_profiles](const size_t print_object_idx) -> const Domain::ZHeightPairs&
    {
        const PrintObject& print_object = *m_objects[print_object_idx];
        if (layer_height_profiles.empty())
            layer_height_profiles.assign(m_objects.size(), Domain::ZHeightPairs());
        Domain::ZHeightPairs& profile = layer_height_profiles[print_object_idx];
        if (profile.empty())
            PrintObject::update_layer_height_profile(
                *print_object.model_object(),
                print_object.slicing_parameters(),
                profile
            );
        return profile;
    };

    // Checks that the print does not exceed the max print height
    for (size_t print_object_idx = 0; print_object_idx < m_objects.size(); ++print_object_idx) {
        const PrintObject& print_object = *m_objects[print_object_idx];
        // FIXME It is quite expensive to generate object layers just to get the print height!
        if (auto layers = generate_object_layers(
                print_object.slicing_parameters(),
                layer_height_profile(print_object_idx)
            );
            !layers.empty()
            && layers.back().top_z > this->config().get<double>("max_print_height") + EPSILON)
        {
            const double shrinkage_compensation_z = this->m_shrinkage_compensation.z();
            if (shrinkage_compensation_z != 1.
                && layers.back().top_z
                    > (this->config().get<double>("max_print_height") / shrinkage_compensation_z
                       + EPSILON))
            {
                // The object exceeds the maximum build volume height because of shrinkage compensation.
                errors.push_back(
                    Error{
                        ErrorCode::ShrinkageCompensationExceedsHeight,
                        {"max_print_height", "filament_shrinkage_compensation_z"},
                        print_object.model_object()->id()
                    }
                );
            } else if (layers.back().middle_z()
                       > this->config().get<double>("max_print_height") + EPSILON)
            {
                // The last slicing plane is below the print volume.
                errors.push_back(
                    Error{
                        ErrorCode::ObjectExceedsHeight,
                        {"max_print_height"},
                        print_object.model_object()->id()
                    }
                );
            } else {
                // The last slicing plane is above the print volume.
                errors.push_back(
                    Error{
                        ErrorCode::LayerExceedsHeight,
                        {"max_print_height"},
                        print_object.model_object()->id()
                    }
                );
            }
        }
    }

    // Some of the objects has variable layer height applied by painting or by a table.
    bool has_custom_layering =
        std::find_if(
            m_objects.begin(),
            m_objects.end(),
            [](const PrintObject* object) { return object->model_object()->has_custom_layering(); }
        )
        != m_objects.end();

    // Custom layering is not allowed for tree supports as of now.
    for (size_t print_object_idx = 0; print_object_idx < m_objects.size(); ++print_object_idx) {
        if (const PrintObject& print_object = *m_objects[print_object_idx];
            print_object.has_support_material()
            && print_object.config().get<Domain::SupportMaterialStyle>("support_material_style")
                == Domain::SupportMaterialStyle::smsOrganic
            && print_object.model_object()->has_custom_layering())
        {
            if (const Domain::ZHeightPairs& layers = layer_height_profile(print_object_idx);
                !layers.empty())
            {
                const SlicingParameters& slicing_parameters = print_object.slicing_parameters();
                if (!check_object_layers_fixed(
                        slicing_parameters.layer_height,
                        slicing_parameters.first_object_layer_height,
                        layers
                    ))
                {
                    errors.push_back(
                        Error{
                            ErrorCode::VariableLayerHeightAndOrganicSupports,
                            {"support_material_style"}
                        }
                    );
                }
            }
        }
    }

    if (this->can_have_wipe_tower() && !m_objects.empty()) {
        // Make sure all extruders use same diameter filament and have the same nozzle diameter
        // EPSILON comparison is used for nozzles and 10 % tolerance is used for filaments
        double first_nozzle_diam =
            Slicing::get_nozzle_diameter(m_config.hw_config(), extruders.front());
        double first_filament_diam =
            m_config.get<std::vector<double>>("filament_diameter").at(extruders.front());

        bool nozzle_diameter_warning_emitted = true;
        for (const auto& extruder_idx : extruders) {
            double nozzle_diam =
                Slicing::get_nozzle_diameter(m_config.hw_config(), extruder_idx);
            double filament_diam =
                m_config.get<std::vector<double>>("filament_diameter").at(extruder_idx);
            if (nozzle_diameter_warning_emitted
                && (nozzle_diam - EPSILON > first_nozzle_diam
                    || nozzle_diam + EPSILON < first_nozzle_diam))
            {
                nozzle_diameter_warning_emitted = false;
                warnings.emplace_back(Warning{WarningCode::WipeTowerNozzleDiameterDiffer});
            } else if (std::abs((filament_diam - first_filament_diam) / first_filament_diam) > 0.1)
            {
                errors.push_back(
                    Error{
                        ErrorCode::WipeTowerDifferentExtruderDiameters,
                        {"filament_diameter"}
                    }
                );
            }
        }

        if (m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfRepRapSprinter
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfRepRapFirmware
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfRepetier
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfMarlinLegacy
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfMarlinFirmware
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfPrusaFirmwareBuddy
            && m_config.get<GCodeFlavor>("gcode_flavor") != GCodeFlavor::gcfKlipper)
        {
            errors.push_back(Error{ErrorCode::WipeTowerGCodeFlavor, {"gcode_flavor"}});
        }

        if (!m_config.get<bool>("use_relative_e_distances")) {
            errors.push_back(
                Error{ErrorCode::WipeTowerAbsoluteDistances, {"use_relative_e_distances"}}
            );
        }
        if (m_config.get<bool>("ooze_prevention")
            && m_config.get<bool>("single_extruder_multi_material"))
        {
            errors.push_back(
                Error{
                    ErrorCode::WipeTowerOozePreventionSingleExtruderMultiMaterial,
                    {"ooze_prevention", "single_extruder_multi_material"}
                }
            );
        }
        if (m_config.get<bool>("use_volumetric_e")) {
            errors.push_back(Error{ErrorCode::WipeTowerVolumetricE, {"use_volumetric_e"}});
        }
        if (m_config.get<bool>("complete_objects") && extruders.size() > 1) {
            errors.push_back(Error{ErrorCode::WipeTowerSequentialPrint, {"complete_objects"}});
        }

        if (m_objects.size() > 1) {
            const SlicingParameters& slicing_params0 = m_objects.front()->slicing_parameters();
            size_t tallest_object_idx                = 0;
            for (size_t i = 1; i < m_objects.size(); ++i) {
                const PrintObject* object               = m_objects[i];
                const SlicingParameters& slicing_params = object->slicing_parameters();
                if (std::abs(
                        slicing_params.first_print_layer_height
                        - slicing_params0.first_print_layer_height
                    ) > EPSILON
                    || std::abs(slicing_params.layer_height - slicing_params0.layer_height)
                        > EPSILON)
                {
                    errors.push_back(Error{ErrorCode::WipeTowerDifferentObjectsLayerHeights});
                }
                if (slicing_params.raft_layers() != slicing_params0.raft_layers()) {
                    errors.push_back(Error{ErrorCode::WipeTowerDifferentObjectsRaftLayerCounts});
                }
                if (slicing_params0.gap_object_support != slicing_params.gap_object_support
                    || slicing_params0.gap_support_object != slicing_params.gap_support_object)
                {
                    errors.push_back(
                        Error{ErrorCode::WipeTowerDifferentObjectsSupportContactDistance}
                    );
                }
                if (!equal_layering(slicing_params, slicing_params0)) {
                    errors.push_back(Error{ErrorCode::WipeTowerDifferentObjectsSlicing});
                }
                if (has_custom_layering) {
                    auto& lh         = layer_height_profile(i);
                    auto& lh_tallest = layer_height_profile(tallest_object_idx);
                    if (lh.back().z > lh_tallest.back().z)
                        tallest_object_idx = i;
                }
            }

            if (has_custom_layering) {
                for (size_t idx_object = 0; idx_object < m_objects.size(); ++idx_object) {
                    if (idx_object == tallest_object_idx)
                        continue;
                    // Check that the layer height profiles are equal. This will happen when one object is
                    // a copy of another, or when a layer height modifier is used the same way on both objects.
                    // The latter case might create a floating point inaccuracy mismatch, so compare
                    // element-wise using an epsilon check.
                    size_t i         = 0;
                    const double eps = 0.5
                        * EPSILON; // layers closer than EPSILON will be merged later. Let's make
                    // this check a bit more sensitive to make sure we never consider two different layers as one.
                    const Domain::ZHeightPairs& profile_object = layer_height_profiles[idx_object];
                    const Domain::ZHeightPairs& profile_tallest_object =
                        layer_height_profiles[tallest_object_idx];
                    while (i < profile_object.size() && i < profile_tallest_object.size()) {
                        if (profile_tallest_object[i].z > profile_object.back().z) {
                            break;
                        }

                        if (std::abs(profile_object[i].z - profile_tallest_object[i].z) > eps
                            || std::abs(
                                   profile_object[i].layer_height
                                   - profile_tallest_object[i].layer_height
                               ) > eps)
                        {
                            errors.push_back(
                                Error{ErrorCode::WipeTowerDifferentObjectsVariableHeight}
                            );
                        }

                        ++i;
                    }
                }
            }
        }
    }

    {
        // Find the smallest used nozzle diameter and the number of unique nozzle diameters.
        double min_nozzle_diameter = std::numeric_limits<double>::max();
        double max_nozzle_diameter = 0;
        for (unsigned int extruder_id : extruders) {
            double dmr = Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), extruder_id);
            min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        }

        auto validate_extrusion_width = [min_nozzle_diameter, max_nozzle_diameter](
                                            const Domain::ConfigView& config,
                                            const char* opt_key,
                                            double layer_height,
                                            Error& error
                                        ) -> bool
        {
            const double extrusion_width_min =
                config.get<Domain::FloatOrPercentage>(opt_key).get_abs_value(min_nozzle_diameter);
            const double extrusion_width_max =
                config.get<Domain::FloatOrPercentage>(opt_key).get_abs_value(max_nozzle_diameter);
            if (extrusion_width_min == 0) {
                // Default "auto-generated" extrusion width is always valid.
            } else if (extrusion_width_min <= layer_height) {
                error = Error{ErrorCode::InsufficientExtrusionWidth, {opt_key}};
                return false;
            } else if (extrusion_width_max >= max_nozzle_diameter * 3.) {
                error = Error{ErrorCode::ExcesiveExtrusionWidth, {opt_key}};
                return false;
            }
            return true;
        };

        auto validate_extrusion_width_vector = [](const std::vector<double>& nozzle_diameters,
                                                  const Domain::ConfigView& config,
                                                  const char* opt_key,
                                                  double layer_height,
                                                  Error& error) -> bool
        {
            const auto extrusion_width =
                config.get<std::vector<Domain::FloatOrPercentage>>(opt_key);

            for (std::size_t tool_index{}; tool_index < nozzle_diameters.size(); ++tool_index) {
                double diameter{nozzle_diameters[tool_index]};
                double width{extrusion_width[tool_index].get_abs_value(diameter)};

                if (width == 0) {
                    // Default "auto-generated" extrusion width is always valid.
                } else if (width <= layer_height) {
                    error = Error{ErrorCode::InsufficientExtrusionWidth, {opt_key}};
                    return false;
                } else if (width >= diameter * 3.) {
                    error = Error{ErrorCode::ExcesiveExtrusionWidth, {opt_key}};
                    return false;
                }
            }

            return true;
        };

        for (PrintObject* object : m_objects) {
            if (object->has_support_material()) {
                if ((object->config().get<int>("support_material_extruder") == 0
                     || object->config().get<int>("support_material_interface_extruder") == 0)
                    && max_nozzle_diameter - min_nozzle_diameter > EPSILON)
                {
                    // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                    // will be printed with the current tool without a forced tool change.
                    // Notify the user that printing supports with different nozzle diameters is experimental and requires caution.
                    warnings.emplace_back(Warning{WarningCode::SupportNozzleDiameterDiffer});
                }
                if (this->can_have_wipe_tower()
                    && object->config().get<Domain::SupportMaterialStyle>("support_material_style")
                        != Domain::SupportMaterialStyle::smsOrganic)
                {
                    if (object->config().get<double>("support_material_contact_distance") == 0) {
                        // Soluble interface
                        if (!object->config().get<bool>("support_material_synchronize_layers")) {
                            errors.push_back(
                                Error{
                                    ErrorCode::WipeTowerSoluableUnsynchronizedLayers,
                                    {"support_material_synchronize_layers"}
                                }
                            );
                        }
                    } else {
                        // Non-soluble interface
                        if (object->config().get<int>("support_material_extruder") != 0
                            || object->config().get<int>("support_material_interface_extruder")
                                != 0)
                        {
                            errors.push_back(
                                Error{
                                    ErrorCode::WipeTowerSupporMaterialExtruderSet,
                                    {"support_material_extruder",
                                     "support_material_interface_extruder"}
                                }
                            );
                        }
                    }
                }
                if (object->config().get<Domain::SupportMaterialStyle>("support_material_style")
                    == Domain::SupportMaterialStyle::smsOrganic)
                {
                    float extrusion_width = std::min(
                        support_material_flow(object).width(),
                        support_material_interface_flow(object).width()
                    );
                    if (object->config().get<double>("support_tree_tip_diameter")
                        < extrusion_width - EPSILON)
                    {
                        errors.push_back(
                            Error{
                                ErrorCode::OrganicSupportTipTooSmall,
                                {"support_tree_tip_diameter"}
                            }
                        );
                    }
                    if (object->config().get<double>("support_tree_branch_diameter")
                        < 2. * extrusion_width - EPSILON)
                    {
                        errors.push_back(
                            Error{
                                ErrorCode::OrganicSupportBranchDiameterSmallerThanSupportMaterial,
                                {"support_tree_branch_diameter"}
                            }
                        );
                    }
                    if (object->config().get<double>("support_tree_branch_diameter")
                        < object->config().get<double>("support_tree_tip_diameter"))
                    {
                        errors.push_back(
                            Error{
                                ErrorCode::OrganicSupportBranchDiameterSmallerThanTreeTip,
                                {"support_tree_branch_diameter"}
                            }
                        );
                    }
                }
            }

            // Do we have custom support data that would not be used?
            // Notify the user in that case.
            if (!object->has_support() && object->has_support_enforcers()) {
                warnings.emplace_back(Warning{WarningCode::SupportsTurnedOff});
            }

            // validate first_layer_height
            assert(!m_config.get<Domain::FloatOrPercentage>("first_layer_height").is_percentage());
            double layer_height = m_config.get<double>("layer_height");
            double first_layer_height =
                m_config.get<Domain::FloatOrPercentage>("first_layer_height")
                    .get_abs_value(layer_height);
            double first_layer_min_nozzle_diameter;
            if (object->has_raft()) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder     = object->config().get<int>("raft_layers") == 1 ?
                        object->config().get<int>("support_material_interface_extruder") - 1 :
                        object->config().get<int>("support_material_extruder") - 1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ?
                    min_nozzle_diameter :
                    Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter) {
                errors.push_back(
                    Error{ErrorCode::FirstLayerHeightTooLarge, {"first_layer_height"}}
                );
            }

            // validate layer_height
            if (layer_height > min_nozzle_diameter) {
                errors.push_back(Error{ErrorCode::LayerHeightTooLarge, {"layer_height"}});
            }

            const std::vector<double> nozzle_diameters{
                Biz::Slicing::get_nozzle_diameters(config().hw_config())
            };

            // Validate extrusion widths.
            Error error;
            if (!validate_extrusion_width_vector(
                    nozzle_diameters,
                    object->config(),
                    "extrusion_width",
                    layer_height,
                    error
                ))
            {
                errors.push_back(error);
            }
            if ((object->has_support() || object->has_raft())
                && !validate_extrusion_width(
                    object->config(),
                    "support_material_extrusion_width",
                    layer_height,
                    error
                ))
            {
                errors.push_back(error);
            }
            for (const char* opt_key :
                 {"perimeter_extrusion_width",
                  "external_perimeter_extrusion_width",
                  "infill_extrusion_width",
                  "solid_infill_extrusion_width",
                  "top_infill_extrusion_width"})
            {
                for (const PrintRegion& region : object->all_regions()) {
                    if (!validate_extrusion_width_vector(
                            nozzle_diameters,
                            region.config(),
                            opt_key,
                            layer_height,
                            error
                        ))
                    {
                        errors.push_back(error);
                    }
                }
            }
        }
    }
    {
        bool before_layer_gcode_resets_extruder =
            boost::regex_search(m_config.get<std::string>("before_layer_gcode"), regex_g92e0);
        bool layer_gcode_resets_extruder =
            boost::regex_search(m_config.get<std::string>("layer_gcode"), regex_g92e0);
        if (m_config.get<bool>("use_relative_e_distances")) {
            // See GH issues #6336 #5073
            if ((m_config.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMarlinLegacy
                 || m_config.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMarlinFirmware
                 || m_config.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfPrusaFirmwareBuddy)
                && !before_layer_gcode_resets_extruder
                && !layer_gcode_resets_extruder)
            {
                errors.push_back(
                    Error{ErrorCode::MissingG92E0, {"use_relative_e_distances", "layer_gcode"}}
                );
            }
        } else if (before_layer_gcode_resets_extruder) {
            errors.push_back(
                Error{
                    ErrorCode::FoundG92E0InBeforeLayerGCode,
                    {"use_relative_e_distances", "before_layer_gcode"}
                }
            );
        } else if (layer_gcode_resets_extruder) {
            errors.push_back(
                Error{
                    ErrorCode::FoundG92E0InLayerGCode,
                    {"use_relative_e_distances", "layer_gcode"}
                }
            );
        }
    }

    return result;
}

double Print::skirt_first_layer_height() const
{
    assert(! m_config.get<Domain::FloatOrPercentage>("first_layer_height").is_percentage());

    const auto layer_height{m_config.get<double>("layer_height")};
    return m_config.get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(layer_height);
}

Flow Print::brim_flow() const
{
    const int extruder_id{static_cast<int>(m_print_regions.front()->extruder(frPerimeter)) - 1};
    ASSERT(extruder_id >= 0);

    Domain::FloatOrPercentage width = m_config.get<std::vector<Domain::FloatOrPercentage>>("first_layer_extrusion_width").at(extruder_id);
    if (width.is_zero())
        width = m_print_regions.front()
                    ->config()
                    .get<std::vector<Domain::FloatOrPercentage>>("perimeter_extrusion_width")
                    .at(extruder_id);
    if (width.is_zero())
        width = m_objects.front()->config().get<std::vector<Domain::FloatOrPercentage>>("extrusion_width").at(extruder_id);

    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
        (float)Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), extruder_id),
		(float)this->skirt_first_layer_height());
}

Flow Print::skirt_flow() const
{
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    // If support_material_extruder == 0 use the 0th nozzle diameter.
    const int support_material_extruder_idx = std::max<int>(m_objects.front()->config().get<int>("support_material_extruder") - 1, 0);

    Domain::FloatOrPercentage width =
        m_config.get<std::vector<Domain::FloatOrPercentage>>("first_layer_extrusion_width")
            .at(support_material_extruder_idx);
    if (width.is_zero())
        width = m_print_regions.front()
                    ->config()
                    .get<std::vector<Domain::FloatOrPercentage>>("perimeter_extrusion_width")
                    .at(support_material_extruder_idx);
    if (width.is_zero())
        width = m_objects.front()
                    ->config()
                    .get<std::vector<Domain::FloatOrPercentage>>("extrusion_width")
                    .at(support_material_extruder_idx);

    return Flow::new_from_config_width(
        frPerimeter,
		width,
		(float)Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), support_material_extruder_idx),
		(float)this->skirt_first_layer_height());
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : m_objects)
        if (object->has_support_material()) 
            return true;
    return false;
}

Biz::Slicing::WipeTowerGeometry get_wipe_tower_geometry(const WipeTowerData& wipe_tower_data) {
    using Biz::Slicing::ZDepth;

    Biz::Slicing::WipeTowerGeometry result;
    result.depths.reserve(wipe_tower_data.z_and_depth_pairs.size());
    std::transform(
        std::begin(wipe_tower_data.z_and_depth_pairs), std::end(wipe_tower_data.z_and_depth_pairs),
        std::back_inserter(result.depths), [](const std::pair<float, float>& z_depth){
            return ZDepth{z_depth.first, z_depth.second};
        }
    );
    result.width = wipe_tower_data.width;
    result.brim_width = wipe_tower_data.brim_width;
    result.cone_radius = wipe_tower_data.cone_radius;
    result.cone_x_scale = wipe_tower_data.cone_x_scale;

    return result;
}

GeneratedSupportPointsSnapshot get_generated_support_points(const PrintObjectPtrs& print_objects)
{
    GeneratedSupportPointsSnapshot result;
    std::set<ObjectID> visited_model_objects;
    for (const PrintObject* print_object : print_objects) {
        const ObjectID model_object_id = print_object->model_object()->id();
        const std::optional<PrintObjectRegions::GeneratedSupportPoints>& support_points =
            print_object->shared_regions()->generated_support_points;

        if (!support_points.has_value() || !visited_model_objects.insert(model_object_id).second) {
            continue;
        }

        ObjectSupportPoints object_support_points{
            model_object_id,
            support_points->object_transform
        };

        object_support_points.support_points.reserve(support_points->support_points.size());
        for (const SupportSpotsGenerator::SupportPoint& support_point :
             support_points->support_points)
        {
            object_support_points.support_points.push_back(
                GeneratedSupportPoint{support_point.position, support_point.spot_radius}
            );
        }

        result.push_back(std::move(object_support_points));
    }

    return result;
}

// Slicing process, running at a background thread.
void Print::process()
{
    name_tbb_thread_pool_threads_set_locale();

    SPDLOG_INFO("Starting the slicing process. {}", log_memory_info());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_objects.size(), 1), [this](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            m_objects[idx]->make_perimeters();
            m_objects[idx]->infill();
            m_objects[idx]->ironing();
        }
    }, tbb::simple_partitioner());

    // The following step writes to m_shared_regions, it should not run in parallel.
    for (PrintObject *obj : m_objects)
        obj->generate_support_spots();
    // check data from previous step, format the error message(s) and send alert to ui
    // this also has to be done sequentially.
    alert_when_supports_needed();

    m_on_generated_support_points(get_generated_support_points(m_objects));

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_objects.size(), 1), [this](const tbb::blocked_range<size_t> &range) {
        for (size_t idx = range.begin(); idx < range.end(); ++idx) {
            PrintObject &obj = *m_objects[idx];
            obj.generate_support_material();
            obj.estimate_curled_extrusions();
            obj.calculate_overhanging_perimeters();
        }
    }, tbb::simple_partitioner());

    if (this->set_started(psWipeTower)) {
        m_wipe_tower_data = std::nullopt;
        m_tool_ordering.clear();
        if (this->can_have_wipe_tower()) {
            //this->set_status(95, _u8L("Generating wipe tower"));

            // Modifies tool ordering!
            m_wipe_tower_data = this->generate_wipe_tower_data();
        } else if (! this->config().get<bool>("complete_objects")) {
        	// Initialize the tool ordering, so it could be used by the G-code preview slider for planning tool changes and filament switches.
        	m_tool_ordering = ToolOrdering(*this, -1, false);
            if (m_tool_ordering.empty() || m_tool_ordering.last_extruder() == unsigned(-1)) {
                throw Biz::Slicing::Exception{
                    Biz::Slicing::Error{Biz::Slicing::ErrorCode::EmptyPrint}
                };
            }
        }
        this->set_done(psWipeTower);
    }
    if (this->set_started(psSkirtBrim)) {
        this->set_status(Domain::Percentage{88}, Biz::Slicing::ProgressInfo::GeneratingSkirtAndBrim);

        m_skirt.clear();
        m_skirt_convex_hull.clear();
        m_first_layer_convex_hull.points.clear();
        const bool draft_shield = config().get<Domain::DraftShield>("draft_shield") != Domain::DraftShield::dsDisabled;

        if (this->has_skirt() && draft_shield) {
            // In case that draft shield is active, generate skirt first so brim
            // can be trimmed to make room for it.
            _make_skirt();
        }

        m_brim.clear();
        m_first_layer_convex_hull.points.clear();
        if (this->has_brim()) {
            Polygons islands_area;
            m_brim = make_brim(*this, this->make_try_cancel(), islands_area);
            for (Polygon &poly : union_(this->first_layer_islands(), islands_area))
                append(m_first_layer_convex_hull.points, std::move(poly.points));
        }


        if (has_skirt() && ! draft_shield) {
            // In case that draft shield is NOT active, generate skirt now.
            // It will be placed around the brim, so brim has to be ready.
            assert(m_skirt.empty());
            _make_skirt();
        }

        this->finalize_first_layer_convex_hull();
        this->set_done(psSkirtBrim);
    }

    if (m_wipe_tower_data) {
        // These values have to be updated here, not during wipe tower generation.
        // When the wipe tower is moved/rotated, it is not regenerated.
        m_wipe_tower_data->position = m_wipe_tower->position;
        m_wipe_tower_data->rotation_angle = m_wipe_tower->rotation;
        m_on_wipe_tower_geometry(get_wipe_tower_geometry(*m_wipe_tower_data));
    } else {
        m_on_wipe_tower_geometry(std::nullopt);
    }
    if (auto conflict =
            Biz::Slicing::find_inter_of_lines_in_diff_objs(objects(), m_wipe_tower_data);
        conflict.has_value())
    {
        this->append_warning_callback(Biz::Slicing::Warning{
            .code = Biz::Slicing::WarningCode::GCodeConflict,
            .item_keys = {},
            .model_object_id = std::nullopt,
            .payload = Biz::Slicing::GCodeConflictWarningPayload{
                .object_names = {conflict->obj_name_1, conflict->obj_name_2},
                .height = conflict->height
            },
            .severity = Biz::Slicing::WarningSeverity::HIGH
        });
    }

    m_sequential_collision_detected = config().get<bool>("complete_objects") ? std::nullopt /*check_seq_conflict(model(), config())*/ : std::nullopt;

    SPDLOG_INFO("Slicing process finished. {}", log_memory_info());
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process output_filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
Biz::libpgcode::ProcessorResult Print::process_gcode()
{
    // output everything to a G-code file
    // The following call may die if the output_filename_format template substitution fails.
    std::string message = _u8L("Generating G-code");
    this->set_status(Domain::Percentage{90}, Biz::Slicing::ProgressInfo::GeneratingGCode);

    // Create GCode on heap, it has quite a lot of data.
    std::unique_ptr<GCodeGenerator> gcode(new GCodeGenerator(const_cast<const Print*>(this)));

    // TODO: provide stats stored in result.print_statistics here
    auto serialized_config = m_metadata_serializer(Domain::BasicPrintStatistics{});

    Biz::libpgcode::ProcessorResult result{gcode->do_export(this, serialized_config)};

    result.sequential_collision_detected = m_sequential_collision_detected;

    return result;
}

namespace {
Thumbnails request_thumbnails(
    const Domain::SlicingId& slicing_id,
    const std::string& thumbnails_request,
    Biz::Slicing::IThumbnailImageGenerator& thumbnail_generator
)
{
    using GCodeThumbnails::parse_request;
    using GCodeThumbnails::RequestParsingResult;

    const tl::expected<RequestParsingResult, ThumbnailErrors> request_data{
        parse_request(thumbnails_request)
    };

    if (!request_data.has_value()) {
        throw Biz::Slicing::Exception(Biz::Slicing::Error{
            .code = Biz::Slicing::ErrorCode::InvalidThumbnailRequest, 
            .payload = Biz::Slicing::InvalidThumbnailRequestPayload{
                .invalid_format = request_data.error().has(ThumbnailError::InvalidVal),
                .out_of_range = request_data.error().has(ThumbnailError::OutOfRange),
                .invalid_ext = request_data.error().has(ThumbnailError::InvalidExt)
            }}
        );
    }

    if (request_data->sizes.empty()) {
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Biz::Slicing::ThumbnailImageResults{});
        return {
            .raw_data = promise.get_future(),
            .formats = request_data->formats
        };
    }

    Biz::Slicing::ThumbnailImageRequest request{
        ThumbnailType::SlicingBed,
        Biz::Slicing::ThumbnailParams{
            .project_id              = slicing_id.project_id,
            .bed_instance_id         = slicing_id.bed_instance_id,
            .bed_instance_with_error = false,
            .pixel_format            = Domain::PixelFormat::RGBA8,
            .sizes                   = request_data->sizes
        }
    };

    return {thumbnail_generator.enqueue_thumbnail_requests({request}), request_data->formats};
}

bool check_result(
    const Biz::libpgcode::ProcessorResult& result,
    const PrintConfigView& config,
    std::function<void(Biz::Slicing::Warning)> append_warning_callback
)
{
    const BuildVolume build_volume(
        config.get<std::vector<Vec2d>>("bed_shape"),
        config.get<double>("max_print_height")
    );
    bool ret = build_volume.all_paths_inside(*result.const_moves());
    if (!ret)
        append_warning_callback(Biz::Slicing::Warning{ Biz::Slicing::WarningCode::ToolpathOutsideBuildVolume });
    return ret;
}

} // namespace

void Print::slice(
    SlicingId slicing_id,
    IThumbnailImageGenerator& thumbnail_generator,
    const std::optional<SliceUntilStep> slice_until_step
)
{
    if (this->is_step_done(psGCodeExport)) {
        // This is a special case. Print::slice is not in most cases called when
        // GCodeExport is finished, but in a special case of invalid
        // data it can be re-called even when the state of the
        // backend is that psGCodeExport is finished.
        // In that case GCodeExport needs to be invalidated to
        // re-generate the FDMResult.
        this->invalidate_step(psGCodeExport);
    }

    // Clean up after the processing on every exit path, even the exceptional ones.
    const ScopeGuard finalize_and_cleanup_guard{
        [this]() -> void
        {
            this->finalize();
            this->cleanup();
        }
    };

    if (slice_until_step.has_value()) {
        ASSERT(std::holds_alternative<FDMPrintObjectStep>(slice_until_step->step));
        const FDMPrintObjectStep until_object_step =
            std::get<FDMPrintObjectStep>(slice_until_step->step);

        if (!this->set_task_until_object_step_impl(
                static_cast<int>(until_object_step),
                slice_until_step->model_object_id,
                m_objects
            ))
        {
            // The model object has no print object.
            return;
        }

        this->process();
        return;
    }

    thumbnails = request_thumbnails(slicing_id, config().get<std::string>("thumbnails"), thumbnail_generator);

    ASSERT(
        !this->is_step_done(psGCodeExport),
        "An earlier return should happen, if the whole thing is already finnished!"
    );
    this->process();

    m_pre_preview = std::make_unique<Biz::Slicing::PrePreview>(*this);
    m_on_fdm_result(m_pre_preview->generate_result());
    Biz::libpgcode::ProcessorResult result{this->process_gcode()};
    result.contained_in_bed = check_result(result, config(), append_warning_callback);

    m_on_fdm_result(std::move(result));
}

void Print::_make_skirt()
{
    // First off we need to decide how tall the skirt must be.
    // The skirt_height option from config is expressed in layers, but our
    // object might have different layer heights, so we need to find the print_z
    // of the highest layer involved.
    // Note that unless has_infinite_skirt() == true
    // the actual skirt might not reach this $skirt_height_z value since the print
    // order of objects on each layer is not guaranteed and will not generally
    // include the thickest object first. It is just guaranteed that a skirt is
    // prepended to the first 'n' layers (with 'n' = skirt_height).
    // $skirt_height_z in this case is the highest possible skirt height for safety.
    double skirt_height_z = 0.;
    for (const PrintObject *object : m_objects) {
        size_t skirt_layers = this->has_infinite_skirt() ?
            object->layer_count() : 
            std::min(size_t(m_config.get<int>("skirt_height")), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->m_layers[skirt_layers-1]->print_z);
    }
    
    // Collect points from all layers contained in skirt height.
    Points points;
    for (const PrintObject *object : m_objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->lslices)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            layer->support_fills.collect_points(object_points);
        }
        // Repeat points for each object copy.
        for (const PrintInstance &instance : object->instances()) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += instance.shift();
            append(points, copy_points);
        }
    }

    // Include the wipe tower.
    append(points, this->first_layer_wipe_tower_corners());

    // Unless draft shield is enabled, include all brims as well.
    if (config().get<Domain::DraftShield>("draft_shield") == Domain::DraftShield::dsDisabled)
        append(points, m_first_layer_convex_hull.points);

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;
    
    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);
    
    // Skirt may be printed on several layers, having distinct layer heights,
    // but loops must be aligned so can't vary width/spacing
    // TODO: use each extruder's own flow
    double first_layer_height = this->skirt_first_layer_height();
    Flow   flow = this->skirt_flow();
    float  spacing = flow.spacing();
    double mm3_per_mm = flow.mm3_per_mm();
    
    std::vector<size_t> extruders;
    std::vector<double> extruders_e_per_mm;
    {
        auto set_extruders = this->extruders();
        extruders.reserve(set_extruders.size());
        extruders_e_per_mm.reserve(set_extruders.size());
        for (auto &extruder_id : set_extruders) {
            extruders.push_back(extruder_id);
            Slicing::GCodeWriterConfig temp_config{m_config};

            // TODO: It is absolutely ridiculous to create temp_config just to get e_per_mm.
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &temp_config).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    size_t n_skirts = m_config.get<int>("skirts");
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    auto   distance = float(scale_(m_config.get<double>("skirt_distance") - spacing/2.));
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<double> extruded_length(extruders.size(), 0.);
    for (size_t i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
        // Offset the skirt outside.
        distance += float(scale_(spacing));
        // Generate the skirt centerline.
        Polygon loop;
        {
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, float(scale_(0.1)));
            Biz::Algorithms::Geometry::simplify_polygons(loops, scale_(0.05), &loops);
			if (loops.empty())
				break;
			loop = loops.front();
        }
        // Extrude the skirt loop.
        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(
            ExtrusionAttributes{
                ExtrusionRole::Skirt,
                ExtrusionFlow{
                    float(mm3_per_mm),        // this will be overridden at G-code export time
                    flow.width(),
                    float(first_layer_height) // this will be overridden at G-code export time
                }
            });
        eloop.paths.back().polyline = Algorithms::Polygon::split_at_first_point(loop);
        m_skirt.append(eloop);
        if (m_config.get<double>("min_skirt_length") > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < m_config.get<double>("min_skirt_length")) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= m_config.get<double>("min_skirt_length"));
                // Enough extruded with the current extruder. Extrude with the next one,
                // until the prescribed number of skirt loops is extruded.
                if (extruder_idx + 1 < extruders.size())
                    ++ extruder_idx;
            }
        } else {
            // The skirt lenght is not limited, extrude the skirt with the 1st extruder only.
        }
    }
    // Brims were generated inside out, reverse to print the outmost contour first.
    m_skirt.reverse();

    // Remember the outer edge of the last skirt line extruded as m_skirt_convex_hull.
    for (Polygon &poly : offset(convex_hull, distance + 0.5f * float(scale_(spacing)), ClipperLib::jtRound, float(scale_(0.1))))
        append(m_skirt_convex_hull, std::move(poly.points));
}



Polygons Print::first_layer_islands() const
{
    Polygons islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty())
            object->support_layers().front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift());
            }
    }
    return islands;
}

Points Print::first_layer_wipe_tower_corners() const
{
    Points pts_scaled;

    if (m_wipe_tower_data) {
        const WipeTowerData& wipe_tower_data{*m_wipe_tower_data};
        double width = m_config.get<double>("wipe_tower_width") + 2*wipe_tower_data.brim_width;
        double depth = wipe_tower_data.depth + 2*wipe_tower_data.brim_width;
        Vec2d pt0(-wipe_tower_data.brim_width, -wipe_tower_data.brim_width);

        // First the corners.
        std::vector<Vec2d> pts = { pt0,
                                   Vec2d(pt0.x()+width, pt0.y()),
                                   Vec2d(pt0.x()+width, pt0.y()+depth),
                                   Vec2d(pt0.x(),pt0.y()+depth)
                                 };

        // Now the stabilization cone.
        Vec2d center = (pts[0] + pts[2])/2.;
        const auto& cone_R = m_wipe_tower_data->cone_radius;
        const auto& cone_x_scale = m_wipe_tower_data->cone_x_scale;
        double r = cone_R + wipe_tower_data.brim_width;
        for (double alpha = 0.; alpha<2*M_PI; alpha += M_PI/20.)
            pts.emplace_back(center + r*Vec2d(std::cos(alpha)/cone_x_scale, std::sin(alpha)));

        for (Vec2d& pt : pts) {
            pt = Eigen::Rotation2Dd(deg2rad(wipe_tower()->rotation)) * pt;
            pt += m_wipe_tower->position;
            pts_scaled.emplace_back(scaled(Vec2d(pt.x(), pt.y())));
        }
    }
    return pts_scaled;
}

void Print::finalize_first_layer_convex_hull()
{
    append(m_first_layer_convex_hull.points, m_skirt_convex_hull);
    if (m_first_layer_convex_hull.empty()) {
        // Neither skirt nor brim was extruded. Collect points of printed objects from 1st layer.
        for (Polygon &poly : this->first_layer_islands())
            append(m_first_layer_convex_hull.points, std::move(poly.points));
    }
    append(m_first_layer_convex_hull.points, this->first_layer_wipe_tower_corners());
    m_first_layer_convex_hull = Geometry::convex_hull(m_first_layer_convex_hull.points);
}

void Print::alert_when_supports_needed()
{
    if (this->set_started(psAlertWhenSupportsNeeded)) {
        SPDLOG_DEBUG("psAlertWhenSupportsNeeded - start");
        set_status(Domain::Percentage{69}, Biz::Slicing::ProgressInfo::CheckingStability);

        auto issue_to_alert_message = [](SupportSpotsGenerator::SupportPointCause cause, bool critical) {
            std::string message;
            switch (cause) {
            //TRN Alert when support is needed. Describes that the model has long bridging extrusions which may print badly 
            case SupportSpotsGenerator::SupportPointCause::LongBridge: message = _u8L("Long bridging extrusions"); break;
            //TRN Alert when support is needed. Describes bridge anchors/turns in the air, which will definitely print badly
            case SupportSpotsGenerator::SupportPointCause::FloatingBridgeAnchor: message = _u8L("Floating bridge anchors"); break;
            case SupportSpotsGenerator::SupportPointCause::FloatingExtrusion:
                if (critical) {
                     //TRN Alert when support is needed. Describes that the print has large overhang area which will print badly or not print at all.
                    message = _u8L("Collapsing overhang");
                } else {
                    //TRN Alert when support is needed. Describes extrusions that are not supported enough and come out curled or loose.
                    message = _u8L("Loose extrusions");
                }
                break;
            //TRN Alert when support is needed. Describes that the print has low bed adhesion and may became loose.
            case SupportSpotsGenerator::SupportPointCause::SeparationFromBed: message = _u8L("Low bed adhesion"); break;
            //TRN Alert when support is needed. Describes that the object has part that is not connected to the bed and will not print at all without supports.
            case SupportSpotsGenerator::SupportPointCause::UnstableFloatingPart: message = _u8L("Floating object part"); break;
            //TRN Alert when support is needed. Describes that the object has thin part that may brake during printing 
            case SupportSpotsGenerator::SupportPointCause::WeakObjectPart: message = _u8L("Thin fragile part"); break;
            }

            return message;
        };

        // TRN this translation rule is used to translate lists of uknown size on single line. The first argument is element of the list,
        // the second argument may be element or rest of the list. For most languages, this does not need translation, but some use different 
        // separator than comma and some use blank space in front of the separator.
        auto single_line_list_rule = L("%1%, %2%");
        auto multiline_list_rule   = "%1%\n%2%";

        auto elements_to_translated_list = [](const std::vector<std::string> &translated_elements, std::string expansion_rule) {
            if (expansion_rule.find("%1%") == expansion_rule.npos || expansion_rule.find("%2%") == expansion_rule.npos) {
                SPDLOG_ERROR("INCORRECT EXPANSION RULE FOR LIST TRANSLATION: {} - IT SHOULD CONTAIN %1% and %2%!", expansion_rule);
                expansion_rule = "%1% %2%";
            }
            if (translated_elements.size() == 0) {
                return std::string{};
            }
            if (translated_elements.size() == 1) {
                return translated_elements.front();
            }

            std::string translated_list = expansion_rule;
            for (int i = 0; i < int(translated_elements.size()) - 1; ++ i) {
                auto first_elem = translated_list.find("%1%");
                assert(first_elem != translated_list.npos);
                translated_list.replace(first_elem, 3, translated_elements[i]);

                // expand the translated list by another application of the same rule
                auto second_elem = translated_list.find("%2%");
                assert(second_elem != translated_list.npos);
                if (i < int(translated_elements.size()) - 2) {
                    translated_list.replace(second_elem, 3, expansion_rule);
                } else {
                    translated_list.replace(second_elem, 3, translated_elements[i + 1]);
                }
            }

            return translated_list;
        };

        // vector of pairs of object and its issues, where each issue is a pair of type and critical flag
        std::vector<std::pair<const PrintObject *, std::vector<std::pair<SupportSpotsGenerator::SupportPointCause, bool>>>> objects_isssues;

        // True when support material actually is generated for the object.
        const auto is_support_generated = [](const PrintObject& print_object) -> bool
        {
            const PrintObjectConfigView& config = print_object.config();
            const SupportMode support_mode      = config.get<SupportMode>("support_material");
            if (support_mode == SupportMode::Everywhere) {
                return true;
            } else if (config.get<int>("support_material_enforce_layers") > 0) {
                return true;
            } else if (support_mode == SupportMode::EnforcersOnly) {
                return print_object.has_support_enforcers();
            }

            return false;
        };

        for (const PrintObject* object : m_objects) {
            if (is_support_generated(*object)) {
                continue;
            } else if (!object->m_shared_regions->generated_support_points.has_value()) {
                continue;
            }

            SupportSpotsGenerator::SupportPoints  supp_points = object->m_shared_regions->generated_support_points->support_points;
            SupportSpotsGenerator::PartialObjects partial_objects = object->m_shared_regions->generated_support_points
                                                                        ->partial_objects;
            auto issues = SupportSpotsGenerator::gather_issues(supp_points, partial_objects);
            if (issues.size() > 0) {
                objects_isssues.emplace_back(object, issues);
            }
        }

        bool                                                                                                  recommend_brim = false;
        std::map<std::pair<SupportSpotsGenerator::SupportPointCause, bool>, std::vector<const PrintObject *>> po_by_support_issues;
        for (const auto &obj : objects_isssues) {
            for (const auto &issue : obj.second) {
                po_by_support_issues[issue].push_back(obj.first);
                if (issue.first == SupportSpotsGenerator::SupportPointCause::SeparationFromBed && !obj.first->has_brim()) {
                    recommend_brim = true;
                }
            }
        }

        std::vector<std::pair<std::string, std::vector<std::string>>> message_elements;
        if (objects_isssues.size() > po_by_support_issues.size()) {
            // there are more objects than causes, group by issues
            for (const auto &issue : po_by_support_issues) {
                auto &pair = message_elements.emplace_back(issue_to_alert_message(issue.first.first, issue.first.second),
                                                           std::vector<std::string>{});
                for (const auto &obj : issue.second) {
                    pair.second.push_back(obj->m_model_object->name);
                }
            }
        } else {
            // more causes than objects, group by objects
            for (const auto &obj : objects_isssues) {
                auto &pair = message_elements.emplace_back(obj.first->model_object()->name,  std::vector<std::string>{});
                for (const auto &issue : obj.second) {
                    pair.second.push_back(issue_to_alert_message(issue.first, issue.second));
                }
            }
        }

        // first, gather sublements into single line list, store in first subelement
        for (auto &pair : message_elements) {
            pair.second.front() = elements_to_translated_list(pair.second, single_line_list_rule);
        }

        // then gather elements to create multiline list
        std::vector<std::string> lines = {};
        for (auto &pair : message_elements) {
            lines.push_back(""); // empty line for readability
            lines.push_back(pair.first);
            lines.push_back(pair.second.front());
        }

        lines.push_back("");
        lines.push_back(_u8L("Consider enabling supports."));
        if (recommend_brim) {
            lines.push_back(_u8L("Also consider enabling brim."));
        }

        // TRN Alert message for detected print issues. first argument is a list of detected issues.
        auto message = elements_to_translated_list(lines, multiline_list_rule);

        if (objects_isssues.size() > 0) {
            this->append_warning_callback(
                Biz::Slicing::Warning{
                    Biz::Slicing::WarningCode::StabilityIssues,
                    {},
                    std::nullopt,
                    Biz::Slicing::StabilityWarningPayload{recommend_brim, message}
                }
            );
        }

        SPDLOG_DEBUG("psAlertWhenSupportsNeeded - end");
        this->set_done(psAlertWhenSupportsNeeded);
    }
}

// Wipe tower support.
bool Print::can_have_wipe_tower() const
{
    return !m_config.get<bool>("spiral_vase")
        && m_config.get<bool>("wipe_tower")
        && m_config.hw_config().material_slot_count() > 1
        && m_extruder_candidates.size() > 1;
}

const std::optional<WipeTowerData>& Print::wipe_tower_data() const
{
    return m_wipe_tower_data;
}

bool is_toolchange_required(
    const bool first_layer,
    const unsigned last_extruder_id,
    const unsigned extruder_id,
    const unsigned current_extruder_id
) {
    if (first_layer && extruder_id == last_extruder_id) {
        return true;
    }
    if (extruder_id != current_extruder_id) {
        return true;
    }
    return false;
}

std::optional<WipeTowerData> Print::generate_wipe_tower_data()
{
    WipeTowerData result;
    if (! this->can_have_wipe_tower())
        return std::nullopt;

    std::vector<std::vector<float>> wipe_volumes = WipeTower::extract_wipe_volumes(m_config);

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    m_tool_ordering = ToolOrdering(*this, (unsigned int)-1, true);

    if (! m_tool_ordering.has_wipe_tower())
        // Don't generate any wipe tower.
        return std::nullopt;

    // Check whether there are any layers in m_tool_ordering, which are marked with has_wipe_tower,
    // they print neither object, nor support. These layers are above the raft and below the object, and they
    // shall be added to the support layers to be printed.
    // see https://github.com/prusa3d/PrusaSlicer/issues/607
    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_tool_ordering.layer_tools().size();
        // Find the first wipe tower layer, which does not have a counterpart in an object or a support layer.
        for (size_t i = 0; i < idx_end; ++ i) {
            const LayerTools &lt = m_tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            // Find the position in m_objects.first()->support_layers to insert these new support layers.
            double wipe_tower_new_layer_print_z_first = m_tool_ordering.layer_tools()[idx_begin].print_z;
            auto it_layer = m_objects.front()->support_layers().begin();
            auto it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                LayerTools &lt = const_cast<LayerTools&>(m_tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - (i == 0 ? 0. : m_tool_ordering.layer_tools()[i-1].print_z);
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, 0, height, lt.print_z, lt.print_z - 0.5 * height);
                ++ it_layer;
            }
        }
    }
    this->throw_if_canceled();

    // Initialize the wipe tower.
    WipeTower wipe_tower(
        this->wipe_tower()->position.cast<float>(),
        this->wipe_tower()->rotation,
        m_config,
        wipe_volumes,
        m_tool_ordering.first_extruder(),
        m_extruder_candidates
    );

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < m_config.hw_config().material_slot_count(); ++ i)
        wipe_tower.set_extruder(i, m_config);

    result.priming = std::make_unique<std::vector<WipeTower::ToolChangeResult>>(
        wipe_tower.prime((float)this->skirt_first_layer_height(), m_tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_tool_ordering.all_extruders().back();
        for (auto &layer_tools : m_tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id, false);
            for (const auto extruder_id : layer_tools.extruders) {
                const bool first_layer{&layer_tools == &m_tool_ordering.front()};
                const unsigned last_extruder_id{m_tool_ordering.all_extruders().back()};
                if (is_toolchange_required(first_layer, last_extruder_id, extruder_id, current_extruder_id)) {
                    float volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];             // total volume to wipe after this toolchange
                    // Not all of that can be used for infill purging:
                    volume_to_wipe -= (float)m_config.get<std::vector<double>>("filament_minimal_purge_on_wipe_tower").at(extruder_id);

                    // try to assign some infills/objects for the wiping:
                    volume_to_wipe = layer_tools.wiping_extrusions_nonconst().mark_wiping_extrusions(*this, layer_tools, current_extruder_id, extruder_id, volume_to_wipe);

                    // add back the minimal amount toforce on the wipe tower:
                    volume_to_wipe += (float)m_config.get<std::vector<double>>("filament_minimal_purge_on_wipe_tower").at(extruder_id);

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height,
                                               current_extruder_id, extruder_id, volume_to_wipe);
                    current_extruder_id = extruder_id;
                }
            }
            layer_tools.wiping_extrusions_nonconst().ensure_perimeters_infills_order(*this, layer_tools);
            if (&layer_tools == &m_tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    // Generate the wipe tower layers.
    result.tool_changes.reserve(m_tool_ordering.layer_tools().size());
    wipe_tower.generate(result.tool_changes);
    result.depth = wipe_tower.get_depth();
    result.z_and_depth_pairs = wipe_tower.get_z_and_depth_pairs();
    result.brim_width = wipe_tower.get_brim_width();
    result.height = wipe_tower.get_wipe_tower_height();

    // Unload the current filament over the purge tower.
    double layer_height = m_objects.front()->config().get<double>("layer_height");
    if (m_tool_ordering.back().wipe_tower_partitions > 0) {
        // The wipe tower goes up to the last layer of the print.
        if (wipe_tower.layer_finished()) {
            // The wipe tower is printed to the top of the print and it has no space left for the final extruder purge.
            // Lift Z to the next layer.
            wipe_tower.set_layer(float(m_tool_ordering.back().print_z + layer_height), float(layer_height));
        } else {
            // There is yet enough space at this layer of the wipe tower for the final purge.
        }
    } else {
        // The wipe tower does not reach the last print layer, perform the pruge at the last print layer.
        assert(m_tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_tool_ordering.back().print_z), float(layer_height));
    }
    result.final_purge = std::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.tool_change((unsigned int)(-1)));

    result.used_filament_until_layer = wipe_tower.get_used_filament_until_layer();
    result.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
    result.width = wipe_tower.width();

    result.first_layer_height = config().get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(layer_height);
    result.cone_x_scale = wipe_tower.get_wipe_tower_cone_x_scale();
    result.cone_radius = wipe_tower.get_wipe_tower_cone_radius();

    return result;
}

PrintRegion *PrintObjectRegions::FuzzySkinPaintedRegion::parent_print_object_region(const LayerRangeRegions &layer_range) const
{
    using FuzzySkinParentType = PrintObjectRegions::FuzzySkinPaintedRegion::ParentType;

    if (this->parent_type == FuzzySkinParentType::PaintedRegion) {
        return layer_range.painted_regions[this->parent].region;
    }

    assert(this->parent_type == FuzzySkinParentType::VolumeRegion);
    return layer_range.volume_regions[this->parent].region;
}

int PrintObjectRegions::FuzzySkinPaintedRegion::parent_print_object_region_id(const LayerRangeRegions &layer_range) const
{
    return this->parent_print_object_region(layer_range)->print_object_region_id();
}

} // namespace Slic3r
