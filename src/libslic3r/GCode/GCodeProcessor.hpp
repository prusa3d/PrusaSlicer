///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <LibBGCode/binarize/binarize.hpp>

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace Slic3r {

    class Print;

    enum class EMoveType : unsigned char
    {
        Noop,
        Retract,
        Unretract,
        Seam,
        Tool_change,
        Color_change,
        Pause_Print,
        Custom_GCode,
        Travel,
        Wipe,
        Extrude,
        Count
    };

    struct PrintEstimatedStatistics
    {
        enum class ETimeMode : unsigned char
        {
            Normal,
            Stealth,
            Count
        };

        struct Mode
        {
            float time;
            std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> custom_gcode_times;

            void reset() {
                time = 0.0f;
                custom_gcode_times.clear();
                custom_gcode_times.shrink_to_fit();
            }
        };

        std::vector<double>                                     volumes_per_color_change;
        std::map<size_t, double>                                volumes_per_extruder;
        std::map<GCodeExtrusionRole, std::pair<double, double>> used_filaments_per_role;
        std::map<size_t, double>                                cost_per_extruder;

        std::array<Mode, static_cast<size_t>(ETimeMode::Count)> modes;

        PrintEstimatedStatistics() { reset(); }

        void reset() {
            for (Mode &m : modes) {
                m.reset();
            }
            volumes_per_color_change.clear();
            volumes_per_color_change.shrink_to_fit();
            volumes_per_extruder.clear();
            used_filaments_per_role.clear();
            cost_per_extruder.clear();
        }
    };

    struct ConflictResult
    {
        std::string _objName1;
        std::string _objName2;
        double      _height;
        const void* _obj1; // nullptr means wipe tower
        const void* _obj2;
        int         layer = -1;
        ConflictResult(const std::string& objName1, const std::string& objName2, double height, const void* obj1, const void* obj2)
          : _objName1(objName1), _objName2(objName2), _height(height), _obj1(obj1), _obj2(obj2)
        {}
        ConflictResult() = default;
    };

    using ConflictResultOpt = std::optional<ConflictResult>;

    struct GCodeProcessorResult
    {
        struct SettingsIds
        {
            std::string print;
            std::vector<std::string> filament;
            std::string printer;

            void reset() {
                print.clear();
                filament.clear();
                printer.clear();
            }
        };

        struct MoveVertex
        {
            unsigned int gcode_id{ 0 };
            EMoveType type{ EMoveType::Noop };
            GCodeExtrusionRole extrusion_role{ GCodeExtrusionRole::None };
            unsigned char extruder_id{ 0 };
            unsigned char cp_color_id{ 0 };
            Vec3f position{ Vec3f::Zero() }; // mm
            float delta_extruder{ 0.0f }; // mm
            float feedrate{ 0.0f }; // mm/s
            float actual_feedrate{ 0.0f }; // mm/s
            float width{ 0.0f }; // mm
            float height{ 0.0f }; // mm
            float mm3_per_mm{ 0.0f };
            float fan_speed{ 0.0f }; // percentage
            float temperature{ 0.0f }; // Celsius degrees
            std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> time{ 0.0f, 0.0f }; // s
            unsigned int layer_id{ 0 };
            bool internal_only{ false };

            float volumetric_rate() const { return feedrate * mm3_per_mm; }
            float actual_volumetric_rate() const { return actual_feedrate * mm3_per_mm; }
        };

        std::string filename;
        bool is_binary_file;
        unsigned int id;
        std::vector<MoveVertex> moves;
        // Positions of ends of lines of the final G-code this->filename after TimeProcessor::post_process() finalizes the G-code.
        // Binarized gcodes usually have several gcode blocks. Each block has its own list on ends of lines.
        // Ascii gcodes have only one list on ends of lines
        std::vector<std::vector<size_t>> lines_ends;
        Pointfs bed_shape;
        float max_print_height;
        float z_offset;
        SettingsIds settings_ids;
        size_t extruders_count;
        bool backtrace_enabled;
        std::vector<std::string> extruder_colors;
        std::vector<float> filament_diameters;
        std::vector<float> filament_densities;
        std::vector<float> filament_cost;

        PrintEstimatedStatistics print_statistics;
        std::vector<CustomGCode::Item> custom_gcode_per_print_z;
        bool spiral_vase_mode;

        ConflictResultOpt conflict_result;
        std::optional<std::pair<std::string, std::string>> sequential_collision_detected;

        void reset();
    };


    class GCodeProcessor
    {
        static const std::vector<std::string> Reserved_Tags;

    public:
        enum class ETags : unsigned char
        {
            Role,
            Wipe_Start,
            Wipe_End,
            Height,
            Width,
            Layer_Change,
            Color_Change,
            Pause_Print,
            Custom_Code,
            First_Line_M73_Placeholder,
            Last_Line_M73_Placeholder,
            Estimated_Printing_Time_Placeholder
        };

        static const std::string& reserved_tag(ETags tag) { return Reserved_Tags[static_cast<unsigned char>(tag)]; }
        // checks the given gcode for reserved tags and returns true when finding the 1st (which is returned into found_tag) 
        static bool contains_reserved_tag(const std::string& gcode, std::string& found_tag);
        // checks the given gcode for reserved tags and returns true when finding any
        // (the first max_count found tags are returned into found_tag)
        static bool contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag);

        static const float Wipe_Width;
        static const float Wipe_Height;

    private:
        using AxisCoords = std::array<double, 4>;
        using ExtruderColors = std::vector<unsigned char>;
        using ExtruderTemps = std::vector<float>;

        enum class EUnits : unsigned char
        {
            Millimeters,
            Inches
        };

        enum class EPositioningType : unsigned char
        {
            Absolute,
            Relative
        };

        struct CachedPosition
        {
            AxisCoords position; // mm
            float feedrate; // mm/s

            void reset();
        };

        struct CpColor
        {
            unsigned char counter;
            unsigned char current;

            void reset();
        };

    public:
        struct FeedrateProfile
        {
            float entry{ 0.0f }; // mm/s
            float cruise{ 0.0f }; // mm/s
            float exit{ 0.0f }; // mm/s
        };

        struct Trapezoid
        {
            float accelerate_until{ 0.0f }; // mm
            float decelerate_after{ 0.0f }; // mm
            float cruise_feedrate{ 0.0f }; // mm/sec

            float acceleration_time(float entry_feedrate, float acceleration) const;
            float cruise_time() const { return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f; }
            float deceleration_time(float distance, float acceleration) const;
            float acceleration_distance() const { return accelerate_until; }
            float cruise_distance() const { return decelerate_after - accelerate_until; }
            float deceleration_distance(float distance) const { return distance - decelerate_after; }
            bool is_cruise_only(float distance) const { return std::abs(cruise_distance() - distance) < EPSILON; }
        };

        struct TimeBlock
        {
            struct Flags
            {
                bool recalculate{ false };
                bool nominal_length{ false };
            };

            EMoveType move_type{ EMoveType::Noop };
            GCodeExtrusionRole role{ GCodeExtrusionRole::None };
            unsigned int move_id{ 0 };
            unsigned int g1_line_id{ 0 };
            unsigned int remaining_internal_g1_lines;
            unsigned int layer_id{ 0 };
            float distance{ 0.0f }; // mm
            float acceleration{ 0.0f }; // mm/s^2
            float max_entry_speed{ 0.0f }; // mm/s
            float safe_feedrate{ 0.0f }; // mm/s
            Flags flags;
            FeedrateProfile feedrate_profile;
            Trapezoid trapezoid;

            // Calculates this block's trapezoid
            void calculate_trapezoid();

            float time() const {
                return trapezoid.acceleration_time(feedrate_profile.entry, acceleration) +
                       trapezoid.cruise_time() + trapezoid.deceleration_time(distance, acceleration);
            }
        };

    private:
        struct TimeMachine
        {
            struct State
            {
                float feedrate; // mm/s
                float safe_feedrate; // mm/s
                AxisCoords axis_feedrate; // mm/s
                AxisCoords abs_axis_feedrate; // mm/s

                void reset();
            };

            struct CustomGCodeTime
            {
                bool needed;
                float cache;
                std::vector<std::pair<CustomGCode::Type, float>> times;

                void reset();
            };

            struct G1LinesCacheItem
            {
                unsigned int id;
                unsigned int remaining_internal_g1_lines;
                float elapsed_time;
            };

            struct ActualSpeedMove
            {
                unsigned int move_id{ 0 };
                std::optional<Vec3f> position;
                float actual_feedrate{ 0.0f };
                std::optional<float> delta_extruder;
                std::optional<float> feedrate;
                std::optional<float> width;
                std::optional<float> height;
                std::optional<float> mm3_per_mm;
                std::optional<float> fan_speed;
                std::optional<float> temperature;
            };

            bool enabled;
            float acceleration; // mm/s^2
            // hard limit for the acceleration, to which the firmware will clamp.
            float max_acceleration; // mm/s^2
            float retract_acceleration; // mm/s^2
            // hard limit for the acceleration, to which the firmware will clamp.
            float max_retract_acceleration; // mm/s^2
            float travel_acceleration; // mm/s^2
            // hard limit for the travel acceleration, to which the firmware will clamp.
            float max_travel_acceleration; // mm/s^2
            float extrude_factor_override_percentage;
            // We accumulate total print time in doubles to reduce the loss of precision
            // while adding big floating numbers with small float numbers.
            double time; // s
            struct StopTime
            {
                unsigned int g1_line_id;
                float elapsed_time;
            };
            std::vector<StopTime> stop_times;
            std::string line_m73_main_mask;
            std::string line_m73_stop_mask;
            State curr;
            State prev;
            CustomGCodeTime gcode_time;
            std::vector<TimeBlock> blocks;
            std::vector<G1LinesCacheItem> g1_times_cache;
            float first_layer_time;
            std::vector<ActualSpeedMove> actual_speed_moves;

            void reset();

            void calculate_time(GCodeProcessorResult& result, PrintEstimatedStatistics::ETimeMode mode, size_t keep_last_n_blocks = 0, float additional_time = 0.0f);
        };

        struct TimeProcessor
        {
            struct Planner
            {
                // Size of the firmware planner queue. The old 8-bit Marlins usually just managed 16 trapezoidal blocks.
                // Let's be conservative and plan for newer boards with more memory.
                static constexpr size_t queue_size = 64;
                // The firmware recalculates last planner_queue_size trapezoidal blocks each time a new block is added.
                // We are not simulating the firmware exactly, we calculate a sequence of blocks once a reasonable number of blocks accumulate.
                static constexpr size_t refresh_threshold = queue_size * 4;
            };

            // extruder_id is currently used to correctly calculate filament load / unload times into the total print time.
            // This is currently only really used by the MK3 MMU2:
            // extruder_unloaded = true means no filament is loaded yet, all the filaments are parked in the MK3 MMU2 unit.
            bool extruder_unloaded;
            // whether or not to export post-process the gcode to export lines M73 in it
            bool export_remaining_time_enabled;
            // allow to skip the lines M201/M203/M204/M205 generated by GCode::print_machine_envelope() for non-Normal time estimate mode
            bool machine_envelope_processing_enabled;
            MachineEnvelopeConfig machine_limits;
            // Additional load / unload times for a filament exchange sequence.
            std::vector<float> filament_load_times;
            std::vector<float> filament_unload_times;
            std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> machines;

            void reset();

            friend class GCodeProcessor;
        };

        struct UsedFilaments  // filaments per ColorChange
        {
            double color_change_cache;
            std::vector<double> volumes_per_color_change;

            double tool_change_cache;
            std::map<size_t, double> volumes_per_extruder;

            double role_cache;
            std::map<GCodeExtrusionRole, std::pair<double, double>> filaments_per_role; // ExtrusionRole -> (m, g)

            void reset();

            void increase_caches(double extruded_volume, unsigned char extruder_id, double parking_volume, double extra_loading_volume);

            void process_color_change_cache();
            void process_extruder_cache(unsigned char extruder_id);
            void process_role_cache(const GCodeProcessor* processor);
            void process_caches(const GCodeProcessor* processor);
       private:
            std::vector<double> extruder_retracted_volume;
            bool recent_toolchange = false;
        };

    public:
        class SeamsDetector
        {
            bool m_active{ false };
            std::optional<Vec3f> m_first_vertex;

        public:
            void activate(bool active) {
                if (m_active != active) {
                    m_active = active;
                    if (m_active)
                        m_first_vertex.reset();
                }
            }

            std::optional<Vec3f> get_first_vertex() const { return m_first_vertex; }
            void set_first_vertex(const Vec3f& vertex) { m_first_vertex = vertex; }

            bool is_active() const { return m_active; }
            bool has_first_vertex() const { return m_first_vertex.has_value(); }
        };

        // Helper class used to fix the z for color change, pause print and
        // custom gcode markes
        class OptionsZCorrector
        {
            GCodeProcessorResult& m_result;
            std::optional<size_t> m_move_id;
            std::optional<size_t> m_custom_gcode_per_print_z_id;

        public:
            explicit OptionsZCorrector(GCodeProcessorResult& result) : m_result(result) {
            }

            void set() {
                m_move_id = m_result.moves.size() - 1;
                m_custom_gcode_per_print_z_id = m_result.custom_gcode_per_print_z.size() - 1;
            }

            void update(float height) {
                if (!m_move_id.has_value() || !m_custom_gcode_per_print_z_id.has_value())
                    return;

                const Vec3f position = m_result.moves.back().position;

                GCodeProcessorResult::MoveVertex& move = m_result.moves.emplace_back(m_result.moves[*m_move_id]);
                move.position = position;
                move.height = height;
                m_result.moves.erase(m_result.moves.begin() + *m_move_id);
                m_result.custom_gcode_per_print_z[*m_custom_gcode_per_print_z_id].print_z = position.z();
                reset();
            }

            void reset() {
                m_move_id.reset();
                m_custom_gcode_per_print_z_id.reset();
            }
        };

        static bgcode::binarize::BinarizerConfig& get_binarizer_config() { return s_binarizer_config; }

    private:
        GCodeReader m_parser;
        bgcode::binarize::Binarizer m_binarizer;
        static bgcode::binarize::BinarizerConfig s_binarizer_config;

        EUnits m_units;
        EPositioningType m_global_positioning_type;
        EPositioningType m_e_local_positioning_type;
        std::vector<Vec3f> m_extruder_offsets;
        GCodeFlavor m_flavor;

        AxisCoords m_start_position; // mm
        AxisCoords m_end_position; // mm
        AxisCoords m_saved_position; // mm
        AxisCoords m_origin; // mm
        CachedPosition m_cached_position;
        bool m_wiping;

        unsigned int m_line_id;
        unsigned int m_last_line_id;
        float m_feedrate; // mm/s
        struct FeedMultiply
        {
            float current; // percentage
            float saved;   // percentage

            void reset() {
                current = 1.0f;
                saved = 1.0f;
            }
        };
        FeedMultiply m_feed_multiply;
        float m_width; // mm
        float m_height; // mm
        float m_forced_width; // mm
        float m_forced_height; // mm
        float m_mm3_per_mm;
        float m_fan_speed; // percentage
        float m_z_offset; // mm
        GCodeExtrusionRole m_extrusion_role;
        unsigned char m_extruder_id;
        ExtruderColors m_extruder_colors;
        ExtruderTemps m_extruder_temps;
        ExtruderTemps m_extruder_temps_config;
        ExtruderTemps m_extruder_temps_first_layer_config;
        bool  m_is_XL_printer = false;
        float m_parking_position;
        float m_extra_loading_move;
        float m_extruded_last_z;
        float m_first_layer_height; // mm
        unsigned int m_g1_line_id;
        unsigned int m_layer_id;
        CpColor m_cp_color;
        bool m_use_volumetric_e;
        SeamsDetector m_seams_detector;
        OptionsZCorrector m_options_z_corrector;
        size_t m_last_default_color_id;
        float m_kissslicer_toolchange_time_correction;
        bool m_single_extruder_multi_material;

        enum class EProducer
        {
            Unknown,
            PrusaSlicer,
            Slic3rPE,
            Slic3r,
            SuperSlicer,
            Cura,
            Simplify3D,
            CraftWare,
            ideaMaker,
            KissSlicer,
            BambuStudio
        };

        static const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> Producers;
        EProducer m_producer;

        TimeProcessor m_time_processor;
        UsedFilaments m_used_filaments;

        Print* m_print{ nullptr };

        GCodeProcessorResult m_result;
        static unsigned int s_result_id;

    public:
        GCodeProcessor();

        void apply_config(const PrintConfig& config);
        void set_print(Print* print) { m_print = print; }
        bgcode::binarize::BinaryData& get_binary_data() { return m_binarizer.get_binary_data(); }
        const bgcode::binarize::BinaryData& get_binary_data() const { return m_binarizer.get_binary_data(); }

        void enable_stealth_time_estimator(bool enabled);
        bool is_stealth_time_estimator_enabled() const {
            return m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled;
        }
        void enable_machine_envelope_processing(bool enabled) { m_time_processor.machine_envelope_processing_enabled = enabled; }
        void reset();

        const GCodeProcessorResult& get_result() const { return m_result; }
        GCodeProcessorResult&& extract_result() { return std::move(m_result); }

        // Load a G-code into a stand-alone G-code viewer.
        // throws CanceledException through print->throw_if_canceled() (sent by the caller as callback).
        void process_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback = nullptr,
            std::function<void(void)> cancel_callback = nullptr);

        // Streaming interface, for processing G-codes just generated by PrusaSlicer in a pipelined fashion.
        void initialize(const std::string& filename);
        void initialize_result_moves() {
            // 1st move must be a dummy move
            assert(m_result.moves.empty());
            m_result.moves.emplace_back(GCodeProcessorResult::MoveVertex());
        }
        void process_buffer(const std::string& buffer);
        void finalize(bool post_process);

        float get_time(PrintEstimatedStatistics::ETimeMode mode) const;
        std::string get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const;
        std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const;

        float get_first_layer_time(PrintEstimatedStatistics::ETimeMode mode) const;

    private:
        void apply_config(const DynamicPrintConfig& config);
        void apply_config_simplify3d(const std::string& filename);
        void apply_config_superslicer(const std::string& filename);
        void apply_config_kissslicer(const std::string& filename);
        void process_gcode_line(const GCodeReader::GCodeLine& line, bool producers_enabled);

        void process_ascii_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback = nullptr,
            std::function<void(void)> cancel_callback = nullptr);
        void process_binary_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback = nullptr,
            std::function<void(void)> cancel_callback = nullptr);

        // Process tags embedded into comments
        void process_tags(const std::string_view comment, bool producers_enabled);
        bool process_producers_tags(const std::string_view comment);
        bool process_prusaslicer_tags(const std::string_view comment);
        bool process_cura_tags(const std::string_view comment);
        bool process_simplify3d_tags(const std::string_view comment);
        bool process_craftware_tags(const std::string_view comment);
        bool process_ideamaker_tags(const std::string_view comment);
        bool process_kissslicer_tags(const std::string_view comment);
        bool process_bambustudio_tags(const std::string_view comment);

        bool detect_producer(const std::string_view comment);

        // Move
        void process_G0(const GCodeReader::GCodeLine& line);
        void process_G1(const GCodeReader::GCodeLine& line);
        enum class G1DiscretizationOrigin {
            G1,
            G2G3,
        };
        void process_G1(const std::array<std::optional<double>, 4>& axes = { std::nullopt, std::nullopt, std::nullopt, std::nullopt },
            const std::optional<double>& feedrate = std::nullopt, G1DiscretizationOrigin origin = G1DiscretizationOrigin::G1,
            const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt);

        // Arc Move
        void process_G2_G3(const GCodeReader::GCodeLine& line, bool clockwise);

        // Retract or Set tool temperature
        void process_G10(const GCodeReader::GCodeLine& line);

        // Unretract
        void process_G11(const GCodeReader::GCodeLine& line);

        // Set Units to Inches
        void process_G20(const GCodeReader::GCodeLine& line);

        // Set Units to Millimeters
        void process_G21(const GCodeReader::GCodeLine& line);

        // Firmware controlled Retract
        void process_G22(const GCodeReader::GCodeLine& line);

        // Firmware controlled Unretract
        void process_G23(const GCodeReader::GCodeLine& line);

        // Move to origin
        void process_G28(const GCodeReader::GCodeLine& line);

        // Save Current Position
        void process_G60(const GCodeReader::GCodeLine& line);

        // Return to Saved Position
        void process_G61(const GCodeReader::GCodeLine& line);
 
        // Set to Absolute Positioning
        void process_G90(const GCodeReader::GCodeLine& line);

        // Set to Relative Positioning
        void process_G91(const GCodeReader::GCodeLine& line);

        // Set Position
        void process_G92(const GCodeReader::GCodeLine& line);

        // Sleep or Conditional stop
        void process_M1(const GCodeReader::GCodeLine& line);

        // Set extruder to absolute mode
        void process_M82(const GCodeReader::GCodeLine& line);

        // Set extruder to relative mode
        void process_M83(const GCodeReader::GCodeLine& line);

        // Set extruder temperature
        void process_M104(const GCodeReader::GCodeLine& line);

        // Set fan speed
        void process_M106(const GCodeReader::GCodeLine& line);

        // Disable fan
        void process_M107(const GCodeReader::GCodeLine& line);

        // Set tool (Sailfish)
        void process_M108(const GCodeReader::GCodeLine& line);

        // Set extruder temperature and wait
        void process_M109(const GCodeReader::GCodeLine& line);

        // Recall stored home offsets
        void process_M132(const GCodeReader::GCodeLine& line);

        // Set tool (MakerWare)
        void process_M135(const GCodeReader::GCodeLine& line);

        // Set max printing acceleration
        void process_M201(const GCodeReader::GCodeLine& line);

        // Set maximum feedrate
        void process_M203(const GCodeReader::GCodeLine& line);

        // Set default acceleration
        void process_M204(const GCodeReader::GCodeLine& line);

        // Advanced settings
        void process_M205(const GCodeReader::GCodeLine& line);

        // Set Feedrate Percentage
        void process_M220(const GCodeReader::GCodeLine& line);

        // Set extrude factor override percentage
        void process_M221(const GCodeReader::GCodeLine& line);

        // Repetier: Store x, y and z position
        void process_M401(const GCodeReader::GCodeLine& line);

        // Repetier: Go to stored position
        void process_M402(const GCodeReader::GCodeLine& line);

        // Set allowable instantaneous speed change
        void process_M566(const GCodeReader::GCodeLine& line);

        // Unload the current filament into the MK3 MMU2 unit at the end of print.
        void process_M702(const GCodeReader::GCodeLine& line);

        // Processes T line (Select Tool)
        void process_T(const GCodeReader::GCodeLine& line);
        void process_T(const std::string_view command);

        // post process the file with the given filename to:
        // 1) add remaining time lines M73 and update moves' gcode ids accordingly
        // 2) update used filament data
        void post_process();

        void store_move_vertex(EMoveType type, bool internal_only = false);

        void set_extrusion_role(GCodeExtrusionRole role);

        float minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_filament_load_time(size_t extruder_id);
        float get_filament_unload_time(size_t extruder_id);

        void process_custom_gcode_time(CustomGCode::Type code);
        void process_filaments(CustomGCode::Type code);

        void calculate_time(GCodeProcessorResult& result, size_t keep_last_n_blocks = 0, float additional_time = 0.0f);

        // Simulates firmware st_synchronize() call
        void simulate_st_synchronize(float additional_time = 0.0f);

        void update_estimated_statistics();

        double extract_absolute_position_on_axis(Axis axis, const GCodeReader::GCodeLine& line, double area_filament_cross_section);

   };

} /* namespace Slic3r */

#endif /* slic3r_GCodeProcessor_hpp_ */


