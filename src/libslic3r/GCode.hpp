///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2019 Thomas Moore
///|/ Copyright (c) 2016 Chow Loong Jin @hyperair
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/GCode.pm:
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Robert Giseburt
///|/ Copyright (c) 2012 Mark Hindess
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include "libslic3r/GCode/ExtrusionOrder.hpp"
#include "libslic3r/GCode/ExtrusionProcessor.hpp"
#include "JumpPointSearch.hpp"
#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "PlaceholderParser.hpp"
#include "PrintConfig.hpp"
#include "Geometry/ArcWelder.hpp"
#include "libslic3r/GCode/AvoidCrossingPerimeters.hpp"
#include "libslic3r/GCode/CoolingBuffer.hpp"
#include "libslic3r/GCode/FindReplace.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/LabelObjects.hpp"
#include "libslic3r/GCode/PressureEqualizer.hpp"
#include "libslic3r/GCode/RetractWhenCrossingPerimeters.hpp"
#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/GCode/SpiralVase.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/GCode/Wipe.hpp"
#include "libslic3r/GCode/WipeTowerIntegration.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/Travels.hpp"
#include "EdgeGrid.hpp"
#include "tcbspan/span.hpp"

#include <memory>
#include <map>
#include <string>

//#include "GCode/PressureEqualizer.hpp"

namespace Slic3r {

// Forward declarations.
class GCodeGenerator;
struct WipeTowerData;

namespace { struct Item; }
struct PrintInstance;

class OozePrevention {
public:
    bool enable;
    
    OozePrevention() : enable(false) {}
    std::string pre_toolchange(GCodeGenerator &gcodegen);
    std::string post_toolchange(GCodeGenerator &gcodegen);
    
private:
    int _get_temp(const GCodeGenerator &gcodegen) const;
};

class ColorPrintColors
{
    static const std::vector<std::string> Colors;
public:
    static const std::vector<std::string>& get() { return Colors; }
};

struct LayerResult {
    std::string gcode;
    size_t      layer_id;
    // Is spiral vase post processing enabled for this layer?
    bool        spiral_vase_enable { false };
    // Should the cooling buffer content be flushed at the end of this layer?
    bool        cooling_buffer_flush { false };
    // Is indicating if this LayerResult should be processed, or it is just inserted artificial LayerResult.
    // It is used for the pressure equalizer because it needs to buffer one layer back.
    bool        nop_layer_result { false };

    static LayerResult make_nop_layer_result() { return {"", std::numeric_limits<coord_t>::max(), false, false, true}; }
};

namespace GCode {
struct PrintObjectInstance
{
    const PrintObject *print_object = nullptr;
    int                instance_idx = -1;

    bool operator==(const PrintObjectInstance &other) const {return print_object == other.print_object && instance_idx == other.instance_idx; }
    bool operator!=(const PrintObjectInstance &other) const { return !(*this == other); }
};

} // namespace GCode

class GCodeGenerator {

public:
    GCodeGenerator(const Print* print = nullptr); // The default value is only used in unit tests.
    ~GCodeGenerator() = default;

    // throws std::runtime_exception on error,
    // throws CanceledException through print->throw_if_canceled().
    void            do_export(Print* print, const char* path, GCodeProcessorResult* result = nullptr, ThumbnailsGeneratorCallback thumbnail_cb = nullptr);

    // Exported for the helper classes (OozePrevention, Wipe) and for the Perl binding for unit tests.
    const Vec2d&    origin() const { return m_origin; }
    void            set_origin(const Vec2d &pointf);
    void            set_origin(const coordf_t x, const coordf_t y) { this->set_origin(Vec2d(x, y)); }
    // Convert coordinates of the active object to G-code coordinates, possibly adjusted for extruder offset.
    template<typename Derived>
    Eigen::Matrix<double, Derived::SizeAtCompileTime, 1, Eigen::DontAlign> point_to_gcode(const Eigen::MatrixBase<Derived> &point) const {
        static_assert(
            Derived::IsVectorAtCompileTime,
            "GCodeGenerator::point_to_gcode(): first parameter is not a vector"
        );
        static_assert(
            int(Derived::SizeAtCompileTime) == 2 || int(Derived::SizeAtCompileTime) == 3,
            "GCodeGenerator::point_to_gcode(): first parameter is not a 2D or 3D vector"
        );

        if constexpr (Derived::SizeAtCompileTime == 2) {
            return Vec2d(unscaled<double>(point.x()), unscaled<double>(point.y())) + m_origin
                - m_config.extruder_offset.get_at(m_writer.extruder()->id());
        } else {
            const Vec2d gcode_point_xy{this->point_to_gcode(point.template head<2>())};
            return to_3d(gcode_point_xy, unscaled(point.z()));
        }
    }

    // Convert coordinates of the active object to G-code coordinates, possibly adjusted for extruder offset and quantized to G-code resolution.
    template<typename Derived>
    Vec2d           point_to_gcode_quantized(const Eigen::MatrixBase<Derived> &point) const {
        static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "GCodeGenerator::point_to_gcode_quantized(): first parameter is not a 2D vector");
        Vec2d p = this->point_to_gcode(point);
        return { GCodeFormatter::quantize_xyzf(p.x()), GCodeFormatter::quantize_xyzf(p.y()) };
    }
    Point           gcode_to_point(const Vec2d &point) const;
    const FullPrintConfig &config() const { return m_config; }
    const Layer*    layer() const { return m_layer; }
    GCodeWriter&    writer() { return m_writer; }
    const GCodeWriter& writer() const { return m_writer; }
    PlaceholderParser& placeholder_parser() { return m_placeholder_parser_integration.parser; }
    const PlaceholderParser& placeholder_parser() const { return m_placeholder_parser_integration.parser; }
    // Process a template through the placeholder parser, collect error messages to be reported
    // inside the generated string and after the G-code export finishes.
    std::string     placeholder_parser_process(const std::string &name, const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override = nullptr);
    bool            enable_cooling_markers() const { return m_enable_cooling_markers; }

    void            set_layer_count(unsigned int value) { m_layer_count = value; }
    void            apply_print_config(const PrintConfig &print_config);

    // append full config to the given string
    static void append_full_config(const Print& print, std::string& str);
    // translate full config into a list of <key, value> items
    static void encode_full_config(const Print& print, std::vector<std::pair<std::string, std::string>>& config);

    using ObjectLayerToPrint  = GCode::ObjectLayerToPrint;
    using ObjectsLayerToPrint = GCode::ObjectsLayerToPrint;

    std::optional<Point> last_position;

private:
    using InstanceToPrint = GCode::InstanceToPrint;
    using InfillRange = GCode::ExtrusionOrder::InfillRange;
    using SliceExtrusions = GCode::ExtrusionOrder::SliceExtrusions;
    using IslandExtrusions = GCode::ExtrusionOrder::IslandExtrusions;

    class GCodeOutputStream {
    public:
        GCodeOutputStream(FILE *f, GCodeProcessor &processor) : f(f), m_processor(processor) {}
        ~GCodeOutputStream() { this->close(); }

        // Set a find-replace post-processor to modify the G-code before GCodePostProcessor.
        // It is being set to null inside process_layers(), because the find-replace process
        // is being called on a secondary thread to improve performance.
        void set_find_replace(GCodeFindReplace *find_replace, bool enabled) { m_find_replace_backup = find_replace; m_find_replace = enabled ? find_replace : nullptr; }
        void find_replace_enable() { m_find_replace = m_find_replace_backup; }
        void find_replace_supress() { m_find_replace = nullptr; }

        bool is_open() const { return f; }
        bool is_error() const;

        void flush();
        void close();

        // Write a string into a file.
        void write(const std::string& what) { this->write(what.c_str()); }
        void write(const char* what);

        // Write a string into a file. 
        // Add a newline, if the string does not end with a newline already.
        // Used to export a custom G-code section processed by the PlaceholderParser.
        void writeln(const std::string& what);

        // Formats and write into a file the given data. 
        void write_format(const char* format, ...);

    private:
        FILE             *f { nullptr };
        // Find-replace post-processor to be called before GCodePostProcessor.
        GCodeFindReplace *m_find_replace { nullptr };
        // If suppressed, the backoup holds m_find_replace.
        GCodeFindReplace *m_find_replace_backup { nullptr };
        GCodeProcessor   &m_processor;
    };
    void            _do_export(Print &print, GCodeOutputStream &file, ThumbnailsGeneratorCallback thumbnail_cb);

    static ObjectsLayerToPrint         		                     collect_layers_to_print(const PrintObject &object);
    static std::vector<std::pair<coordf_t, ObjectsLayerToPrint>> collect_layers_to_print(const Print &print);

    Polyline get_layer_change_xy_path(const Vec3d &from, const Vec3d &to);

    std::string get_ramping_layer_change_gcode(const Vec3d &from, const Vec3d &to, const unsigned extruder_id);

    /** @brief Generates ramping travel gcode for layer change. */
    std::string generate_ramping_layer_change_gcode(
        const Polyline &xy_path,
        const double initial_elevation,
        const GCode::Impl::Travels::ElevatedTravelParams &elevation_params
    ) const;

    std::vector<GCode::ExtrusionOrder::ExtruderExtrusions> get_sorted_extrusions(
        const Print &print,
        const ObjectsLayerToPrint &layers,
        const LayerTools &layer_tools,
        const std::vector<InstanceToPrint> &instances_to_print,
        const GCode::SmoothPathCaches &smooth_path_caches,
        const bool first_layer
    );

    LayerResult process_layer(
        const Print                     &print,
        // Set of object & print layers of the same PrintObject and with the same print_z.
        const ObjectsLayerToPrint       &layers,
        const LayerTools  				&layer_tools,
        const GCode::SmoothPathCaches   &smooth_path_caches,
        const bool                       last_layer,
		// Pairs of PrintObject index and its instance index.
		const std::vector<const PrintInstance*> *ordering,
        // If set to size_t(-1), then print all copies of all objects.
        // Otherwise print a single copy of a single object.
        const size_t                     single_object_idx = size_t(-1));

    // Process all layers of all objects (non-sequential mode) with a parallel pipeline:
    // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
    // and export G-code into file.
    void process_layers(
        const Print                                                   &print,
        const ToolOrdering                                            &tool_ordering,
        const std::vector<const PrintInstance*>                       &print_object_instances_ordering,
        const std::vector<std::pair<coordf_t, ObjectsLayerToPrint>>   &layers_to_print,
        const GCode::SmoothPathCache                                  &smooth_path_cache_global,
        GCodeOutputStream                                             &output_stream);
    // Process all layers of a single object instance (sequential mode) with a parallel pipeline:
    // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
    // and export G-code into file.
    void process_layers(
        const Print                             &print,
        const ToolOrdering                      &tool_ordering,
        ObjectsLayerToPrint                      layers_to_print,
        const size_t                             single_object_idx,
        const GCode::SmoothPathCache            &smooth_path_cache_global,
        GCodeOutputStream                       &output_stream);

    void            set_extruders(const std::vector<unsigned int> &extruder_ids);
    std::string     preamble();
    std::string change_layer(
        coordf_t previous_layer_z,
        coordf_t print_z,
        bool vase_mode,
        const Point &first_point,
        const bool first_layer
    );
    std::string extrude_smooth_path(
        const GCode::SmoothPath &smooth_path,
        const bool is_loop,
        const std::string_view description,
        const double speed,
        const std::size_t wipe_offset = 0
    );
    std::string extrude_skirt(
        GCode::SmoothPath smooth_path, const ExtrusionFlow &extrusion_flow_override
    );

    std::vector<InstanceToPrint> sort_print_object_instances(
        // Object and Support layers for the current print_z, collected for a single object, or for possibly multiple objects with multiple instances.
        const std::vector<ObjectLayerToPrint>           &layers,
        // Ordering must be defined for normal (non-sequential print).
        const std::vector<const PrintInstance*>         *ordering,
        // For sequential print, the instance of the object to be printing has to be defined.
        const size_t                                     single_object_instance_idx);

    std::string extrude_perimeters(
        const PrintRegion &region,
        const std::vector<GCode::ExtrusionOrder::Perimeter> &perimeters,
        const InstanceToPrint &print_instance
    );

    std::string extrude_infill_ranges(
        const std::vector<InfillRange> &infill_ranges,
        const std::string &commment
    );

    void initialize_instance(
        const InstanceToPrint &print_instance,
        const ObjectLayerToPrint &layer_to_print,
        const bool is_first
    );

    std::string extrude_slices(
        const InstanceToPrint &print_instance,
        const ObjectLayerToPrint &layer_to_print,
        const std::vector<SliceExtrusions> &slices_extrusions
    );

    std::string extrude_support(
        const std::vector<GCode::ExtrusionOrder::SupportPath> &support_extrusions
    );

    std::string generate_travel_gcode(
        const Points3& travel,
        const std::string& comment,
        const std::function<std::string()>& insert_gcode
    );
    Polyline generate_travel_xy_path(
        const Point& start,
        const Point& end,
        const bool needs_retraction,
        bool& could_be_wipe_disabled
    );
    std::string travel_to(
        const Vec3crd &start_point,
        const Vec3crd &end_point,
        ExtrusionRole role,
        const std::string &comment,
        const std::function<std::string()>& insert_gcode
    );

    std::string travel_to_first_position(const Vec3crd& point, const double from_z, const ExtrusionRole role, const std::function<std::string()>& insert_gcode);

    bool            needs_retraction(const Polyline &travel, ExtrusionRole role = ExtrusionRole::None);

    std::string     retract_and_wipe(bool toolchange = false, bool reset_e = true);
    std::string     unretract() { return m_writer.unretract(); }
    std::string     set_extruder(unsigned int extruder_id, double print_z);
    bool line_distancer_is_required(const std::vector<unsigned int>& extruder_ids);

    Seams::Placer                       m_seam_placer;

    /* Origin of print coordinates expressed in unscaled G-code coordinates.
       This affects the input arguments supplied to the extrude*() and travel_to()
       methods. */
    Vec2d                               m_origin;
    FullPrintConfig                     m_config;
    // scaled G-code resolution
    double                              m_scaled_resolution;
    GCodeWriter                         m_writer;

    struct PlaceholderParserIntegration {
        void reset();
        void init(const GCodeWriter &config);
        void update_from_gcodewriter(const GCodeWriter &writer, const WipeTowerData& wipe_tower_data);
        void validate_output_vector_variables();

        PlaceholderParser                   parser;
        // For random number generator etc.
        PlaceholderParser::ContextData      context;
        // Collection of templates, on which the placeholder substitution failed.
        std::map<std::string, std::string>  failed_templates;
        // Input/output from/to custom G-code block, for returning position, retraction etc.
        DynamicConfig                       output_config;
        ConfigOptionFloats                 *opt_position { nullptr };
        ConfigOptionFloats                 *opt_e_position { nullptr };
        ConfigOptionFloat                  *opt_zhop { nullptr };
        ConfigOptionFloats                 *opt_e_retracted { nullptr };
        ConfigOptionFloats                 *opt_e_restart_extra { nullptr };
        ConfigOptionFloats                 *opt_extruded_volume { nullptr };
        ConfigOptionFloats                 *opt_extruded_weight { nullptr };
        ConfigOptionFloat                  *opt_extruded_volume_total { nullptr };
        ConfigOptionFloat                  *opt_extruded_weight_total { nullptr };
        // Caches of the data passed to the script.
        size_t                              num_extruders;
        std::vector<double>                 position;
        std::vector<double>                 e_position;
        std::vector<double>                 e_retracted;
        std::vector<double>                 e_restart_extra;
    } m_placeholder_parser_integration;

    OozePrevention                      m_ooze_prevention;
    GCode::Wipe                         m_wipe;
    GCode::LabelObjects                 m_label_objects;
    AvoidCrossingPerimeters             m_avoid_crossing_perimeters;
    JPSPathFinder                       m_avoid_crossing_curled_overhangs;
    RetractWhenCrossingPerimeters       m_retract_when_crossing_perimeters;
    GCode::TravelObstacleTracker        m_travel_obstacle_tracker;
    bool                                m_enable_loop_clipping;
    // If enabled, the G-code generator will put following comments at the ends
    // of the G-code lines: _EXTRUDE_SET_SPEED, _WIPE, _BRIDGE_FAN_START, _BRIDGE_FAN_END
    // Those comments are received and consumed (removed from the G-code) by the CoolingBuffer.pm Perl module.
    bool                                m_enable_cooling_markers;
    // Markers for the Pressure Equalizer to recognize the extrusion type.
    // The Pressure Equalizer removes the markers from the final G-code.
    bool                                m_enable_extrusion_role_markers;
    // Keeps track of the last extrusion role passed to the processor
    GCodeExtrusionRole                  m_last_processor_extrusion_role;
    // How many times will change_layer() be called?
    // change_layer() will update the progress bar.
    unsigned int                        m_layer_count;
    // Progress bar indicator. Increments from -1 up to layer_count.
    int                                 m_layer_index;
    // Current layer processed. In sequential printing mode, only a single copy will be printed.
    // In non-sequential mode, all its copies will be printed.
    const Layer*                        m_layer;
    // m_layer is an object layer and it is being printed over raft surface.
    bool                                m_object_layer_over_raft;
    double                              m_volumetric_speed;
    // Support for the extrusion role markers. Which marker is active?
    GCodeExtrusionRole                  m_last_extrusion_role;
    // Support for G-Code Processor
    float                               m_last_height{ 0.0f };
    float                               m_last_layer_z{ 0.0f };
    float                               m_max_layer_z{ 0.0f };
    float                               m_last_width{ 0.0f };

    std::optional<Vec3d>                m_previous_layer_last_position;
    std::optional<Vec3d>                m_previous_layer_last_position_before_wipe;
    bool                                m_moved_to_first_layer_point{false};

    // This needs to be populated during the layer processing!
    std::unique_ptr<CoolingBuffer>      m_cooling_buffer;
    std::unique_ptr<SpiralVase>         m_spiral_vase;
    std::unique_ptr<GCodeFindReplace>   m_find_replace;
    std::unique_ptr<PressureEqualizer>  m_pressure_equalizer;
    std::unique_ptr<GCode::WipeTowerIntegration> m_wipe_tower;

    // Current fan speed set by dynamic fan speed control.
    std::optional<float>                m_current_dynamic_fan_speed;

    // Heights (print_z) at which the skirt has already been extruded.
    std::vector<coordf_t>               m_skirt_done;
    // Has the brim been extruded already? Brim is being extruded only for the first object of a multi-object print.
    bool                                m_brim_done;
    // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
    bool                                m_second_layer_things_done;
    // G-code that is due to be written before the next extrusion
    std::string                         m_pending_pre_extrusion_gcode;
    // Pointer to currently exporting PrintObject and instance index.
    GCode::PrintObjectInstance          m_current_instance;

    bool                                m_silent_time_estimator_enabled;

    // Processor
    GCodeProcessor                      m_processor;

    // Back-pointer to Print (const).
    const Print*                        m_print;

    struct EmitModifiers {
        EmitModifiers(bool emit_fan_speed_reset, bool emit_bridge_fan_start, bool emit_bridge_fan_end)
            : emit_fan_speed_reset(emit_fan_speed_reset), emit_bridge_fan_start(emit_bridge_fan_start), emit_bridge_fan_end(emit_bridge_fan_end) {}

        EmitModifiers() : EmitModifiers(true, true, true) {};

        static EmitModifiers create_with_disabled_emits() {
            return {false, false, false};
        }

        bool emit_fan_speed_reset  = true;

        bool emit_bridge_fan_start = true;
        bool emit_bridge_fan_end   = true;
    };

    std::string                         _extrude(const ExtrusionAttributes &attribs, const Geometry::ArcWelder::Path &path, std::string_view description, double speed, const EmitModifiers &emit_modifiers = EmitModifiers());

    void                                print_machine_envelope(GCodeOutputStream &file, const Print &print);
    void                                _print_first_layer_chamber_temperature(GCodeOutputStream &file, const Print &print, const std::string &gcode, int temp, bool wait, bool accurate);
    void                                _print_first_layer_bed_temperature(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait);
    void                                _print_first_layer_extruder_temperatures(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait);
    // On the first printing layer. This flag triggers first layer speeds.
    bool                                on_first_layer() const { return m_layer != nullptr && m_layer->id() == 0; }
    // To control print speed of 1st object layer over raft interface.
    bool                                object_layer_over_raft() const { return m_object_layer_over_raft; }

    // Fill in cache of smooth paths for perimeters, fills and supports of the given object layers.
    // Based on params, the paths are either decimated to sparser polylines, or interpolated with circular arches.
    static void                         smooth_path_interpolate(const ObjectLayerToPrint &layers, const GCode::SmoothPathCache::InterpolationParameters &params, GCode::SmoothPathCache &out);

    friend class GCode::Wipe;
    friend class GCode::WipeTowerIntegration;
    friend class PressureEqualizer;
};

std::vector<const PrintInstance*> sort_object_instances_by_model_order(const Print& print);

}

#endif
