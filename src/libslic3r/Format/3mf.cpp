///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2020 Henner Zeller
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Time.hpp"

#include "libslic3r/I18N.hpp"

#include "3mf.hpp"

#include <limits>
#include <stdexcept>
#include <optional>
#include <string_view>

#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <boost/filesystem.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/qi_int.hpp>
#include <boost/log/trivial.hpp>

#include <boost/property_tree/xml_parser.hpp>
namespace pt = boost::property_tree;

#include <expat.h>
#include <Eigen/Dense>
#include <LocalesUtils.hpp>

#include "libslic3r/miniz_extension.hpp"

#include "libslic3r/TextConfiguration.hpp"
#include "libslic3r/EmbossShape.hpp"
#include "libslic3r/ExPolygonSerialize.hpp" 

#include "libslic3r/NSVGUtils.hpp"

#include <fast_float.h>

// Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point formatter,
// https://github.com/boostorg/spirit/pull/586
// where the exported string is one digit shorter than it should be to guarantee lossless round trip.
// The code is left here for the ocasion boost guys improve.
#define EXPORT_3MF_USE_SPIRIT_KARMA_FP 0

// VERSION NUMBERS
// 0 : .3mf, files saved by older slic3r or other applications. No version definition in them.
// 1 : Introduction of 3mf versioning. No other change in data saved into 3mf files.
// 2 : Volumes' matrices and source data added to Metadata/Slic3r_PE_model.config file, meshes transformed back to their coordinate system on loading.
// WARNING !! -> the version number has been rolled back to 1
//               the next change should use 3
const unsigned int VERSION_3MF = 1;
// Allow loading version 2 file as well.
const unsigned int VERSION_3MF_COMPATIBLE = 2;
const char* SLIC3RPE_3MF_VERSION = "slic3rpe:Version3mf"; // definition of the metadata name saved into .model file

// Painting gizmos data version numbers
// 0 : 3MF files saved by older PrusaSlicer or the painting gizmo wasn't used. No version definition in them.
// 1 : Introduction of painting gizmos data versioning. No other changes in painting gizmos data.
const unsigned int FDM_SUPPORTS_PAINTING_VERSION = 1;
const unsigned int SEAM_PAINTING_VERSION         = 1;
const unsigned int MM_PAINTING_VERSION           = 1;

const std::string SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION = "slic3rpe:FdmSupportsPaintingVersion";
const std::string SLIC3RPE_SEAM_PAINTING_VERSION         = "slic3rpe:SeamPaintingVersion";
const std::string SLIC3RPE_MM_PAINTING_VERSION           = "slic3rpe:MmPaintingVersion";

const std::string MODEL_FOLDER = "3D/";
const std::string MODEL_EXTENSION = ".model";
const std::string MODEL_FILE = "3D/3dmodel.model"; // << this is the only format of the string which works with CURA
const std::string CONTENT_TYPES_FILE = "[Content_Types].xml";
const std::string RELATIONSHIPS_FILE = "_rels/.rels";
const std::string THUMBNAIL_FILE = "Metadata/thumbnail.png";
const std::string PRINT_CONFIG_FILE = "Metadata/Slic3r_PE.config";
const std::string MODEL_CONFIG_FILE = "Metadata/Slic3r_PE_model.config";
const std::string LAYER_HEIGHTS_PROFILE_FILE = "Metadata/Slic3r_PE_layer_heights_profile.txt";
const std::string LAYER_CONFIG_RANGES_FILE = "Metadata/Prusa_Slicer_layer_config_ranges.xml";
const std::string SLA_SUPPORT_POINTS_FILE = "Metadata/Slic3r_PE_sla_support_points.txt";
const std::string SLA_DRAIN_HOLES_FILE = "Metadata/Slic3r_PE_sla_drain_holes.txt";
const std::string CUSTOM_GCODE_PER_PRINT_Z_FILE = "Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml";
const std::string CUT_INFORMATION_FILE = "Metadata/Prusa_Slicer_cut_information.xml";

static constexpr const char *RELATIONSHIP_TAG = "Relationship";

static constexpr const char* TARGET_ATTR = "Target";
static constexpr const char* RELS_TYPE_ATTR = "Type";

static constexpr const char* MODEL_TAG = "model";
static constexpr const char* RESOURCES_TAG = "resources";
static constexpr const char* OBJECT_TAG = "object";
static constexpr const char* MESH_TAG = "mesh";
static constexpr const char* VERTICES_TAG = "vertices";
static constexpr const char* VERTEX_TAG = "vertex";
static constexpr const char* TRIANGLES_TAG = "triangles";
static constexpr const char* TRIANGLE_TAG = "triangle";
static constexpr const char* COMPONENTS_TAG = "components";
static constexpr const char* COMPONENT_TAG = "component";
static constexpr const char* BUILD_TAG = "build";
static constexpr const char* ITEM_TAG = "item";
static constexpr const char* METADATA_TAG = "metadata";

static constexpr const char* CONFIG_TAG = "config";
static constexpr const char* VOLUME_TAG = "volume";

static constexpr const char* UNIT_ATTR = "unit";
static constexpr const char* NAME_ATTR = "name";
static constexpr const char* TYPE_ATTR = "type";
static constexpr const char* ID_ATTR = "id";
static constexpr const char* X_ATTR = "x";
static constexpr const char* Y_ATTR = "y";
static constexpr const char* Z_ATTR = "z";
static constexpr const char* V1_ATTR = "v1";
static constexpr const char* V2_ATTR = "v2";
static constexpr const char* V3_ATTR = "v3";
static constexpr const char* PPATH_ATTR = "p:path";
static constexpr const char* OBJECTID_ATTR = "objectid";
static constexpr const char* TRANSFORM_ATTR = "transform";
static constexpr const char* PRINTABLE_ATTR = "printable";
static constexpr const char* INSTANCESCOUNT_ATTR = "instances_count";
static constexpr const char* CUSTOM_SUPPORTS_ATTR = "slic3rpe:custom_supports";
static constexpr const char* CUSTOM_SEAM_ATTR = "slic3rpe:custom_seam";
static constexpr const char* MM_SEGMENTATION_ATTR = "slic3rpe:mmu_segmentation";

static constexpr const char* KEY_ATTR = "key";
static constexpr const char* VALUE_ATTR = "value";
static constexpr const char* FIRST_TRIANGLE_ID_ATTR = "firstid";
static constexpr const char* LAST_TRIANGLE_ID_ATTR = "lastid";

static constexpr const char* OBJECT_TYPE = "object";
static constexpr const char* VOLUME_TYPE = "volume";

static constexpr const char* NAME_KEY        = "name";
static constexpr const char* MODIFIER_KEY    = "modifier";
static constexpr const char* VOLUME_TYPE_KEY = "volume_type";
static constexpr const char* MATRIX_KEY      = "matrix";
static constexpr const char* SOURCE_FILE_KEY              = "source_file";
static constexpr const char* SOURCE_OBJECT_ID_KEY         = "source_object_id";
static constexpr const char* SOURCE_VOLUME_ID_KEY         = "source_volume_id";
static constexpr const char* SOURCE_OFFSET_X_KEY          = "source_offset_x";
static constexpr const char* SOURCE_OFFSET_Y_KEY          = "source_offset_y";
static constexpr const char* SOURCE_OFFSET_Z_KEY          = "source_offset_z";
static constexpr const char* SOURCE_IN_INCHES_KEY         = "source_in_inches";
static constexpr const char* SOURCE_IN_METERS_KEY         = "source_in_meters";
static constexpr const char* SOURCE_IS_BUILTIN_VOLUME_KEY = "source_is_builtin_volume";

static constexpr const char* MESH_STAT_EDGES_FIXED          = "edges_fixed";
static constexpr const char* MESH_STAT_DEGENERATED_FACETS   = "degenerate_facets";
static constexpr const char* MESH_STAT_FACETS_REMOVED       = "facets_removed";
static constexpr const char* MESH_STAT_FACETS_RESERVED      = "facets_reversed";
static constexpr const char* MESH_STAT_BACKWARDS_EDGES      = "backwards_edges";

// Store / load of TextConfiguration
static constexpr const char *TEXT_TAG = "slic3rpe:text";
static constexpr const char *TEXT_DATA_ATTR = "text";
// TextConfiguration::EmbossStyle
static constexpr const char *STYLE_NAME_ATTR      = "style_name";
static constexpr const char *FONT_DESCRIPTOR_ATTR = "font_descriptor";
static constexpr const char *FONT_DESCRIPTOR_TYPE_ATTR = "font_descriptor_type";

// TextConfiguration::FontProperty
static constexpr const char *CHAR_GAP_ATTR    = "char_gap";
static constexpr const char *LINE_GAP_ATTR    = "line_gap";
static constexpr const char *LINE_HEIGHT_ATTR = "line_height";
static constexpr const char *BOLDNESS_ATTR    = "boldness";
static constexpr const char *SKEW_ATTR        = "skew";
static constexpr const char *PER_GLYPH_ATTR   = "per_glyph";
static constexpr const char *HORIZONTAL_ALIGN_ATTR  = "horizontal";
static constexpr const char *VERTICAL_ALIGN_ATTR    = "vertical";
static constexpr const char *COLLECTION_NUMBER_ATTR = "collection";

static constexpr const char *FONT_FAMILY_ATTR    = "family";
static constexpr const char *FONT_FACE_NAME_ATTR = "face_name";
static constexpr const char *FONT_STYLE_ATTR     = "style";
static constexpr const char *FONT_WEIGHT_ATTR    = "weight";

// Store / load of EmbossShape
static constexpr const char *SHAPE_TAG = "slic3rpe:shape";
static constexpr const char *SHAPE_SCALE_ATTR   = "scale";
static constexpr const char *UNHEALED_ATTR = "unhealed";
static constexpr const char *SVG_FILE_PATH_ATTR = "filepath";
static constexpr const char *SVG_FILE_PATH_IN_3MF_ATTR = "filepath3mf";

// EmbossProjection
static constexpr const char *DEPTH_ATTR       = "depth";
static constexpr const char *USE_SURFACE_ATTR = "use_surface";
// static constexpr const char *FIX_TRANSFORMATION_ATTR = "transform";


const unsigned int VALID_OBJECT_TYPES_COUNT = 1;
const char* VALID_OBJECT_TYPES[] =
{
    "model"
};

const char* INVALID_OBJECT_TYPES[] =
{
    "solidsupport",
    "support",
    "surface",
    "other"
};

class version_error : public Slic3r::FileIOError
{
public:
    version_error(const std::string& what_arg) : Slic3r::FileIOError(what_arg) {}
    version_error(const char* what_arg) : Slic3r::FileIOError(what_arg) {}
};

const char* get_attribute_value_charptr(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    if ((attributes == nullptr) || (attributes_size == 0) || (attributes_size % 2 != 0) || (attribute_key == nullptr))
        return nullptr;

    for (unsigned int a = 0; a < attributes_size; a += 2) {
        if (::strcmp(attributes[a], attribute_key) == 0)
            return attributes[a + 1];
    }

    return nullptr;
}

std::string get_attribute_value_string(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? text : "";
}

float get_attribute_value_float(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    float value = 0.0f;
    if (const char *text = get_attribute_value_charptr(attributes, attributes_size, attribute_key); text != nullptr)
        fast_float::from_chars(text, text + strlen(text), value);
    return value;
}

int get_attribute_value_int(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    int value = 0;
    if (const char *text = get_attribute_value_charptr(attributes, attributes_size, attribute_key); text != nullptr)
        boost::spirit::qi::parse(text, text + strlen(text), boost::spirit::qi::int_, value);
    return value;
}

bool get_attribute_value_bool(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? (bool)::atoi(text) : true;
}

Slic3r::Transform3d get_transform_from_3mf_specs_string(const std::string& mat_str)
{
    // check: https://3mf.io/3d-manufacturing-format/ or https://github.com/3MFConsortium/spec_core/blob/master/3MF%20Core%20Specification.md
    // to see how matrices are stored inside 3mf according to specifications
    Slic3r::Transform3d ret = Slic3r::Transform3d::Identity();

    if (mat_str.empty())
        // empty string means default identity matrix
        return ret;

    std::vector<std::string> mat_elements_str;
    boost::split(mat_elements_str, mat_str, boost::is_any_of(" "), boost::token_compress_on);

    unsigned int size = (unsigned int)mat_elements_str.size();
    if (size != 12)
        // invalid data, return identity matrix
        return ret;

    unsigned int i = 0;
    // matrices are stored into 3mf files as 4x3
    // we need to transpose them
    for (unsigned int c = 0; c < 4; ++c) {
        for (unsigned int r = 0; r < 3; ++r) {
            ret(r, c) = ::atof(mat_elements_str[i++].c_str());
        }
    }
    return ret;
}

float get_unit_factor(const std::string& unit)
{
    const char* text = unit.c_str();

    if (::strcmp(text, "micron") == 0)
        return 0.001f;
    else if (::strcmp(text, "centimeter") == 0)
        return 10.0f;
    else if (::strcmp(text, "inch") == 0)
        return 25.4f;
    else if (::strcmp(text, "foot") == 0)
        return 304.8f;
    else if (::strcmp(text, "meter") == 0)
        return 1000.0f;
    else
        // default "millimeters" (see specification)
        return 1.0f;
}

bool is_valid_object_type(const std::string& type)
{
    // if the type is empty defaults to "model" (see specification)
    if (type.empty())
        return true;

    for (unsigned int i = 0; i < VALID_OBJECT_TYPES_COUNT; ++i) {
        if (::strcmp(type.c_str(), VALID_OBJECT_TYPES[i]) == 0)
            return true;
    }

    return false;
}

namespace Slic3r {

    // Base class with error messages management
    class _3MF_Base
    {
        std::vector<std::string> m_errors;

    protected:
        void add_error(const std::string& error) { m_errors.push_back(error); }
        void clear_errors() { m_errors.clear(); }

    public:
        void log_errors()
        {
            for (const std::string& error : m_errors)
                BOOST_LOG_TRIVIAL(error) << error;
        }
    };

    class _3MF_Importer : public _3MF_Base
    {
        typedef std::pair<std::string, int> PathId;

        struct Component
        {
            PathId object_id;
            std::string path;
            Transform3d transform;

            explicit Component(PathId object_id)
                : object_id(object_id)
                , transform(Transform3d::Identity())
            {
            }

            Component(PathId object_id, const Transform3d &transform)
                : object_id(object_id)
                , transform(transform)
            {
            }
        };

        typedef std::vector<Component> ComponentsList;

        struct Geometry
        {
            std::vector<Vec3f> vertices;
            std::vector<Vec3i> triangles;
            std::vector<std::string> custom_supports;
            std::vector<std::string> custom_seam;
            std::vector<std::string> mm_segmentation;

            bool empty() { return vertices.empty() || triangles.empty(); }

            void reset() {
                vertices.clear();
                triangles.clear();
                custom_supports.clear();
                custom_seam.clear();
                mm_segmentation.clear();
            }
        };

        struct CurrentObject
        {
            // ID of the object inside the 3MF file, 1 based.
            int id;
            // Index of the ModelObject in its respective Model, zero based.
            int model_object_idx;
            Geometry geometry;
            ModelObject* object;
            ComponentsList components;

            CurrentObject() { reset(); }

            void reset() {
                id = -1;
                model_object_idx = -1;
                geometry.reset();
                object = nullptr;
                components.clear();
            }
        };

        struct CurrentConfig
        {
            int object_id;
            int volume_id;
        };

        struct Instance
        {
            ModelInstance* instance;
            Transform3d transform;

            Instance(ModelInstance* instance, const Transform3d& transform)
                : instance(instance)
                , transform(transform)
            {
            }
        };

        struct Metadata
        {
            std::string key;
            std::string value;

            Metadata(const std::string& key, const std::string& value)
                : key(key)
                , value(value)
            {
            }
        };

        typedef std::vector<Metadata> MetadataList;

        struct ObjectMetadata
        {
            struct VolumeMetadata
            {
                unsigned int first_triangle_id;
                unsigned int last_triangle_id;
                MetadataList metadata;
                RepairedMeshErrors mesh_stats;
                std::optional<TextConfiguration> text_configuration;
                std::optional<EmbossShape> shape_configuration;
                VolumeMetadata(unsigned int first_triangle_id, unsigned int last_triangle_id)
                    : first_triangle_id(first_triangle_id)
                    , last_triangle_id(last_triangle_id)
                {
                }
            };

            typedef std::vector<VolumeMetadata> VolumeMetadataList;

            MetadataList metadata;
            VolumeMetadataList volumes;
        };

        struct CutObjectInfo
        {
            struct Connector
            {
                int     volume_id;
                int     type;
                float   r_tolerance;
                float   h_tolerance;
            };
            CutObjectBase           id;
            std::vector<Connector>  connectors;
        };

        // Map from a 1 based 3MF object ID to a 0 based ModelObject index inside m_model->objects.
        typedef std::map<PathId, int> IdToModelObjectMap;
        typedef std::map<PathId, ComponentsList> IdToAliasesMap;
        typedef std::vector<Instance> InstancesList;
        typedef std::map<int, ObjectMetadata> IdToMetadataMap;
        typedef std::map<PathId, Geometry> IdToGeometryMap;
        typedef std::map<int, std::vector<coordf_t>> IdToLayerHeightsProfileMap;
        typedef std::map<int, t_layer_config_ranges> IdToLayerConfigRangesMap;
        typedef std::map<int, CutObjectInfo>         IdToCutObjectInfoMap;
        typedef std::map<int, std::vector<sla::SupportPoint>> IdToSlaSupportPointsMap;
        typedef std::map<int, std::vector<sla::DrainHole>> IdToSlaDrainHolesMap;
        using PathToEmbossShapeFileMap = std::map<std::string, std::shared_ptr<std::string>>;
        // Version of the 3mf file
        unsigned int m_version;
        bool m_check_version;

        // Semantic version of PrusaSlicer, that generated this 3MF.
        boost::optional<Semver> m_prusaslicer_generator_version;
        unsigned int m_fdm_supports_painting_version = 0;
        unsigned int m_seam_painting_version         = 0;
        unsigned int m_mm_painting_version           = 0;

        XML_Parser m_xml_parser;
        // Error code returned by the application side of the parser. In that case the expat may not reliably deliver the error state
        // after returning from XML_Parse() function, thus we keep the error state here.
        bool m_parse_error { false };
        std::string m_parse_error_message;
        Model* m_model;
        float m_unit_factor;
        CurrentObject m_curr_object;
        IdToModelObjectMap m_objects;
        IdToAliasesMap m_objects_aliases;
        InstancesList m_instances;
        IdToGeometryMap m_geometries;
        CurrentConfig m_curr_config;
        IdToMetadataMap m_objects_metadata;
        IdToCutObjectInfoMap m_cut_object_infos;
        IdToLayerHeightsProfileMap m_layer_heights_profiles;
        IdToLayerConfigRangesMap m_layer_config_ranges;
        IdToSlaSupportPointsMap m_sla_support_points;
        IdToSlaDrainHolesMap    m_sla_drain_holes;
        PathToEmbossShapeFileMap m_path_to_emboss_shape_files;
        std::string m_curr_metadata_name;
        std::string m_curr_characters;
        std::string m_name;
        std::string m_start_part_path;
        std::string m_model_path;

    public:
        _3MF_Importer();
        ~_3MF_Importer();

        bool load_model_from_file(const std::string& filename, Model& model, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, bool check_version);
        unsigned int version() const { return m_version; }
        boost::optional<Semver> prusaslicer_generator_version() const { return m_prusaslicer_generator_version; }

    private:
        void _destroy_xml_parser();
        void _stop_xml_parser(const std::string& msg = std::string());

        bool        parse_error()         const { return m_parse_error; }
        const char* parse_error_message() const {
            return m_parse_error ?
                // The error was signalled by the user code, not the expat parser.
                (m_parse_error_message.empty() ? "Invalid 3MF format" : m_parse_error_message.c_str()) :
                // The error was signalled by the expat parser.
                XML_ErrorString(XML_GetErrorCode(m_xml_parser));
        }

        bool _load_model_from_file(const std::string& filename, Model& model, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions);
        bool _extract_relationships_from_archive(mz_zip_archive &archive, const mz_zip_archive_file_stat &stat);
        bool _extract_model_from_archive(mz_zip_archive &archive, const mz_zip_archive_file_stat &stat);
        bool _is_svg_shape_file(const std::string &filename) const;
        void _extract_cut_information_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions);
        void _extract_layer_heights_profile_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        void _extract_layer_config_ranges_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions);
        void _extract_sla_support_points_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        void _extract_sla_drain_holes_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);

        void _extract_custom_gcode_per_print_z_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);

        void _extract_print_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, DynamicPrintConfig& config, ConfigSubstitutionContext& subs_context, const std::string& archive_filename);
        bool _extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model);
        void _extract_embossed_svg_shape_file(const std::string &filename, mz_zip_archive &archive, const mz_zip_archive_file_stat &stat);

        // handlers to parse the .rels file
        void _handle_start_relationships_element(const char* name, const char** attributes);
        bool _handle_start_relationship(const char **attributes, unsigned int num_attributes);

        // handlers to parse the .model file
        void _handle_start_model_xml_element(const char* name, const char** attributes);
        void _handle_end_model_xml_element(const char* name);
        void _handle_model_xml_characters(const XML_Char* s, int len);

        // handlers to parse the MODEL_CONFIG_FILE file
        void _handle_start_config_xml_element(const char* name, const char** attributes);
        void _handle_end_config_xml_element(const char* name);

        bool _handle_start_model(const char** attributes, unsigned int num_attributes);
        bool _handle_end_model();

        bool _handle_start_resources(const char** attributes, unsigned int num_attributes);
        bool _handle_end_resources();

        bool _handle_start_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_object();

        bool _handle_start_mesh(const char** attributes, unsigned int num_attributes);
        bool _handle_end_mesh();

        bool _handle_start_vertices(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertices();

        bool _handle_start_vertex(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertex();

        bool _handle_start_triangles(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangles();

        bool _handle_start_triangle(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangle();

        bool _handle_start_components(const char** attributes, unsigned int num_attributes);
        bool _handle_end_components();

        bool _handle_start_component(const char** attributes, unsigned int num_attributes);
        bool _handle_end_component();

        bool _handle_start_build(const char** attributes, unsigned int num_attributes);
        bool _handle_end_build();

        bool _handle_start_item(const char** attributes, unsigned int num_attributes);
        bool _handle_end_item();

        bool _handle_start_metadata(const char** attributes, unsigned int num_attributes);
        bool _handle_end_metadata();

        bool _handle_start_text_configuration(const char** attributes, unsigned int num_attributes);
        bool _handle_start_shape_configuration(const char **attributes, unsigned int num_attributes);

        bool _create_object_instance(PathId object_id, const Transform3d& transform, const bool printable, unsigned int recur_counter);

        void _apply_transform(ModelInstance& instance, const Transform3d& transform);

        bool _handle_start_config(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config();

        bool _handle_start_config_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_object();

        bool _handle_start_config_volume(const char** attributes, unsigned int num_attributes);
        bool _handle_start_config_volume_mesh(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_volume();
        bool _handle_end_config_volume_mesh();

        bool _handle_start_config_metadata(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_metadata();

        bool _generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes, ConfigSubstitutionContext& config_substitutions);

        // callbacks to parse the .rels file
        static void XMLCALL _handle_start_relationships_element(void *userData, const char *name, const char **attributes);

        // callbacks to parse the .model file
        static void XMLCALL _handle_start_model_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_model_xml_element(void* userData, const char* name);
        static void XMLCALL _handle_model_xml_characters(void* userData, const XML_Char* s, int len);

        // callbacks to parse the MODEL_CONFIG_FILE file
        static void XMLCALL _handle_start_config_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_config_xml_element(void* userData, const char* name);
    };

    _3MF_Importer::_3MF_Importer()
        : m_version(0)
        , m_check_version(false)
        , m_xml_parser(nullptr)
        , m_model(nullptr)   
        , m_unit_factor(1.0f)
        , m_curr_metadata_name("")
        , m_curr_characters("")
        , m_name("")
    {
    }

    _3MF_Importer::~_3MF_Importer()
    {
        _destroy_xml_parser();
    }

    bool _3MF_Importer::load_model_from_file(const std::string& filename, Model& model, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, bool check_version)
    {
        m_version = 0;
        m_fdm_supports_painting_version = 0;
        m_seam_painting_version = 0;
        m_mm_painting_version = 0;
        m_check_version = check_version;
        m_model = &model;
        m_unit_factor = 1.0f;
        m_curr_object.reset();
        m_objects.clear();
        m_objects_aliases.clear();
        m_instances.clear();
        m_geometries.clear();
        m_curr_config.object_id = -1;
        m_curr_config.volume_id = -1;
        m_objects_metadata.clear();
        m_layer_heights_profiles.clear();
        m_layer_config_ranges.clear();
        m_sla_support_points.clear();
        m_curr_metadata_name.clear();
        m_curr_characters.clear();
        m_start_part_path = MODEL_FILE; // set default value for invalid .rel file
        clear_errors();

        return _load_model_from_file(filename, model, config, config_substitutions);
    }

    void _3MF_Importer::_destroy_xml_parser()
    {
        if (m_xml_parser != nullptr) {
            XML_ParserFree(m_xml_parser);
            m_xml_parser = nullptr;
        }
    }

    void _3MF_Importer::_stop_xml_parser(const std::string &msg)
    {
        assert(! m_parse_error);
        assert(m_parse_error_message.empty());
        assert(m_xml_parser != nullptr);
        m_parse_error = true;
        m_parse_error_message = msg;
        XML_StopParser(m_xml_parser, false);
    }

    bool _3MF_Importer::_load_model_from_file(const std::string& filename, Model& model, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        if (!open_zip_reader(&archive, filename)) {
            add_error("Unable to open the file");
            return false;
        }

        mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

        mz_zip_archive_file_stat stat;

        m_name = boost::filesystem::path(filename).stem().string();

        int index = mz_zip_reader_locate_file(&archive, RELATIONSHIPS_FILE.c_str(), nullptr, 0);
        if (index < 0 || !mz_zip_reader_file_stat(&archive, index, &stat))
            return false;
        
        mz_zip_archive_file_stat start_part_stat{std::numeric_limits<mz_uint32>::max()};
        m_model_path = MODEL_FILE;
        _extract_relationships_from_archive(archive, stat);
        bool found_model = false;

        // we first loop the entries to read from the .model files which are not root
        for (mz_uint i = 0; i < num_entries; ++i) {
            if (mz_zip_reader_file_stat(&archive, i, &stat)) {
                std::string name(stat.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');

                if (boost::algorithm::iends_with(name, MODEL_EXTENSION)) {
                    // valid model name -> extract model
                    m_model_path = "/" + name;
                    if (m_model_path == m_start_part_path) {
                        start_part_stat = stat;
                        continue;
                    }
                    try {
                        if (!_extract_model_from_archive(archive, stat)) {
                            close_zip_reader(&archive);
                            add_error("Archive does not contain a valid model");
                            return false;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        // ensure the zip archive is closed and rethrow the exception
                        close_zip_reader(&archive);
                        throw Slic3r::FileIOError(e.what());
                    }
                    found_model = true;
                }
            }
        }

        // Read root model file
        if (start_part_stat.m_file_index < num_entries) {
            try {
                m_model_path.clear();
                if (!_extract_model_from_archive(archive, start_part_stat)) {
                    close_zip_reader(&archive);
                    add_error("Archive does not contain a valid model");
                    return false;
                }
            } catch (const std::exception &e) {
                // ensure the zip archive is closed and rethrow the exception
                close_zip_reader(&archive);
                throw Slic3r::FileIOError(e.what());
            }
            found_model = true;
        }
        if (!found_model) {
            close_zip_reader(&archive);
            add_error("Not valid 3mf. There is missing .model file.");
            return false;
        }

        // we then loop again the entries to read other files stored in the archive
        for (mz_uint i = 0; i < num_entries; ++i) {
            if (mz_zip_reader_file_stat(&archive, i, &stat)) {
                std::string name(stat.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');

                if (boost::algorithm::iequals(name, LAYER_HEIGHTS_PROFILE_FILE)) {
                    // extract slic3r layer heights profile file
                    _extract_layer_heights_profile_config_from_archive(archive, stat);
                }
                else if (boost::algorithm::iequals(name, CUT_INFORMATION_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_cut_information_from_archive(archive, stat, config_substitutions);
                }
                else if (boost::algorithm::iequals(name, LAYER_CONFIG_RANGES_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_layer_config_ranges_from_archive(archive, stat, config_substitutions);
                }
                else if (boost::algorithm::iequals(name, SLA_SUPPORT_POINTS_FILE)) {
                    // extract sla support points file
                    _extract_sla_support_points_from_archive(archive, stat);
                }
                else if (boost::algorithm::iequals(name, SLA_DRAIN_HOLES_FILE)) {
                    // extract sla support points file
                    _extract_sla_drain_holes_from_archive(archive, stat);
                }
                else if (boost::algorithm::iequals(name, PRINT_CONFIG_FILE)) {
                    // extract slic3r print config file
                    _extract_print_config_from_archive(archive, stat, config, config_substitutions, filename);
                }
                else if (boost::algorithm::iequals(name, CUSTOM_GCODE_PER_PRINT_Z_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_custom_gcode_per_print_z_from_archive(archive, stat);
                }
                else if (boost::algorithm::iequals(name, MODEL_CONFIG_FILE)) {
                    // extract slic3r model config file
                    if (!_extract_model_config_from_archive(archive, stat, model)) {
                        close_zip_reader(&archive);
                        add_error("Archive does not contain a valid model config");
                        return false;
                    }
                } 
                else if (_is_svg_shape_file(name)) {
                    _extract_embossed_svg_shape_file(name, archive, stat);
                }
            }
        }

        close_zip_reader(&archive);

        if (m_version == 0) {
            // if the 3mf was not produced by PrusaSlicer and there is more than one instance,
            // split the object in as many objects as instances
            size_t curr_models_count = m_model->objects.size();
            size_t i = 0;
            while (i < curr_models_count) {
                ModelObject* model_object = m_model->objects[i];
                if (model_object->instances.size() > 1) {
                    // select the geometry associated with the original model object
                    const Geometry* geometry = nullptr;
                    for (const IdToModelObjectMap::value_type& object : m_objects) {
                        if (object.second == int(i)) {
                            IdToGeometryMap::const_iterator obj_geometry = m_geometries.find(object.first);
                            if (obj_geometry == m_geometries.end()) {
                                add_error("Unable to find object geometry");
                                return false;
                            }
                            geometry = &obj_geometry->second;
                            break;
                        }
                    }

                    if (geometry == nullptr) {
                        add_error("Unable to find object geometry");
                        return false;
                    }

                    // use the geometry to create the volumes in the new model objects
                    ObjectMetadata::VolumeMetadataList volumes(1, { 0, (unsigned int)geometry->triangles.size() - 1 });

                    // for each instance after the 1st, create a new model object containing only that instance
                    // and copy into it the geometry
                    while (model_object->instances.size() > 1) {
                        ModelObject* new_model_object = m_model->add_object(*model_object);
                        new_model_object->clear_instances();
                        new_model_object->add_instance(*model_object->instances.back());
                        model_object->delete_last_instance();
                        if (!_generate_volumes(*new_model_object, *geometry, volumes, config_substitutions))
                            return false;
                    }
                }
                ++i;
            }
        }

        for (const IdToModelObjectMap::value_type& object : m_objects) {
            if (object.second >= int(m_model->objects.size())) {
                add_error("Unable to find object");
                return false;
            }
            ModelObject* model_object = m_model->objects[object.second];
            IdToGeometryMap::const_iterator obj_geometry = m_geometries.find(object.first);
            if (obj_geometry == m_geometries.end()) {
                add_error("Unable to find object geometry");
                return false;
            }

            // m_layer_heights_profiles are indexed by a 1 based model object index.
            IdToLayerHeightsProfileMap::iterator obj_layer_heights_profile = m_layer_heights_profiles.find(object.second + 1);
            if (obj_layer_heights_profile != m_layer_heights_profiles.end())
                model_object->layer_height_profile.set(std::move(obj_layer_heights_profile->second));

            // m_layer_config_ranges are indexed by a 1 based model object index.
            IdToLayerConfigRangesMap::iterator obj_layer_config_ranges = m_layer_config_ranges.find(object.second + 1);
            if (obj_layer_config_ranges != m_layer_config_ranges.end())
                model_object->layer_config_ranges = std::move(obj_layer_config_ranges->second);

            // m_sla_support_points are indexed by a 1 based model object index.
            IdToSlaSupportPointsMap::iterator obj_sla_support_points = m_sla_support_points.find(object.second + 1);
            if (obj_sla_support_points != m_sla_support_points.end() && !obj_sla_support_points->second.empty()) {
                model_object->sla_support_points = std::move(obj_sla_support_points->second);
                model_object->sla_points_status = sla::PointsStatus::UserModified;
            }

            IdToSlaDrainHolesMap::iterator obj_drain_holes = m_sla_drain_holes.find(object.second + 1);
            if (obj_drain_holes != m_sla_drain_holes.end() && !obj_drain_holes->second.empty()) {
                model_object->sla_drain_holes = std::move(obj_drain_holes->second);
            }

            ObjectMetadata::VolumeMetadataList volumes;
            ObjectMetadata::VolumeMetadataList* volumes_ptr = nullptr;

            IdToMetadataMap::iterator obj_metadata = m_objects_metadata.find(object.first.second);
            if (obj_metadata != m_objects_metadata.end()) {
                // config data has been found, this model was saved using slic3r pe

                // apply object's name and config data
                for (const Metadata& metadata : obj_metadata->second.metadata) {
                    if (metadata.key == "name")
                        model_object->name = metadata.value;
                    else
                        model_object->config.set_deserialize(metadata.key, metadata.value, config_substitutions);
                }

                // select object's detected volumes
                volumes_ptr = &obj_metadata->second.volumes;
            }
            else {
                // config data not found, this model was not saved using slic3r pe

                // add the entire geometry as the single volume to generate
                volumes.emplace_back(0, (int)obj_geometry->second.triangles.size() - 1);

                // select as volumes
                volumes_ptr = &volumes;
            }

            if (!_generate_volumes(*model_object, obj_geometry->second, *volumes_ptr, config_substitutions))
                return false;

            // Apply cut information for object if any was loaded
            // m_cut_object_ids are indexed by a 1 based model object index.
            IdToCutObjectInfoMap::iterator cut_object_info = m_cut_object_infos.find(object.second + 1);
            if (cut_object_info != m_cut_object_infos.end()) {
                model_object->cut_id = cut_object_info->second.id;
                int vol_cnt = int(model_object->volumes.size());
                for (auto connector : cut_object_info->second.connectors) {
                    if (connector.volume_id < 0 || connector.volume_id >= vol_cnt) {
                        add_error("Invalid connector is found");
                        continue;
                    }
                    model_object->volumes[connector.volume_id]->cut_info = 
                        ModelVolume::CutInfo(CutConnectorType(connector.type), connector.r_tolerance, connector.h_tolerance, true);
                }
            }
        }

        // If instances contain a single volume, the volume offset should be 0,0,0
        // This equals to say that instance world position and volume world position should match
        // Correct all instances/volumes for which this does not hold
        for (int obj_id = 0; obj_id < int(model.objects.size()); ++obj_id) {
            ModelObject* o = model.objects[obj_id];
            if (o->volumes.size() == 1) {
                ModelVolume* v = o->volumes.front();
                const Slic3r::Geometry::Transformation& first_inst_trafo = o->instances.front()->get_transformation();
                const Vec3d world_vol_offset = (first_inst_trafo * v->get_transformation()).get_offset();
                const Vec3d world_inst_offset = first_inst_trafo.get_offset();

                if (!world_vol_offset.isApprox(world_inst_offset)) {
                    const Slic3r::Geometry::Transformation& vol_trafo = v->get_transformation();
                    for (int inst_id = 0; inst_id < int(o->instances.size()); ++inst_id) {
                        ModelInstance* i = o->instances[inst_id];
                        const Slic3r::Geometry::Transformation& inst_trafo = i->get_transformation();
                        i->set_offset((inst_trafo * vol_trafo).get_offset());
                    }
                    v->set_offset(Vec3d::Zero());
                }
            }
        }

        for (int obj_id = 0; obj_id < int(model.objects.size()); ++obj_id) {
            ModelObject* o = model.objects[obj_id];
            for (int vol_id = 0; vol_id < int(o->volumes.size()); ++vol_id) {
                ModelVolume* v = o->volumes[vol_id];
                if (v->source.input_file.empty())
                    v->source.input_file = filename;
                if (v->source.volume_idx == -1)
                    v->source.volume_idx = vol_id;
                if (v->source.object_idx == -1)
                    v->source.object_idx = obj_id;
            }
        }

        // We support our 3mf contains only configuration without mesh,
        // others MUST contain mesh (triangles and vertices).
        if (!m_prusaslicer_generator_version.has_value() && model.objects.empty()) {
            const std::string msg = (boost::format(_u8L("The 3MF file does not contain a valid mesh.\n\n\"%1%\"")) % filename).str();
            throw Slic3r::RuntimeError(msg);
        }

        return true;
    }

    bool _3MF_Importer::_extract_relationships_from_archive(mz_zip_archive &archive, const mz_zip_archive_file_stat &stat)
    {
        if (stat.m_uncomp_size == 0 ||
            stat.m_uncomp_size > 10000000 // Prevent overloading by big Relations file(>10MB). there is no reason to be soo big
            ) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void *) this);
        XML_SetStartElementHandler(m_xml_parser, _handle_start_relationships_element);

        void *parser_buffer = XML_GetBuffer(m_xml_parser, (int) stat.m_uncomp_size);
        if (parser_buffer == nullptr) {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t) stat.m_uncomp_size, 0);
        if (res == 0) {
            add_error("Error while reading config data to buffer");
            return false;
        }

        if (!XML_ParseBuffer(m_xml_parser, (int) stat.m_uncomp_size, 1)) {
            char error_buf[1024];
            ::sprintf(error_buf, "Error (%s) while parsing xml file at line %d", XML_ErrorString(XML_GetErrorCode(m_xml_parser)),
                      (int) XML_GetCurrentLineNumber(m_xml_parser));
            add_error(error_buf);
            return false;
        }

        return true;
    }

    bool _3MF_Importer::_is_svg_shape_file(const std::string &name) const { 
        return boost::starts_with(name, MODEL_FOLDER) && boost::ends_with(name, ".svg");
    }

    bool _3MF_Importer::_extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size == 0) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _3MF_Importer::_handle_start_model_xml_element, _3MF_Importer::_handle_end_model_xml_element);
        XML_SetCharacterDataHandler(m_xml_parser, _3MF_Importer::_handle_model_xml_characters);

        struct CallbackData
        {
            XML_Parser& parser;
            _3MF_Importer& importer;
            const mz_zip_archive_file_stat& stat;

            CallbackData(XML_Parser& parser, _3MF_Importer& importer, const mz_zip_archive_file_stat& stat) : parser(parser), importer(importer), stat(stat) {}
        };

        CallbackData data(m_xml_parser, *this, stat);

        mz_bool res = 0;

        try
        {
            res = mz_zip_reader_extract_to_callback(&archive, stat.m_file_index, [](void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n)->size_t {
                CallbackData* data = (CallbackData*)pOpaque;
                if (!XML_Parse(data->parser, (const char*)pBuf, (int)n, (file_ofs + n == data->stat.m_uncomp_size) ? 1 : 0) || data->importer.parse_error()) {
                    char error_buf[1024];
                    ::sprintf(error_buf, "Error (%s) while parsing '%s' at line %d", data->importer.parse_error_message(), data->stat.m_filename, (int)XML_GetCurrentLineNumber(data->parser));
                    throw Slic3r::FileIOError(error_buf);
                }

                return n;
                }, &data, 0);
        }
        catch (const version_error& e)
        {
            // rethrow the exception
            throw Slic3r::FileIOError(e.what());
        }
        catch (std::exception& e)
        {
            add_error(e.what());
            return false;
        }

        if (res == 0) {
            add_error("Error while extracting model data from ZIP archive");
            return false;
        }

        return true;
    }

    void _3MF_Importer::_extract_cut_information_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading cut information data to buffer");
                return;
            }

            std::istringstream iss(buffer); // wrap returned xml to istringstream
            pt::ptree objects_tree;
            pt::read_xml(iss, objects_tree);

            for (const auto& object : objects_tree.get_child("objects")) {
                pt::ptree object_tree = object.second;
                int obj_idx = object_tree.get<int>("<xmlattr>.id", -1);
                if (obj_idx <= 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToCutObjectInfoMap::iterator object_item = m_cut_object_infos.find(obj_idx);
                if (object_item != m_cut_object_infos.end()) {
                    add_error("Found duplicated cut_object_id");
                    continue;
                }

                CutObjectBase cut_id;
                std::vector<CutObjectInfo::Connector>  connectors;

                for (const auto& obj_cut_info : object_tree) {
                    if (obj_cut_info.first == "cut_id") {
                        pt::ptree cut_id_tree = obj_cut_info.second;
                        cut_id = CutObjectBase(ObjectID( cut_id_tree.get<size_t>("<xmlattr>.id")),
                                                         cut_id_tree.get<size_t>("<xmlattr>.check_sum"),
                                                         cut_id_tree.get<size_t>("<xmlattr>.connectors_cnt"));
                    }
                    if (obj_cut_info.first == "connectors") {
                        pt::ptree cut_connectors_tree = obj_cut_info.second;
                        for (const auto& cut_connector : cut_connectors_tree) {
                            if (cut_connector.first != "connector")
                                continue;
                            pt::ptree connector_tree = cut_connector.second;
                            CutObjectInfo::Connector connector = {connector_tree.get<int>("<xmlattr>.volume_id"),
                                                                  connector_tree.get<int>("<xmlattr>.type"),
                                                                  connector_tree.get<float>("<xmlattr>.r_tolerance"),
                                                                  connector_tree.get<float>("<xmlattr>.h_tolerance")};
                            connectors.emplace_back(connector);
                        }
                    }
                }

                CutObjectInfo cut_info {cut_id, connectors};
                m_cut_object_infos.insert({ obj_idx, cut_info });
            }
        }
    }

    void _3MF_Importer::_extract_print_config_from_archive(
        mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, 
        DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, 
        const std::string& archive_filename)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading config data to buffer");
                return;
            }
            //FIXME Loading a "will be one day a legacy format" of configuration in a form of a G-code comment.
            // Each config line is prefixed with a semicolon (G-code comment), that is ugly.

            // Replacing the legacy function with load_from_ini_string_commented leads to issues when
            // parsing 3MFs from before PrusaSlicer 2.0.0 (which can have duplicated entries in the INI.
            // See https://github.com/prusa3d/PrusaSlicer/issues/7155. We'll revert it for now.
            //config_substitutions.substitutions = config.load_from_ini_string_commented(std::move(buffer), config_substitutions.rule);
            ConfigBase::load_from_gcode_string_legacy(config, buffer.data(), config_substitutions);
        }
    }

    void _3MF_Importer::_extract_layer_heights_profile_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading layer heights profile data to buffer");
                return;
            }

            if (buffer.back() == '\n')
                buffer.pop_back();

            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);

            for (const std::string& object : objects)             {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);
                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }

                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }

                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToLayerHeightsProfileMap::iterator object_item = m_layer_heights_profiles.find(object_id);
                if (object_item != m_layer_heights_profiles.end()) {
                    add_error("Found duplicated layer heights profile");
                    continue;
                }

                std::vector<std::string> object_data_profile;
                boost::split(object_data_profile, object_data[1], boost::is_any_of(";"), boost::token_compress_off);
                if (object_data_profile.size() <= 4 || object_data_profile.size() % 2 != 0) {
                    add_error("Found invalid layer heights profile");
                    continue;
                }

                std::vector<coordf_t> profile;
                profile.reserve(object_data_profile.size());

                for (const std::string& value : object_data_profile) {
                    profile.push_back((coordf_t)std::atof(value.c_str()));
                }

                m_layer_heights_profiles.insert({ object_id, profile });
            }
        }
    }

    void _3MF_Importer::_extract_layer_config_ranges_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading layer config ranges data to buffer");
                return;
            }

            std::istringstream iss(buffer); // wrap returned xml to istringstream
            pt::ptree objects_tree;
            pt::read_xml(iss, objects_tree);

            for (const auto& object : objects_tree.get_child("objects")) {
                pt::ptree object_tree = object.second;
                int obj_idx = object_tree.get<int>("<xmlattr>.id", -1);
                if (obj_idx <= 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToLayerConfigRangesMap::iterator object_item = m_layer_config_ranges.find(obj_idx);
                if (object_item != m_layer_config_ranges.end()) {
                    add_error("Found duplicated layer config range");
                    continue;
                }

                t_layer_config_ranges config_ranges;

                for (const auto& range : object_tree) {
                    if (range.first != "range")
                        continue;
                    pt::ptree range_tree = range.second;
                    double min_z = range_tree.get<double>("<xmlattr>.min_z");
                    double max_z = range_tree.get<double>("<xmlattr>.max_z");

                    // get Z range information
                    DynamicPrintConfig config;

                    for (const auto& option : range_tree) {
                        if (option.first != "option")
                            continue;
                        std::string opt_key = option.second.get<std::string>("<xmlattr>.opt_key");
                        std::string value = option.second.data();
                        config.set_deserialize(opt_key, value, config_substitutions);
                    }

                    config_ranges[{ min_z, max_z }].assign_config(std::move(config));
                }

                if (!config_ranges.empty())
                    m_layer_config_ranges.insert({ obj_idx, std::move(config_ranges) });
            }
        }
    }

    void _3MF_Importer::_extract_sla_support_points_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading sla support points data to buffer");
                return;
            }

            if (buffer.back() == '\n')
                buffer.pop_back();

            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);

            // Info on format versioning - see 3mf.hpp
            int version = 0;
            std::string key("support_points_format_version=");
            if (!objects.empty() && objects[0].find(key) != std::string::npos) {
                objects[0].erase(objects[0].begin(), objects[0].begin() + long(key.size())); // removes the string
                version = std::stoi(objects[0]);
                objects.erase(objects.begin()); // pop the header
            }

            for (const std::string& object : objects) {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);

                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }

                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }

                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToSlaSupportPointsMap::iterator object_item = m_sla_support_points.find(object_id);
                if (object_item != m_sla_support_points.end()) {
                    add_error("Found duplicated SLA support points");
                    continue;
                }

                std::vector<std::string> object_data_points;
                boost::split(object_data_points, object_data[1], boost::is_any_of(" "), boost::token_compress_off);

                std::vector<sla::SupportPoint> sla_support_points;

                if (version == 0) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=3)
                    sla_support_points.emplace_back(float(std::atof(object_data_points[i+0].c_str())),
                                                    float(std::atof(object_data_points[i+1].c_str())),
													float(std::atof(object_data_points[i+2].c_str())),
                                                    0.4f,
                                                    false);
                }
                if (version == 1) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=5)
                    sla_support_points.emplace_back(float(std::atof(object_data_points[i+0].c_str())),
                                                    float(std::atof(object_data_points[i+1].c_str())),
                                                    float(std::atof(object_data_points[i+2].c_str())),
                                                    float(std::atof(object_data_points[i+3].c_str())),
													//FIXME storing boolean as 0 / 1 and importing it as float.
                                                    std::abs(std::atof(object_data_points[i+4].c_str()) - 1.) < EPSILON);
                }

                if (!sla_support_points.empty())
                    m_sla_support_points.insert({ object_id, sla_support_points });
            }
        }
    }
    
    void _3MF_Importer::_extract_sla_drain_holes_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer(size_t(stat.m_uncomp_size), 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading sla support points data to buffer");
                return;
            }
            
            if (buffer.back() == '\n')
                buffer.pop_back();
            
            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);
            
            // Info on format versioning - see 3mf.hpp
            int version = 0;
            std::string key("drain_holes_format_version=");
            if (!objects.empty() && objects[0].find(key) != std::string::npos) {
                objects[0].erase(objects[0].begin(), objects[0].begin() + long(key.size())); // removes the string
                version = std::stoi(objects[0]);
                objects.erase(objects.begin()); // pop the header
            }
            
            for (const std::string& object : objects) {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);
                
                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }
                
                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }
                
                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }
                
                IdToSlaDrainHolesMap::iterator object_item = m_sla_drain_holes.find(object_id);
                if (object_item != m_sla_drain_holes.end()) {
                    add_error("Found duplicated SLA drain holes");
                    continue;
                }
                
                std::vector<std::string> object_data_points;
                boost::split(object_data_points, object_data[1], boost::is_any_of(" "), boost::token_compress_off);
                
                sla::DrainHoles sla_drain_holes;

                if (version == 1) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=8)
                        sla_drain_holes.emplace_back(Vec3f{float(std::atof(object_data_points[i+0].c_str())),
                                                      float(std::atof(object_data_points[i+1].c_str())),
                                                      float(std::atof(object_data_points[i+2].c_str()))},
                                                     Vec3f{float(std::atof(object_data_points[i+3].c_str())),
                                                      float(std::atof(object_data_points[i+4].c_str())),
                                                      float(std::atof(object_data_points[i+5].c_str()))},
                                                      float(std::atof(object_data_points[i+6].c_str())),
                                                      float(std::atof(object_data_points[i+7].c_str())));
                }

                // The holes are saved elevated above the mesh and deeper (bad idea indeed).
                // This is retained for compatibility.
                // Place the hole to the mesh and make it shallower to compensate.
                // The offset is 1 mm above the mesh.
                for (sla::DrainHole& hole : sla_drain_holes) {
                    hole.pos += hole.normal.normalized();
                    hole.height -= 1.f;
                }
                
                if (!sla_drain_holes.empty())
                    m_sla_drain_holes.insert({ object_id, sla_drain_holes });
            }
        }
    }

    void _3MF_Importer::_extract_embossed_svg_shape_file(const std::string &filename, mz_zip_archive &archive, const mz_zip_archive_file_stat &stat){
        assert(m_path_to_emboss_shape_files.find(filename) == m_path_to_emboss_shape_files.end());
        auto file = std::make_unique<std::string>(stat.m_uncomp_size, '\0');
        mz_bool res  = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void *) file->data(), stat.m_uncomp_size, 0);
        if (res == 0) {
            add_error("Error while reading svg shape for emboss");
            return;
        }
        
        // store for case svg is loaded before volume
        m_path_to_emboss_shape_files[filename] = std::move(file);
        
        // find embossed volume, for case svg is loaded after volume
        for (const ModelObject* object : m_model->objects)
        for (ModelVolume *volume : object->volumes) {
            std::optional<EmbossShape> &es = volume->emboss_shape;
            if (!es.has_value())
                continue;
            std::optional<EmbossShape::SvgFile> &svg = es->svg_file;
            if (!svg.has_value())
                continue;
            if (filename.compare(svg->path_in_3mf) == 0)
                svg->file_data = m_path_to_emboss_shape_files[filename];
        }
    }

    bool _3MF_Importer::_extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model)
    {
        if (stat.m_uncomp_size == 0) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _3MF_Importer::_handle_start_config_xml_element, _3MF_Importer::_handle_end_config_xml_element);

        void* parser_buffer = XML_GetBuffer(m_xml_parser, (int)stat.m_uncomp_size);
        if (parser_buffer == nullptr) {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, parser_buffer, (size_t)stat.m_uncomp_size, 0);
        if (res == 0) {
            add_error("Error while reading config data to buffer");
            return false;
        }

        if (!XML_ParseBuffer(m_xml_parser, (int)stat.m_uncomp_size, 1)) {
            char error_buf[1024];
            ::sprintf(error_buf, "Error (%s) while parsing xml file at line %d", XML_ErrorString(XML_GetErrorCode(m_xml_parser)), (int)XML_GetCurrentLineNumber(m_xml_parser));
            add_error(error_buf);
            return false;
        }

        return true;
    }

    void _3MF_Importer::_extract_custom_gcode_per_print_z_from_archive(::mz_zip_archive &archive, const mz_zip_archive_file_stat &stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading custom Gcodes per height data to buffer");
                return;
            }

            std::istringstream iss(buffer); // wrap returned xml to istringstream
            pt::ptree main_tree;
            pt::read_xml(iss, main_tree);

            if (main_tree.front().first != "custom_gcodes_per_print_z")
                return;
            pt::ptree code_tree = main_tree.front().second;

            m_model->custom_gcode_per_print_z.gcodes.clear();

            for (const auto& code : code_tree) {
                if (code.first == "mode") {
                    pt::ptree tree = code.second;
                    std::string mode = tree.get<std::string>("<xmlattr>.value");
                    m_model->custom_gcode_per_print_z.mode = mode == CustomGCode::SingleExtruderMode ? CustomGCode::Mode::SingleExtruder :
                                                             mode == CustomGCode::MultiAsSingleMode  ? CustomGCode::Mode::MultiAsSingle  :
                                                             CustomGCode::Mode::MultiExtruder;
                }
                if (code.first != "code")
                    continue;

                pt::ptree tree = code.second;
                double print_z          = tree.get<double>      ("<xmlattr>.print_z" );
                int extruder            = tree.get<int>         ("<xmlattr>.extruder");
                std::string color       = tree.get<std::string> ("<xmlattr>.color"   );

                CustomGCode::Type   type;
                std::string         extra;
                pt::ptree attr_tree = tree.find("<xmlattr>")->second;
                if (attr_tree.find("type") == attr_tree.not_found()) {
                    // It means that data was saved in old version (2.2.0 and older) of PrusaSlicer
                    // read old data ... 
                    std::string gcode       = tree.get<std::string> ("<xmlattr>.gcode");
                    // ... and interpret them to the new data
                    type  = gcode == "M600"           ? CustomGCode::ColorChange : 
                            gcode == "M601"           ? CustomGCode::PausePrint  :   
                            gcode == "tool_change"    ? CustomGCode::ToolChange  :   CustomGCode::Custom;
                    extra = type == CustomGCode::PausePrint ? color :
                            type == CustomGCode::Custom     ? gcode : "";
                }
                else {
                    type  = static_cast<CustomGCode::Type>(tree.get<int>("<xmlattr>.type"));
                    extra = tree.get<std::string>("<xmlattr>.extra");
                }
                m_model->custom_gcode_per_print_z.gcodes.push_back(CustomGCode::Item{print_z, type, extruder, color, extra}) ;
            }
        }
    }

    void XMLCALL _3MF_Importer::_handle_start_relationships_element(void *userData, const char *name, const char **attributes)
    {
        _3MF_Importer *importer = (_3MF_Importer *) userData;
        if (importer != nullptr)
            importer->_handle_start_relationships_element(name, attributes);
    }

    void _3MF_Importer::_handle_start_relationships_element(const char *name, const char **attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool         res            = true;
        unsigned int num_attributes = (unsigned int) XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(RELATIONSHIP_TAG, name) == 0)
            res = _handle_start_relationship(attributes, num_attributes);

        m_curr_characters.clear();
        if (!res)
            _stop_xml_parser();
    }

    bool _3MF_Importer::_handle_start_relationship(const char **attributes, unsigned int num_attributes)
    {
        std::string type = get_attribute_value_string(attributes, num_attributes, RELS_TYPE_ATTR);
        // only exactly that string type mean root model file
        if (type == "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel") {
            std::string path = get_attribute_value_string(attributes, num_attributes, TARGET_ATTR);
            m_start_part_path = path;
        }
        return true;
    }

    void _3MF_Importer::_handle_start_model_xml_element(const char *name, const char **attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_start_model(attributes, num_attributes);
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_start_resources(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_object(attributes, num_attributes);
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_start_mesh(attributes, num_attributes);
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_start_vertices(attributes, num_attributes);
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_start_vertex(attributes, num_attributes);
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_start_triangles(attributes, num_attributes);
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_start_triangle(attributes, num_attributes);
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_start_components(attributes, num_attributes);
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_start_component(attributes, num_attributes);
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_start_build(attributes, num_attributes);
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_start_item(attributes, num_attributes);
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_start_metadata(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _3MF_Importer::_handle_end_model_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_end_model();
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_end_resources();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_object();
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_end_mesh();
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_end_vertices();
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_end_vertex();
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_end_triangles();
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_end_triangle();
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_end_components();
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_end_component();
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_end_build();
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_end_item();
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_end_metadata();

        if (!res)
            _stop_xml_parser();
    }

    void _3MF_Importer::_handle_model_xml_characters(const XML_Char* s, int len)
    {
        m_curr_characters.append(s, len);
    }

    void _3MF_Importer::_handle_start_config_xml_element(const char* name, const char** attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_start_config(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_config_object(attributes, num_attributes);
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_start_config_volume(attributes, num_attributes);
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_start_config_volume_mesh(attributes, num_attributes);
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_start_config_metadata(attributes, num_attributes);
        else if (::strcmp(SHAPE_TAG, name) == 0)
            res = _handle_start_shape_configuration(attributes, num_attributes);
        else if (::strcmp(TEXT_TAG, name) == 0)
            res = _handle_start_text_configuration(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _3MF_Importer::_handle_end_config_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_end_config();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_config_object();
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_end_config_volume();
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_end_config_volume_mesh();
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_end_config_metadata();

        if (!res)
            _stop_xml_parser();
    }

    bool _3MF_Importer::_handle_start_model(const char** attributes, unsigned int num_attributes)
    {
        m_unit_factor = get_unit_factor(get_attribute_value_string(attributes, num_attributes, UNIT_ATTR));
        return true;
    }

    bool _3MF_Importer::_handle_end_model()
    {
        if (!m_model_path.empty())
            return true;

        // deletes all non-built or non-instanced objects
        for (const IdToModelObjectMap::value_type& object : m_objects) {
            if (object.second >= int(m_model->objects.size())) {
                add_error("Unable to find object");
                return false;
            }
            ModelObject *model_object = m_model->objects[object.second];
            if (model_object != nullptr && model_object->instances.size() == 0)
                m_model->delete_object(model_object);
        }

        if (m_version == 0) {
            // if the 3mf was not produced by PrusaSlicer and there is only one object,
            // set the object name to match the filename
            if (m_model->objects.size() == 1)
                m_model->objects.front()->name = m_name;
        }

        // applies instances' matrices
        for (Instance& instance : m_instances) {
            if (instance.instance != nullptr && instance.instance->get_object() != nullptr)
                // apply the transform to the instance
                _apply_transform(*instance.instance, instance.transform);
        }

        return true;
    }

    bool _3MF_Importer::_handle_start_resources(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_resources()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_object(const char** attributes, unsigned int num_attributes)
    {
        // reset current data
        m_curr_object.reset();

        if (is_valid_object_type(get_attribute_value_string(attributes, num_attributes, TYPE_ATTR))) {
            // create new object (it may be removed later if no instances are generated from it)
            m_curr_object.model_object_idx = (int)m_model->objects.size();
            m_curr_object.object = m_model->add_object();
            if (m_curr_object.object == nullptr) {
                add_error("Unable to create object");
                return false;
            }

            // set object data
            m_curr_object.object->name = get_attribute_value_string(attributes, num_attributes, NAME_ATTR);
            if (m_curr_object.object->name.empty())
                m_curr_object.object->name = m_name + "_" + std::to_string(m_model->objects.size());

            m_curr_object.id = get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        }

        return true;
    }

    bool _3MF_Importer::_handle_end_object()
    {
        if (m_curr_object.object != nullptr) {
            PathId object_id{m_model_path, m_curr_object.id};
            if (m_curr_object.geometry.empty()) {
                // no geometry defined
                // remove the object from the model
                m_model->delete_object(m_curr_object.object);

                if (m_curr_object.components.empty()) {
                    // no components defined -> invalid object, delete it
                    IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
                    if (object_item != m_objects.end())
                        m_objects.erase(object_item);

                    IdToAliasesMap::iterator alias_item = m_objects_aliases.find(object_id);
                    if (alias_item != m_objects_aliases.end())
                        m_objects_aliases.erase(alias_item);
                }
                else
                    // adds components to aliases
                    m_objects_aliases.insert({ object_id, m_curr_object.components });
            }
            else {
                // geometry defined, store it for later use
                m_geometries.insert({ object_id, std::move(m_curr_object.geometry) });

                // stores the object for later use
                if (m_objects.find(object_id) == m_objects.end()) {
                    m_objects.insert({ object_id, m_curr_object.model_object_idx });
                    m_objects_aliases.insert({object_id, {1, Component(object_id)}}); // aliases itself
                }
                else {
                    add_error("Found object with duplicate id");
                    return false;
                }
            }
        }

        return true;
    }

    bool _3MF_Importer::_handle_start_mesh(const char** attributes, unsigned int num_attributes)
    {
        // reset current geometry
        m_curr_object.geometry.reset();
        return true;
    }

    bool _3MF_Importer::_handle_end_mesh()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_vertices(const char** attributes, unsigned int num_attributes)
    {
        // reset current vertices
        m_curr_object.geometry.vertices.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_vertices()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_vertex(const char** attributes, unsigned int num_attributes)
    {
        // appends the vertex coordinates
        // missing values are set equal to ZERO
        m_curr_object.geometry.vertices.emplace_back(
            m_unit_factor * get_attribute_value_float(attributes, num_attributes, X_ATTR),
            m_unit_factor * get_attribute_value_float(attributes, num_attributes, Y_ATTR),
            m_unit_factor * get_attribute_value_float(attributes, num_attributes, Z_ATTR));
        return true;
    }

    bool _3MF_Importer::_handle_end_vertex()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_triangles(const char** attributes, unsigned int num_attributes)
    {
        // reset current triangles
        m_curr_object.geometry.triangles.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_triangles()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_triangle(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes:
        // p1
        // p2
        // p3
        // pid
        // see specifications

        // appends the triangle's vertices indices
        // missing values are set equal to ZERO
        m_curr_object.geometry.triangles.emplace_back(
            get_attribute_value_int(attributes, num_attributes, V1_ATTR),
            get_attribute_value_int(attributes, num_attributes, V2_ATTR),
            get_attribute_value_int(attributes, num_attributes, V3_ATTR));

        m_curr_object.geometry.custom_supports.push_back(get_attribute_value_string(attributes, num_attributes, CUSTOM_SUPPORTS_ATTR));
        m_curr_object.geometry.custom_seam.push_back(get_attribute_value_string(attributes, num_attributes, CUSTOM_SEAM_ATTR));

        // Now load MM segmentation data. Unfortunately, BambuStudio has changed the attribute name after they forked us,
        // leading to https://github.com/prusa3d/PrusaSlicer/issues/12502. Let's try to load both keys if the usual
        // one that PrusaSlicer uses is not present.
        std::string mm_segmentation_serialized = get_attribute_value_string(attributes, num_attributes, MM_SEGMENTATION_ATTR);
        if (mm_segmentation_serialized.empty())
            mm_segmentation_serialized = get_attribute_value_string(attributes, num_attributes, "paint_color");
        m_curr_object.geometry.mm_segmentation.push_back(mm_segmentation_serialized);

        return true;
    }

    bool _3MF_Importer::_handle_end_triangle()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_components(const char** attributes, unsigned int num_attributes)
    {
        // reset current components
        m_curr_object.components.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_components()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_component(const char** attributes, unsigned int num_attributes)
    {
        std::string path = get_attribute_value_string(attributes, num_attributes, PPATH_ATTR);
        if (path.empty()) path = m_model_path;
        
        int         object_id = get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Transform3d transform = get_transform_from_3mf_specs_string(get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));
        
        PathId path_id { path, object_id };
        IdToModelObjectMap::iterator object_item = m_objects.find(path_id);
        if (object_item == m_objects.end()) {
            IdToAliasesMap::iterator alias_item = m_objects_aliases.find(path_id);
            if (alias_item == m_objects_aliases.end()) {
                add_error("Found component with invalid object id");
                return false;
            }
        }

        m_curr_object.components.emplace_back(path_id, transform);

        return true;
    }

    bool _3MF_Importer::_handle_end_component()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_build(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_build()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_item(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes
        // thumbnail
        // partnumber
        // pid
        // pindex
        // see specifications

        int object_id = get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Transform3d transform = get_transform_from_3mf_specs_string(get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));
        std::string path = get_attribute_value_string(attributes, num_attributes, PPATH_ATTR);
        if (path.empty()) path = m_model_path;
        int printable = get_attribute_value_bool(attributes, num_attributes, PRINTABLE_ATTR);

        return _create_object_instance({path, object_id}, transform, printable, 1);
    }

    bool _3MF_Importer::_handle_end_item()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_metadata(const char** attributes, unsigned int num_attributes)
    {
        m_curr_characters.clear();

        std::string name = get_attribute_value_string(attributes, num_attributes, NAME_ATTR);
        if (!name.empty())
            m_curr_metadata_name = name;

        return true;
    }

    inline static void check_painting_version(unsigned int loaded_version, unsigned int highest_supported_version, const std::string &error_msg)
    {
        if (loaded_version > highest_supported_version)
            throw version_error(error_msg);
    }

    bool _3MF_Importer::_handle_end_metadata()
    {
        if (m_curr_metadata_name == SLIC3RPE_3MF_VERSION) {
            m_version = (unsigned int)atoi(m_curr_characters.c_str());
            if (m_check_version && (m_version > VERSION_3MF_COMPATIBLE)) {
                // std::string msg = _u8L("The selected 3mf file has been saved with a newer version of " + std::string(SLIC3R_APP_NAME) + " and is not compatible.");
                // throw version_error(msg.c_str());
                const std::string msg = (boost::format(_u8L("The selected 3mf file has been saved with a newer version of %1% and is not compatible.")) % std::string(SLIC3R_APP_NAME)).str();
                throw version_error(msg);
            }
        } else if (m_curr_metadata_name == "Application") {
            // Generator application of the 3MF.
            // SLIC3R_APP_KEY - SLIC3R_VERSION
            if (boost::starts_with(m_curr_characters, "PrusaSlicer-"))
                m_prusaslicer_generator_version = Semver::parse(m_curr_characters.substr(12));
        } else if (m_curr_metadata_name == SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION) {
            m_fdm_supports_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_fdm_supports_painting_version, FDM_SUPPORTS_PAINTING_VERSION,
                _u8L("The selected 3MF contains FDM supports painted object using a newer version of PrusaSlicer and is not compatible."));
        } else if (m_curr_metadata_name == SLIC3RPE_SEAM_PAINTING_VERSION) {
            m_seam_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_seam_painting_version, SEAM_PAINTING_VERSION,
                _u8L("The selected 3MF contains seam painted object using a newer version of PrusaSlicer and is not compatible."));
        } else if (m_curr_metadata_name == SLIC3RPE_MM_PAINTING_VERSION) {
            m_mm_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_mm_painting_version, MM_PAINTING_VERSION,
                _u8L("The selected 3MF contains multi-material painted object using a newer version of PrusaSlicer and is not compatible."));
        }

        return true;
    }

    struct TextConfigurationSerialization
    {
    public:
        TextConfigurationSerialization() = delete;
                
        using TypeToName = boost::bimap<EmbossStyle::Type, std::string_view>;
        static const TypeToName type_to_name;

        using HorizontalAlignToName = boost::bimap<FontProp::HorizontalAlign, std::string_view>;
        static const HorizontalAlignToName horizontal_align_to_name;

        using VerticalAlignToName = boost::bimap<FontProp::VerticalAlign, std::string_view>;
        static const VerticalAlignToName vertical_align_to_name;
        
        static EmbossStyle::Type get_type(std::string_view type) {
            const auto& to_type = TextConfigurationSerialization::type_to_name.right;
            auto type_item = to_type.find(type);
            assert(type_item != to_type.end());
            if (type_item == to_type.end()) return EmbossStyle::Type::undefined;
            return type_item->second;        
        }

        static std::string_view get_name(EmbossStyle::Type type) {
            const auto& to_name = TextConfigurationSerialization::type_to_name.left;
            auto type_name = to_name.find(type);
            assert(type_name != to_name.end());
            if (type_name == to_name.end()) return "unknown type";
            return type_name->second;
        }

        static void to_xml(std::stringstream &stream, const TextConfiguration &tc);
        static std::optional<TextConfiguration> read(const char **attributes, unsigned int num_attributes);
        static EmbossShape read_old(const char **attributes, unsigned int num_attributes);
    };

    bool _3MF_Importer::_handle_start_text_configuration(const char **attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Can not assign volume mesh to a valid object");
            return false;
        }
        if (object->second.volumes.empty()) {
            add_error("Can not assign mesh to a valid volume");
            return false;
        }
        ObjectMetadata::VolumeMetadata& volume = object->second.volumes.back();
        volume.text_configuration = TextConfigurationSerialization::read(attributes, num_attributes);
        if (!volume.text_configuration.has_value())
            return false;

        // Is 3mf version with shapes?
        if (volume.shape_configuration.has_value())
            return true;

        // Back compatibility for 3mf version without shapes
        volume.shape_configuration = TextConfigurationSerialization::read_old(attributes, num_attributes);
        return true;
    }

    // Definition of read/write method for EmbossShape
    static void to_xml(std::stringstream &stream, const EmbossShape &es, const ModelVolume &volume, mz_zip_archive &archive);
    static std::optional<EmbossShape> read_emboss_shape(const char **attributes, unsigned int num_attributes);

    bool _3MF_Importer::_handle_start_shape_configuration(const char **attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Can not assign volume mesh to a valid object");
            return false;
        }
        auto &volumes = object->second.volumes;
        if (volumes.empty()) {
            add_error("Can not assign mesh to a valid volume");
            return false;
        }
        ObjectMetadata::VolumeMetadata &volume = volumes.back();
        volume.shape_configuration = read_emboss_shape(attributes, num_attributes);
        if (!volume.shape_configuration.has_value())
            return false;

        // Fill svg file content into shape_configuration
        std::optional<EmbossShape::SvgFile> &svg = volume.shape_configuration->svg_file;
        if (!svg.has_value())
            return true; // do not contain svg file

        const std::string &path = svg->path_in_3mf;
        if (path.empty()) 
            return true; // do not contain svg file

        auto it = m_path_to_emboss_shape_files.find(path);
        if (it == m_path_to_emboss_shape_files.end())
            return true; // svg file is not loaded yet

        svg->file_data = it->second;
        return true;
    }

    bool _3MF_Importer::_create_object_instance(PathId object_id, const Transform3d& transform, const bool printable, unsigned int recur_counter)
    {
        static const unsigned int MAX_RECURSIONS = 10;

        // escape from circular aliasing
        if (recur_counter > MAX_RECURSIONS) {
            add_error("Too many recursions");
            return false;
        }

        IdToAliasesMap::iterator it = m_objects_aliases.find(object_id);
        if (it == m_objects_aliases.end()) {
            add_error("Found item with invalid object id");
            return false;
        }

        if (it->second.size() == 1 && it->second[0].object_id == object_id) {
            // aliasing to itself

            IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
            if (object_item == m_objects.end() || object_item->second == -1) {
                add_error("Found invalid object");
                return false;
            }
            else {
                ModelInstance* instance = m_model->objects[object_item->second]->add_instance();
                if (instance == nullptr) {
                    add_error("Unable to add object instance");
                    return false;
                }
                instance->printable = printable;

                m_instances.emplace_back(instance, transform);
            }
        }
        else {
            // recursively process nested components
            for (const Component& component : it->second) {
                if (!_create_object_instance(component.object_id, transform * component.transform, printable, recur_counter + 1))
                    return false;
            }
        }

        return true;
    }

    void _3MF_Importer::_apply_transform(ModelInstance& instance, const Transform3d& transform)
    {
        Slic3r::Geometry::Transformation t(transform);
        // invalid scale value, return
        if (!t.get_scaling_factor().all())
            return;

        instance.set_transformation(t);
    }

    bool _3MF_Importer::_handle_start_config(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_config()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_object(const char** attributes, unsigned int num_attributes)
    {
        int object_id = get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        IdToMetadataMap::iterator object_item = m_objects_metadata.find(object_id);
        if (object_item != m_objects_metadata.end()) {
            add_error("Found duplicated object id");
            return false;
        }

        // Added because of github #3435, currently not used by PrusaSlicer
        // int instances_count_id = get_attribute_value_int(attributes, num_attributes, INSTANCESCOUNT_ATTR);

        m_objects_metadata.insert({ object_id, ObjectMetadata() });
        m_curr_config.object_id = object_id;
        return true;
    }

    bool _3MF_Importer::_handle_end_config_object()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_volume(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Cannot assign volume to a valid object");
            return false;
        }

        m_curr_config.volume_id = (int)object->second.volumes.size();

        unsigned int first_triangle_id = (unsigned int)get_attribute_value_int(attributes, num_attributes, FIRST_TRIANGLE_ID_ATTR);
        unsigned int last_triangle_id = (unsigned int)get_attribute_value_int(attributes, num_attributes, LAST_TRIANGLE_ID_ATTR);

        object->second.volumes.emplace_back(first_triangle_id, last_triangle_id);
        return true;
    }

    bool _3MF_Importer::_handle_start_config_volume_mesh(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Cannot assign volume mesh to a valid object");
            return false;
        }
        if (object->second.volumes.empty()) {
            add_error("Cannot assign mesh to a valid volume");
            return false;
        }

        ObjectMetadata::VolumeMetadata& volume = object->second.volumes.back();

        int edges_fixed         = get_attribute_value_int(attributes, num_attributes, MESH_STAT_EDGES_FIXED       );
        int degenerate_facets   = get_attribute_value_int(attributes, num_attributes, MESH_STAT_DEGENERATED_FACETS);
        int facets_removed      = get_attribute_value_int(attributes, num_attributes, MESH_STAT_FACETS_REMOVED    );
        int facets_reversed     = get_attribute_value_int(attributes, num_attributes, MESH_STAT_FACETS_RESERVED   );
        int backwards_edges     = get_attribute_value_int(attributes, num_attributes, MESH_STAT_BACKWARDS_EDGES   );

        volume.mesh_stats = { edges_fixed, degenerate_facets, facets_removed, facets_reversed, backwards_edges };

        return true;
    }

    bool _3MF_Importer::_handle_end_config_volume()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_config_volume_mesh()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_metadata(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Cannot assign metadata to valid object id");
            return false;
        }

        std::string type = get_attribute_value_string(attributes, num_attributes, TYPE_ATTR);
        std::string key = get_attribute_value_string(attributes, num_attributes, KEY_ATTR);
        std::string value = get_attribute_value_string(attributes, num_attributes, VALUE_ATTR);

        if (type == OBJECT_TYPE)
            object->second.metadata.emplace_back(key, value);
        else if (type == VOLUME_TYPE) {
            if (size_t(m_curr_config.volume_id) < object->second.volumes.size())
                object->second.volumes[m_curr_config.volume_id].metadata.emplace_back(key, value);
        }
        else {
            add_error("Found invalid metadata type");
            return false;
        }

        return true;
    }

    bool _3MF_Importer::_handle_end_config_metadata()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes, ConfigSubstitutionContext& config_substitutions)
    {
        if (!object.volumes.empty()) {
            add_error("Found invalid volumes count");
            return false;
        }

        unsigned int geo_tri_count = (unsigned int)geometry.triangles.size();
        unsigned int renamed_volumes_count = 0;

        for (const ObjectMetadata::VolumeMetadata& volume_data : volumes) {
            if (geo_tri_count <= volume_data.first_triangle_id || geo_tri_count <= volume_data.last_triangle_id || volume_data.last_triangle_id < volume_data.first_triangle_id) {
                add_error("Found invalid triangle id");
                return false;
            }

            Transform3d volume_matrix_to_object = Transform3d::Identity();
            bool        has_transform 		    = false;
            // extract the volume transformation from the volume's metadata, if present
            for (const Metadata& metadata : volume_data.metadata) {
                if (metadata.key == MATRIX_KEY) {
                    volume_matrix_to_object = Slic3r::Geometry::transform3d_from_string(metadata.value);
                    has_transform 			= ! volume_matrix_to_object.isApprox(Transform3d::Identity(), 1e-10);
                    break;
                }
            }

            // splits volume out of imported geometry
            indexed_triangle_set its;
            its.indices.assign(geometry.triangles.begin() + volume_data.first_triangle_id, geometry.triangles.begin() + volume_data.last_triangle_id + 1);
            const size_t triangles_count = its.indices.size();
            if (triangles_count == 0) {
                add_error("An empty triangle mesh found");
                return false;
            }

            {
                int min_id = its.indices.front()[0];
                int max_id = min_id;
                for (const Vec3i& face : its.indices) {
                    for (const int tri_id : face) {
                        if (tri_id < 0 || tri_id >= int(geometry.vertices.size())) {
                            add_error("Found invalid vertex id");
                            return false;
                        }
                        min_id = std::min(min_id, tri_id);
                        max_id = std::max(max_id, tri_id);
                    }
                }
                its.vertices.assign(geometry.vertices.begin() + min_id, geometry.vertices.begin() + max_id + 1);

                // rebase indices to the current vertices list
                for (Vec3i& face : its.indices)
                    for (int& tri_id : face)
                        tri_id -= min_id;
            }

            if (m_prusaslicer_generator_version && 
                *m_prusaslicer_generator_version >= *Semver::parse("2.4.0-alpha1") &&
                *m_prusaslicer_generator_version < *Semver::parse("2.4.0-alpha3"))
                // PrusaSlicer 2.4.0-alpha2 contained a bug, where all vertices of a single object were saved for each volume the object contained.
                // Remove the vertices, that are not referenced by any face.
                its_compactify_vertices(its, true);

            TriangleMesh triangle_mesh(std::move(its), volume_data.mesh_stats);

            if (m_version == 0) {
                // if the 3mf was not produced by PrusaSlicer and there is only one instance,
                // bake the transformation into the geometry to allow the reload from disk command
                // to work properly
                if (object.instances.size() == 1) {
                    triangle_mesh.transform(object.instances.front()->get_transformation().get_matrix(), false);
                    object.instances.front()->set_transformation(Slic3r::Geometry::Transformation());
                    //FIXME do the mesh fixing?
                }
            }
            if (triangle_mesh.volume() < 0)
                triangle_mesh.flip_triangles();

			ModelVolume* volume = object.add_volume(std::move(triangle_mesh));
            // stores the volume matrix taken from the metadata, if present
            if (has_transform)
                volume->source.transform = Slic3r::Geometry::Transformation(volume_matrix_to_object);

            // recreate custom supports, seam and mm segmentation from previously loaded attribute
            volume->supported_facets.reserve(triangles_count);
            volume->seam_facets.reserve(triangles_count);
            volume->mm_segmentation_facets.reserve(triangles_count);
            for (size_t i=0; i<triangles_count; ++i) {
                size_t index = volume_data.first_triangle_id + i;
                assert(index < geometry.custom_supports.size());
                assert(index < geometry.custom_seam.size());
                assert(index < geometry.mm_segmentation.size());
                if (! geometry.custom_supports[index].empty())
                    volume->supported_facets.set_triangle_from_string(i, geometry.custom_supports[index]);
                if (! geometry.custom_seam[index].empty())
                    volume->seam_facets.set_triangle_from_string(i, geometry.custom_seam[index]);
                if (! geometry.mm_segmentation[index].empty())
                    volume->mm_segmentation_facets.set_triangle_from_string(i, geometry.mm_segmentation[index]);
            }
            volume->supported_facets.shrink_to_fit();
            volume->seam_facets.shrink_to_fit();
            volume->mm_segmentation_facets.shrink_to_fit();

            if (auto &es = volume_data.shape_configuration; es.has_value())
                volume->emboss_shape = std::move(es);            
            if (auto &tc = volume_data.text_configuration; tc.has_value())
                volume->text_configuration = std::move(tc);
            
            // apply the remaining volume's metadata
            for (const Metadata& metadata : volume_data.metadata) {
                if (metadata.key == NAME_KEY)
                    volume->name = metadata.value;
                else if ((metadata.key == MODIFIER_KEY) && (metadata.value == "1"))
					volume->set_type(ModelVolumeType::PARAMETER_MODIFIER);
                else if (metadata.key == VOLUME_TYPE_KEY)
                    volume->set_type(ModelVolume::type_from_string(metadata.value));
                else if (metadata.key == SOURCE_FILE_KEY)
                    volume->source.input_file = metadata.value;
                else if (metadata.key == SOURCE_OBJECT_ID_KEY)
                    volume->source.object_idx = ::atoi(metadata.value.c_str());
                else if (metadata.key == SOURCE_VOLUME_ID_KEY)
                    volume->source.volume_idx = ::atoi(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_X_KEY)
                    volume->source.mesh_offset.x() = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_Y_KEY)
                    volume->source.mesh_offset.y() = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_Z_KEY)
                    volume->source.mesh_offset.z() = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_IN_INCHES_KEY)
                    volume->source.is_converted_from_inches = metadata.value == "1";
                else if (metadata.key == SOURCE_IN_METERS_KEY)
                    volume->source.is_converted_from_meters = metadata.value == "1";
                else if (metadata.key == SOURCE_IS_BUILTIN_VOLUME_KEY)
                    volume->source.is_from_builtin_objects = metadata.value == "1";
                else
                    volume->config.set_deserialize(metadata.key, metadata.value, config_substitutions);
            }

            // this may happen for 3mf saved by 3rd part softwares
            if (volume->name.empty()) {
                volume->name = object.name;
                if (renamed_volumes_count > 0)
                    volume->name += "_" + std::to_string(renamed_volumes_count + 1);
                ++renamed_volumes_count;
            }
        }

        return true;
    }

    void XMLCALL _3MF_Importer::_handle_start_model_xml_element(void* userData, const char* name, const char** attributes)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_model_xml_element(name, attributes);
    }

    void XMLCALL _3MF_Importer::_handle_end_model_xml_element(void* userData, const char* name)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_model_xml_element(name);
    }

    void XMLCALL _3MF_Importer::_handle_model_xml_characters(void* userData, const XML_Char* s, int len)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_model_xml_characters(s, len);
    }

    void XMLCALL _3MF_Importer::_handle_start_config_xml_element(void* userData, const char* name, const char** attributes)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_config_xml_element(name, attributes);
    }
    
    void XMLCALL _3MF_Importer::_handle_end_config_xml_element(void* userData, const char* name)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_config_xml_element(name);
    }

    class _3MF_Exporter : public _3MF_Base
    {
        struct BuildItem
        {
            unsigned int id;
            Transform3d transform;
            bool printable;

            BuildItem(unsigned int id, const Transform3d& transform, const bool printable)
                : id(id)
                , transform(transform)
                , printable(printable)
            {
            }
        };

        struct Offsets
        {
            unsigned int first_vertex_id;
            unsigned int first_triangle_id;
            unsigned int last_triangle_id;

            Offsets(unsigned int first_vertex_id)
                : first_vertex_id(first_vertex_id)
                , first_triangle_id(-1)
                , last_triangle_id(-1)
            {
            }
        };

        typedef std::map<const ModelVolume*, Offsets> VolumeToOffsetsMap;

        struct ObjectData
        {
            ModelObject* object;
            VolumeToOffsetsMap volumes_offsets;

            explicit ObjectData(ModelObject* object)
                : object(object)
            {
            }
        };

        typedef std::vector<BuildItem> BuildItemsList;
        typedef std::map<int, ObjectData> IdToObjectDataMap;

        bool m_fullpath_sources{ true };
        bool m_zip64 { true };

    public:
        bool save_model_to_file(const std::string& filename, Model& model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data, bool zip64);
        static void add_transformation(std::stringstream &stream, const Transform3d &tr);
    private:
        void _publish(Model &model);
        bool _save_model_to_file(const std::string& filename, Model& model, const DynamicPrintConfig* config, const ThumbnailData* thumbnail_data);
        bool _add_content_types_file_to_archive(mz_zip_archive& archive);
        bool _add_thumbnail_file_to_archive(mz_zip_archive& archive, const ThumbnailData& thumbnail_data);
        bool _add_relationships_file_to_archive(mz_zip_archive& archive);
        bool _add_model_file_to_archive(const std::string& filename, mz_zip_archive& archive, const Model& model, IdToObjectDataMap& objects_data);
        bool _add_object_to_model_stream(mz_zip_writer_staged_context &context, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets);
        bool _add_mesh_to_object_stream(mz_zip_writer_staged_context &context, ModelObject& object, VolumeToOffsetsMap& volumes_offsets);        
        bool _add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items);
        bool _add_cut_information_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_layer_height_profile_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_layer_config_ranges_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_sla_support_points_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_sla_drain_holes_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_print_config_file_to_archive(mz_zip_archive& archive, const DynamicPrintConfig &config);
        bool _add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model, const IdToObjectDataMap &objects_data);
        bool _add_custom_gcode_per_print_z_file_to_archive(mz_zip_archive& archive, Model& model, const DynamicPrintConfig* config);
    };

    bool _3MF_Exporter::save_model_to_file(const std::string& filename, Model& model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data, bool zip64)
    {
        clear_errors();
        m_fullpath_sources = fullpath_sources;
        m_zip64 = zip64;
        return _save_model_to_file(filename, model, config, thumbnail_data);
    }

    bool _3MF_Exporter::_save_model_to_file(const std::string& filename, Model& model, const DynamicPrintConfig* config, const ThumbnailData* thumbnail_data)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        if (!open_zip_writer(&archive, filename)) {
            add_error("Unable to open the file");
            return false;
        }

        // Adds content types file ("[Content_Types].xml";).
        // The content of this file is the same for each PrusaSlicer 3mf.
        if (!_add_content_types_file_to_archive(archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        if (thumbnail_data != nullptr && thumbnail_data->is_valid()) {
            // Adds the file Metadata/thumbnail.png.
            if (!_add_thumbnail_file_to_archive(archive, *thumbnail_data)) {
                close_zip_writer(&archive);
                boost::filesystem::remove(filename);
                return false;
            }
        }

        // Adds relationships file ("_rels/.rels"). 
        // The content of this file is the same for each PrusaSlicer 3mf.
        // The relationshis file contains a reference to the geometry file "3D/3dmodel.model", the name was chosen to be compatible with CURA.
        if (!_add_relationships_file_to_archive(archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds model file ("3D/3dmodel.model").
        // This is the one and only file that contains all the geometry (vertices and triangles) of all ModelVolumes.
        IdToObjectDataMap objects_data;
        if (!_add_model_file_to_archive(filename, archive, model, objects_data)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds file with information for object cut ("Metadata/Slic3r_PE_cut_information.txt").
        // All information for object cut of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_cut_information_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds layer height profile file ("Metadata/Slic3r_PE_layer_heights_profile.txt").
        // All layer height profiles of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_layer_height_profile_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds layer config ranges file ("Metadata/Slic3r_PE_layer_config_ranges.txt").
        // All layer height profiles of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_layer_config_ranges_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds sla support points file ("Metadata/Slic3r_PE_sla_support_points.txt").
        // All  sla support points of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_sla_support_points_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }
        
        if (!_add_sla_drain_holes_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }
        

        // Adds custom gcode per height file ("Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml").
        // All custom gcode per height of whole Model are stored here
        if (!_add_custom_gcode_per_print_z_file_to_archive(archive, model, config)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds slic3r print config file ("Metadata/Slic3r_PE.config").
        // This file contains the content of FullPrintConfing / SLAFullPrintConfig.
        if (config != nullptr) {
            if (!_add_print_config_file_to_archive(archive, *config)) {
                close_zip_writer(&archive);
                boost::filesystem::remove(filename);
                return false;
            }
        }

        // Adds slic3r model config file ("Metadata/Slic3r_PE_model.config").
        // This file contains all the attributes of all ModelObjects and their ModelVolumes (names, parameter overrides).
        // As there is just a single Indexed Triangle Set data stored per ModelObject, offsets of volumes into their respective Indexed Triangle Set data
        // is stored here as well.
        if (!_add_model_config_file_to_archive(archive, model, objects_data)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        if (!mz_zip_writer_finalize_archive(&archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            add_error("Unable to finalize the archive");
            return false;
        }

        close_zip_writer(&archive);

        return true;
    }

    bool _3MF_Exporter::_add_content_types_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
        stream << " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n";
        stream << " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n";
        stream << " <Default Extension=\"png\" ContentType=\"image/png\"/>\n";
        stream << "</Types>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, CONTENT_TYPES_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add content types file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_thumbnail_file_to_archive(mz_zip_archive& archive, const ThumbnailData& thumbnail_data)
    {
        bool res = false;

        size_t png_size = 0;
        void* png_data = tdefl_write_image_to_png_file_in_memory_ex((const void*)thumbnail_data.pixels.data(), thumbnail_data.width, thumbnail_data.height, 4, &png_size, MZ_DEFAULT_LEVEL, 1);
        if (png_data != nullptr) {
            res = mz_zip_writer_add_mem(&archive, THUMBNAIL_FILE.c_str(), (const void*)png_data, png_size, MZ_DEFAULT_COMPRESSION);
            mz_free(png_data);
        }

        if (!res)
            add_error("Unable to add thumbnail file to archive");

        return res;
    }

    bool _3MF_Exporter::_add_relationships_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
        stream << " <Relationship Target=\"/" << MODEL_FILE << "\" Id=\"rel-1\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n";
        stream << " <Relationship Target=\"/" << THUMBNAIL_FILE << "\" Id=\"rel-2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\"/>\n";
        stream << "</Relationships>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, RELATIONSHIPS_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add relationships file to archive");
            return false;
        }

        return true;
    }

    static void reset_stream(std::stringstream &stream)
    {
        stream.str("");
        stream.clear();
        // https://en.cppreference.com/w/cpp/types/numeric_limits/max_digits10
        // Conversion of a floating-point value to text and back is exact as long as at least max_digits10 were used (9 for float, 17 for double).
        // It is guaranteed to produce the same floating-point value, even though the intermediate text representation is not exact.
        // The default value of std::stream precision is 6 digits only!
        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    }

    bool _3MF_Exporter::_add_model_file_to_archive(const std::string& filename, mz_zip_archive& archive, const Model& model, IdToObjectDataMap& objects_data)
    {
        mz_zip_writer_staged_context context;
        if (!mz_zip_writer_add_staged_open(&archive, &context, MODEL_FILE.c_str(), 
            m_zip64 ? 
                // Maximum expected and allowed 3MF file size is 16GiB.
                // This switches the ZIP file to a 64bit mode, which adds a tiny bit of overhead to file records.
                (uint64_t(1) << 30) * 16 : 
                // Maximum expected 3MF file size is 4GB-1. This is a workaround for interoperability with Windows 10 3D model fixing API, see
                // GH issue #6193.
                (uint64_t(1) << 32) - 1,
            nullptr, nullptr, 0, MZ_DEFAULT_COMPRESSION, nullptr, 0, nullptr, 0)) {
            add_error("Unable to add model file to archive");
            return false;
        }

        {
            std::stringstream stream;
            reset_stream(stream);
            stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            stream << "<" << MODEL_TAG << " unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" xmlns:slic3rpe=\"http://schemas.slic3r.org/3mf/2017/06\">\n";
            stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_3MF_VERSION << "\">" << VERSION_3MF << "</" << METADATA_TAG << ">\n";

            if (model.is_fdm_support_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION << "\">" << FDM_SUPPORTS_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            if (model.is_seam_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_SEAM_PAINTING_VERSION << "\">" << SEAM_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            if (model.is_mm_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_MM_PAINTING_VERSION << "\">" << MM_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            std::string name = xml_escape(boost::filesystem::path(filename).stem().string());
            stream << " <" << METADATA_TAG << " name=\"Title\">" << name << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Designer\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Description\">" << name << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Copyright\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"LicenseTerms\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Rating\">" << "</" << METADATA_TAG << ">\n";
            std::string date = Slic3r::Utils::utc_timestamp(Slic3r::Utils::get_current_time_utc());
            // keep only the date part of the string
            date = date.substr(0, 10);
            stream << " <" << METADATA_TAG << " name=\"CreationDate\">" << date << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"ModificationDate\">" << date << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Application\">" << SLIC3R_APP_KEY << "-" << SLIC3R_VERSION << "</" << METADATA_TAG << ">\n";
            stream << " <" << RESOURCES_TAG << ">\n";
            std::string buf = stream.str();
            if (! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) {
                add_error("Unable to add model file to archive");
                return false;
            }
        }

        // Instance transformations, indexed by the 3MF object ID (which is a linear serialization of all instances of all ModelObjects).
        BuildItemsList build_items;

        // The object_id here is a one based identifier of the first instance of a ModelObject in the 3MF file, where
        // all the object instances of all ModelObjects are stored and indexed in a 1 based linear fashion.
        // Therefore the list of object_ids here may not be continuous.
        unsigned int object_id = 1;
        for (ModelObject* obj : model.objects) {
            if (obj == nullptr)
                continue;

            // Index of an object in the 3MF file corresponding to the 1st instance of a ModelObject.
            unsigned int curr_id = object_id;
            IdToObjectDataMap::iterator object_it = objects_data.insert({ curr_id, ObjectData(obj) }).first;
            // Store geometry of all ModelVolumes contained in a single ModelObject into a single 3MF indexed triangle set object.
            // object_it->second.volumes_offsets will contain the offsets of the ModelVolumes in that single indexed triangle set.
            // object_id will be increased to point to the 1st instance of the next ModelObject.
            if (!_add_object_to_model_stream(context, object_id, *obj, build_items, object_it->second.volumes_offsets)) {
                add_error("Unable to add object to archive");
                mz_zip_writer_add_staged_finish(&context);
                return false;
            }
        }

        {
            std::stringstream stream;
            reset_stream(stream);
            stream << " </" << RESOURCES_TAG << ">\n";

            // Store the transformations of all the ModelInstances of all ModelObjects, indexed in a linear fashion.
            if (!_add_build_to_model_stream(stream, build_items)) {
                add_error("Unable to add build to archive");
                mz_zip_writer_add_staged_finish(&context);
                return false;
            }

            stream << "</" << MODEL_TAG << ">\n";
           
            std::string buf = stream.str();

            if ((! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) ||
                ! mz_zip_writer_add_staged_finish(&context)) {
                add_error("Unable to add model file to archive");
                return false;
            }
        }

        return true;
    }

    bool _3MF_Exporter::_add_object_to_model_stream(mz_zip_writer_staged_context &context, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets)
    {
        std::stringstream stream;
        reset_stream(stream);
        unsigned int id = 0;
        for (const ModelInstance* instance : object.instances) {
			assert(instance != nullptr);
            if (instance == nullptr)
                continue;

            unsigned int instance_id = object_id + id;
            stream << "  <" << OBJECT_TAG << " id=\"" << instance_id << "\" type=\"model\">\n";

            if (id == 0) {
                std::string buf = stream.str();
                reset_stream(stream);
                if ((! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) ||
                    ! _add_mesh_to_object_stream(context, object, volumes_offsets)) {
                    add_error("Unable to add mesh to archive");
                    return false;
                }
            }
            else {
                stream << "   <" << COMPONENTS_TAG << ">\n";
                stream << "    <" << COMPONENT_TAG << " objectid=\"" << object_id << "\"/>\n";
                stream << "   </" << COMPONENTS_TAG << ">\n";
            }

            Transform3d t = instance->get_matrix();
            // instance_id is just a 1 indexed index in build_items.
            assert(instance_id == build_items.size() + 1);
            build_items.emplace_back(instance_id, t, instance->printable);

            stream << "  </" << OBJECT_TAG << ">\n";

            ++id;
        }

        object_id += id;
        std::string buf = stream.str();
        return buf.empty() || mz_zip_writer_add_staged_data(&context, buf.data(), buf.size());
    }

#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
    template <typename Num>
    struct coordinate_policy_fixed : boost::spirit::karma::real_policies<Num>
    {
        static int floatfield(Num n) { return fmtflags::fixed; }
        // Number of decimal digits to maintain float accuracy when storing into a text file and parsing back.
        static unsigned precision(Num /* n */) { return std::numeric_limits<Num>::max_digits10 + 1; }
        // No trailing zeros, thus for fmtflags::fixed usually much less than max_digits10 decimal numbers will be produced.
        static bool trailing_zeros(Num /* n */) { return false; }
    };
    template <typename Num>
    struct coordinate_policy_scientific : coordinate_policy_fixed<Num>
    {
        static int floatfield(Num n) { return fmtflags::scientific; }
    };
    // Define a new generator type based on the new coordinate policy.
    using coordinate_type_fixed      = boost::spirit::karma::real_generator<float, coordinate_policy_fixed<float>>;
    using coordinate_type_scientific = boost::spirit::karma::real_generator<float, coordinate_policy_scientific<float>>;
#endif // EXPORT_3MF_USE_SPIRIT_KARMA_FP

    bool _3MF_Exporter::_add_mesh_to_object_stream(mz_zip_writer_staged_context &context, ModelObject& object, VolumeToOffsetsMap& volumes_offsets)
    {
        std::string output_buffer;
        output_buffer += "   <";
        output_buffer += MESH_TAG;
        output_buffer += ">\n    <";
        output_buffer += VERTICES_TAG;
        output_buffer += ">\n";

        auto flush = [this, &output_buffer, &context](bool force = false) {
            if ((force && ! output_buffer.empty()) || output_buffer.size() >= 65536 * 16) {
                if (! mz_zip_writer_add_staged_data(&context, output_buffer.data(), output_buffer.size())) {
                    add_error("Error during writing or compression");
                    return false;
                }
                output_buffer.clear();
            }
            return true;
        };

        auto format_coordinate = [](float f, char *buf) -> char* {
            assert(is_decimal_separator_point());
#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
            // Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point formatter,
            // https://github.com/boostorg/spirit/pull/586
            // where the exported string is one digit shorter than it should be to guarantee lossless round trip.
            // The code is left here for the ocasion boost guys improve.
            coordinate_type_fixed      const coordinate_fixed      = coordinate_type_fixed();
            coordinate_type_scientific const coordinate_scientific = coordinate_type_scientific();
            // Format "f" in a fixed format.
            char *ptr = buf;
            boost::spirit::karma::generate(ptr, coordinate_fixed, f);
            // Format "f" in a scientific format.
            char *ptr2 = ptr;
            boost::spirit::karma::generate(ptr2, coordinate_scientific, f);
            // Return end of the shorter string.
            auto len2 = ptr2 - ptr;
            if (ptr - buf > len2) {
                // Move the shorter scientific form to the front.
                memcpy(buf, ptr, len2);
                ptr = buf + len2;
            }
            // Return pointer to the end.
            return ptr;
#else
            // Round-trippable float, shortest possible.
            return buf + sprintf(buf, "%.9g", f);
#endif
        };

        char buf[256];
        unsigned int vertices_count = 0;
        for (ModelVolume* volume : object.volumes) {
            if (volume == nullptr)
                continue;

            volumes_offsets.insert({ volume, Offsets(vertices_count) });

            const indexed_triangle_set &its = volume->mesh().its;
            if (its.vertices.empty()) {
                add_error("Found invalid mesh");
                return false;
            }

            vertices_count += (int)its.vertices.size();

            const Transform3d& matrix = volume->get_matrix();
            for (const auto& vertex: its.vertices) {
                Vec3f v = (matrix * vertex.cast<double>()).cast<float>();
                char *ptr = buf;
                boost::spirit::karma::generate(ptr, boost::spirit::lit("     <") << VERTEX_TAG << " x=\"");
                ptr = format_coordinate(v.x(), ptr);
                boost::spirit::karma::generate(ptr, "\" y=\"");
                ptr = format_coordinate(v.y(), ptr);
                boost::spirit::karma::generate(ptr, "\" z=\"");
                ptr = format_coordinate(v.z(), ptr);
                boost::spirit::karma::generate(ptr, "\"/>\n");
                *ptr = '\0';
                output_buffer += buf;
                if (! flush())
                    return false;
            }
        }

        output_buffer += "    </";
        output_buffer += VERTICES_TAG;
        output_buffer += ">\n    <";
        output_buffer += TRIANGLES_TAG;
        output_buffer += ">\n";

        unsigned int triangles_count = 0;
        for (ModelVolume* volume : object.volumes) {
            if (volume == nullptr)
                continue;

            bool is_left_handed = volume->is_left_handed();
            VolumeToOffsetsMap::iterator volume_it = volumes_offsets.find(volume);
            assert(volume_it != volumes_offsets.end());

            const indexed_triangle_set &its = volume->mesh().its;

            // updates triangle offsets
            volume_it->second.first_triangle_id = triangles_count;
            triangles_count += (int)its.indices.size();
            volume_it->second.last_triangle_id = triangles_count - 1;

            for (int i = 0; i < int(its.indices.size()); ++ i) {
                {
                    const Vec3i &idx = its.indices[i];
                    char *ptr = buf;
                    boost::spirit::karma::generate(ptr, boost::spirit::lit("     <") << TRIANGLE_TAG <<
                        " v1=\"" << boost::spirit::int_ <<
                        "\" v2=\"" << boost::spirit::int_ <<
                        "\" v3=\"" << boost::spirit::int_ << "\"",
                        idx[is_left_handed ? 2 : 0] + volume_it->second.first_vertex_id,
                        idx[1] + volume_it->second.first_vertex_id,
                        idx[is_left_handed ? 0 : 2] + volume_it->second.first_vertex_id);
                    *ptr = '\0';
                    output_buffer += buf;
                }

                std::string custom_supports_data_string = volume->supported_facets.get_triangle_as_string(i);
                if (! custom_supports_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += CUSTOM_SUPPORTS_ATTR;
                    output_buffer += "=\"";
                    output_buffer += custom_supports_data_string;
                    output_buffer += "\"";
                }

                std::string custom_seam_data_string = volume->seam_facets.get_triangle_as_string(i);
                if (! custom_seam_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += CUSTOM_SEAM_ATTR;
                    output_buffer += "=\"";
                    output_buffer += custom_seam_data_string;
                    output_buffer += "\"";
                }

                std::string mm_painting_data_string = volume->mm_segmentation_facets.get_triangle_as_string(i);
                if (! mm_painting_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += MM_SEGMENTATION_ATTR;
                    output_buffer += "=\"";
                    output_buffer += mm_painting_data_string;
                    output_buffer += "\"";
                }

                output_buffer += "/>\n";

                if (! flush())
                    return false;
            }
        }

        output_buffer += "    </";
        output_buffer += TRIANGLES_TAG;
        output_buffer += ">\n   </";
        output_buffer += MESH_TAG;
        output_buffer += ">\n";

        // Force flush.
        return flush(true);
    }

    void _3MF_Exporter::add_transformation(std::stringstream &stream, const Transform3d &tr)
    {
        for (unsigned c = 0; c < 4; ++c) {
            for (unsigned r = 0; r < 3; ++r) {
                stream << tr(r, c);
                if (r != 2 || c != 3) stream << " ";
            }
        }
    }

    bool _3MF_Exporter::_add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items)
    {
        // This happens for empty projects
        if (build_items.size() == 0) {
            add_error("No build item found");
            return true;
        }

        stream << " <" << BUILD_TAG << ">\n";

        for (const BuildItem& item : build_items) {
            stream << "  <" << ITEM_TAG << " " << OBJECTID_ATTR << "=\"" << item.id << "\" " << TRANSFORM_ATTR << "=\"";
            add_transformation(stream, item.transform);
            stream << "\" " << PRINTABLE_ATTR << "=\"" << item.printable << "\"/>\n";
        }

        stream << " </" << BUILD_TAG << ">\n";

        return true;
    }

    bool _3MF_Exporter::_add_cut_information_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        std::string out = "";
        pt::ptree tree;

        unsigned int object_cnt = 0;
        for (const ModelObject* object : model.objects) {
            object_cnt++;
            if (!object->is_cut())
                continue;
            pt::ptree& obj_tree = tree.add("objects.object", "");

            obj_tree.put("<xmlattr>.id", object_cnt);

            // Store info for cut_id
            pt::ptree& cut_id_tree = obj_tree.add("cut_id", "");

            // store cut_id atributes
            cut_id_tree.put("<xmlattr>.id",             object->cut_id.id().id);
            cut_id_tree.put("<xmlattr>.check_sum",      object->cut_id.check_sum());
            cut_id_tree.put("<xmlattr>.connectors_cnt", object->cut_id.connectors_cnt());

            int volume_idx = -1;
            for (const ModelVolume* volume : object->volumes) {
                ++volume_idx;
                if (volume->is_cut_connector()) {
                    pt::ptree& connectors_tree = obj_tree.add("connectors.connector", "");
                    connectors_tree.put("<xmlattr>.volume_id",   volume_idx);
                    connectors_tree.put("<xmlattr>.type",        int(volume->cut_info.connector_type));
                    connectors_tree.put("<xmlattr>.r_tolerance", volume->cut_info.radius_tolerance);
                    connectors_tree.put("<xmlattr>.h_tolerance", volume->cut_info.height_tolerance);
                }
            }
        }

        if (!tree.empty()) {
            std::ostringstream oss;
            pt::write_xml(oss, tree);
            out = oss.str();

            // Post processing("beautification") of the output string for a better preview
            boost::replace_all(out, "><object", ">\n <object");
            boost::replace_all(out, "><cut_id", ">\n  <cut_id");
            boost::replace_all(out, "></cut_id>", ">\n  </cut_id>");
            boost::replace_all(out, "><connectors", ">\n  <connectors");
            boost::replace_all(out, "></connectors>", ">\n  </connectors>");
            boost::replace_all(out, "><connector", ">\n   <connector");
            boost::replace_all(out, "></connector>", ">\n   </connector>");
            boost::replace_all(out, "></object>", ">\n </object>");
            // OR just 
            boost::replace_all(out, "><", ">\n<");
        }

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, CUT_INFORMATION_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add cut information file to archive");
                return false;
            }
        }

        return true;
    }

    bool _3MF_Exporter::_add_layer_height_profile_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        std::string out = "";
        char buffer[1024];

        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            const std::vector<double>& layer_height_profile = object->layer_height_profile.get();
            if (layer_height_profile.size() >= 4 && layer_height_profile.size() % 2 == 0) {
                sprintf(buffer, "object_id=%d|", count);
                out += buffer;

                // Store the layer height profile as a single semicolon separated list.
                for (size_t i = 0; i < layer_height_profile.size(); ++i) {
                    sprintf(buffer, (i == 0) ? "%f" : ";%f", layer_height_profile[i]);
                    out += buffer;
                }
                
                out += "\n";
            }
        }

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, LAYER_HEIGHTS_PROFILE_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add layer heights profile file to archive");
                return false;
            }
        }

        return true;
    }

    bool _3MF_Exporter::_add_layer_config_ranges_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        std::string out = "";
        pt::ptree tree;

        unsigned int object_cnt = 0;
        for (const ModelObject* object : model.objects) {
            object_cnt++;
            const t_layer_config_ranges& ranges = object->layer_config_ranges;
            if (!ranges.empty())
            {
                pt::ptree& obj_tree = tree.add("objects.object","");

                obj_tree.put("<xmlattr>.id", object_cnt);

                // Store the layer config ranges.
                for (const auto& range : ranges) {
                    pt::ptree& range_tree = obj_tree.add("range", "");

                    // store minX and maxZ
                    range_tree.put("<xmlattr>.min_z", range.first.first);
                    range_tree.put("<xmlattr>.max_z", range.first.second);

                    // store range configuration
                    const ModelConfig& config = range.second;
                    for (const std::string& opt_key : config.keys()) {
                        pt::ptree& opt_tree = range_tree.add("option", config.opt_serialize(opt_key));
                        opt_tree.put("<xmlattr>.opt_key", opt_key);
                    }
                }
            }
        }

        if (!tree.empty()) {
            std::ostringstream oss;
            pt::write_xml(oss, tree);
            out = oss.str();

            // Post processing("beautification") of the output string for a better preview
            boost::replace_all(out, "><object",      ">\n <object");
            boost::replace_all(out, "><range",       ">\n  <range");
            boost::replace_all(out, "><option",      ">\n   <option");
            boost::replace_all(out, "></range>",     ">\n  </range>");
            boost::replace_all(out, "></object>",    ">\n </object>");
            // OR just 
            boost::replace_all(out, "><",            ">\n<"); 
        }

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, LAYER_CONFIG_RANGES_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add layer heights profile file to archive");
                return false;
            }
        }

        return true;
    }

    bool _3MF_Exporter::_add_sla_support_points_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        std::string out = "";
        char buffer[1024];

        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            const std::vector<sla::SupportPoint>& sla_support_points = object->sla_support_points;
            if (!sla_support_points.empty()) {
                sprintf(buffer, "object_id=%d|", count);
                out += buffer;

                // Store the layer height profile as a single space separated list.
                for (size_t i = 0; i < sla_support_points.size(); ++i) {
                    sprintf(buffer, (i==0 ? "%f %f %f %f %f" : " %f %f %f %f %f"),  sla_support_points[i].pos(0), sla_support_points[i].pos(1), sla_support_points[i].pos(2), sla_support_points[i].head_front_radius, (float)sla_support_points[i].is_new_island);
                    out += buffer;
                }
                out += "\n";
            }
        }

        if (!out.empty()) {
            // Adds version header at the beginning:
            out = std::string("support_points_format_version=") + std::to_string(support_points_format_version) + std::string("\n") + out;

            if (!mz_zip_writer_add_mem(&archive, SLA_SUPPORT_POINTS_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add sla support points file to archive");
                return false;
            }
        }
        return true;
    }
    
    bool _3MF_Exporter::_add_sla_drain_holes_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        const char *const fmt = "object_id=%d|";
        std::string out;
        
        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            sla::DrainHoles drain_holes = object->sla_drain_holes;

            // The holes were placed 1mm above the mesh in the first implementation.
            // This was a bad idea and the reference point was changed in 2.3 so
            // to be on the mesh exactly. The elevated position is still saved
            // in 3MFs for compatibility reasons.
            for (sla::DrainHole& hole : drain_holes) {
                hole.pos -= hole.normal.normalized();
                hole.height += 1.f;
            }

            if (!drain_holes.empty()) {
                out += string_printf(fmt, count);
                
                // Store the layer height profile as a single space separated list.
                for (size_t i = 0; i < drain_holes.size(); ++i)
                    out += string_printf((i == 0 ? "%f %f %f %f %f %f %f %f" : " %f %f %f %f %f %f %f %f"),
                                         drain_holes[i].pos(0),
                                         drain_holes[i].pos(1),
                                         drain_holes[i].pos(2),
                                         drain_holes[i].normal(0),
                                         drain_holes[i].normal(1),
                                         drain_holes[i].normal(2),
                                         drain_holes[i].radius,
                                         drain_holes[i].height);
                
                out += "\n";
            }
        }
        
        if (!out.empty()) {
            // Adds version header at the beginning:
            out = std::string("drain_holes_format_version=") + std::to_string(drain_holes_format_version) + std::string("\n") + out;
            
            if (!mz_zip_writer_add_mem(&archive, SLA_DRAIN_HOLES_FILE.c_str(), static_cast<const void*>(out.data()), out.length(), mz_uint(MZ_DEFAULT_COMPRESSION))) {
                add_error("Unable to add sla support points file to archive");
                return false;
            }
        }
        return true;
    }

    bool _3MF_Exporter::_add_print_config_file_to_archive(mz_zip_archive& archive, const DynamicPrintConfig &config)
    {
        assert(is_decimal_separator_point());
        char buffer[1024];
        sprintf(buffer, "; %s\n\n", header_slic3r_generated().c_str());
        std::string out = buffer;

        for (const std::string &key : config.keys())
            if (key != "compatible_printers")
                out += "; " + key + " = " + config.opt_serialize(key) + "\n";

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, PRINT_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add print config file to archive");
                return false;
            }
        }

        return true;
    }

    bool _3MF_Exporter::_add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model, const IdToObjectDataMap &objects_data)
    {
        enum class MetadataType{
            object,
            volume
        };

        auto add_metadata = [](std::stringstream &stream, unsigned indent, MetadataType type,
            const std::string &key, const std::string &value) {
            const char *type_value;
            switch (type) {
            case MetadataType::object: type_value = OBJECT_TYPE; break;
            case MetadataType::volume: type_value = VOLUME_TYPE; break;
            };
            stream << std::string(indent, ' ') << '<' << METADATA_TAG << " " 
                << TYPE_ATTR << "=\"" << type_value << "\" " 
                << KEY_ATTR  << "=\"" << key << "\" " 
                << VALUE_ATTR << "=\"" << xml_escape_double_quotes_attribute_value(value) << "\"/>\n";
        };

        std::stringstream stream;
        // Store mesh transformation in full precision, as the volumes are stored transformed and they need to be transformed back
        // when loaded as accurately as possible.
		stream << std::setprecision(std::numeric_limits<double>::max_digits10);
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<" << CONFIG_TAG << ">\n";

        for (const IdToObjectDataMap::value_type& obj_metadata : objects_data) {
            const ModelObject* obj = obj_metadata.second.object;
            if (obj == nullptr) continue;
            // Output of instances count added because of github #3435, currently not used by PrusaSlicer
            stream << " <" << OBJECT_TAG << " " << ID_ATTR << "=\"" << obj_metadata.first << "\" " << INSTANCESCOUNT_ATTR << "=\"" << obj->instances.size() << "\">\n";

            // stores object's name
            if (!obj->name.empty())                    
                add_metadata(stream, 2, MetadataType::object, "name", obj->name);
            // stores object's config data
            const ModelConfigObject &config = obj->config;
            for (const std::string& key : config.keys())
                add_metadata(stream, 2, MetadataType::object, key, config.opt_serialize(key));

            for (const ModelVolume* volume : obj_metadata.second.object->volumes) {
                if (volume == nullptr) continue;
                const VolumeToOffsetsMap& offsets = obj_metadata.second.volumes_offsets;
                VolumeToOffsetsMap::const_iterator it = offsets.find(volume);
                if (it != offsets.end()) {
                    // stores volume's offsets
                    stream << "  <" << VOLUME_TAG << " ";
                    stream << FIRST_TRIANGLE_ID_ATTR << "=\"" << it->second.first_triangle_id << "\" ";
                    stream << LAST_TRIANGLE_ID_ATTR << "=\"" << it->second.last_triangle_id << "\">\n";

                    // stores volume's name
                    if (!volume->name.empty())
                        add_metadata(stream, 3, MetadataType::volume, NAME_KEY, volume->name);

                    // stores volume's modifier field (legacy, to support old slicers)
                    if (volume->is_modifier())
                        add_metadata(stream, 3, MetadataType::volume, MODIFIER_KEY, "1");
                    // stores volume's type (overrides the modifier field above)
                    add_metadata(stream, 3, MetadataType::volume, VOLUME_TYPE_KEY, ModelVolume::type_to_string(volume->type()));

                    // stores volume's local matrix
                    stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << VOLUME_TYPE << "\" " << KEY_ATTR << "=\"" << MATRIX_KEY << "\" " << VALUE_ATTR << "=\"";
                    const Transform3d matrix = volume->get_matrix() * volume->source.transform.get_matrix();
                    for (int r = 0; r < 4; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            stream << matrix(r, c);
                            if (r != 3 || c != 3)
                                stream << " ";
                        }
                    }
                    stream << "\"/>\n";

                    // stores volume's source data
                    {
                        std::string input_file = xml_escape(m_fullpath_sources ? volume->source.input_file : boost::filesystem::path(volume->source.input_file).filename().string());
                        std::string prefix = std::string("   <") + METADATA_TAG + " " + TYPE_ATTR + "=\"" + VOLUME_TYPE + "\" " + KEY_ATTR + "=\"";
                        if (! volume->source.input_file.empty()) {
                            stream << prefix << SOURCE_FILE_KEY      << "\" " << VALUE_ATTR << "=\"" << input_file << "\"/>\n";
                            stream << prefix << SOURCE_OBJECT_ID_KEY << "\" " << VALUE_ATTR << "=\"" << volume->source.object_idx << "\"/>\n";
                            stream << prefix << SOURCE_VOLUME_ID_KEY << "\" " << VALUE_ATTR << "=\"" << volume->source.volume_idx << "\"/>\n";
                            stream << prefix << SOURCE_OFFSET_X_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(0) << "\"/>\n";
                            stream << prefix << SOURCE_OFFSET_Y_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(1) << "\"/>\n";
                            stream << prefix << SOURCE_OFFSET_Z_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(2) << "\"/>\n";
                        }
                        assert(! volume->source.is_converted_from_inches || ! volume->source.is_converted_from_meters);
                        if (volume->source.is_converted_from_inches)
                            stream << prefix << SOURCE_IN_INCHES_KEY << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                        else if (volume->source.is_converted_from_meters)
                            stream << prefix << SOURCE_IN_METERS_KEY << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                        if (volume->source.is_from_builtin_objects)
                            stream << prefix << SOURCE_IS_BUILTIN_VOLUME_KEY << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                    }

                    // stores volume's config data
                    for (const std::string& key : volume->config.keys()) {
                        stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << VOLUME_TYPE << "\" " << KEY_ATTR << "=\"" << key << "\" " << VALUE_ATTR << "=\"" << volume->config.opt_serialize(key) << "\"/>\n";
                    }

                    if (const std::optional<EmbossShape> &es = volume->emboss_shape;
                        es.has_value())
                        to_xml(stream, *es, *volume, archive);
                    
                    if (const std::optional<TextConfiguration> &tc = volume->text_configuration;
                        tc.has_value())
                        TextConfigurationSerialization::to_xml(stream, *tc);

                    // stores mesh's statistics
                    const RepairedMeshErrors& stats = volume->mesh().stats().repaired_errors;
                    stream << "   <" << MESH_TAG << " ";
                    stream << MESH_STAT_EDGES_FIXED        << "=\"" << stats.edges_fixed        << "\" ";
                    stream << MESH_STAT_DEGENERATED_FACETS << "=\"" << stats.degenerate_facets  << "\" ";
                    stream << MESH_STAT_FACETS_REMOVED     << "=\"" << stats.facets_removed     << "\" ";
                    stream << MESH_STAT_FACETS_RESERVED    << "=\"" << stats.facets_reversed    << "\" ";
                    stream << MESH_STAT_BACKWARDS_EDGES    << "=\"" << stats.backwards_edges    << "\"/>\n";

                    stream << "  </" << VOLUME_TAG << ">\n";
                }
            }
            stream << " </" << OBJECT_TAG << ">\n";
        }

        stream << "</" << CONFIG_TAG << ">\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, MODEL_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add model config file to archive");
            return false;
        }

        return true;
    }

bool _3MF_Exporter::_add_custom_gcode_per_print_z_file_to_archive( mz_zip_archive& archive, Model& model, const DynamicPrintConfig* config)
{
    std::string out = "";

    if (!model.custom_gcode_per_print_z.gcodes.empty()) {
        pt::ptree tree;
        pt::ptree& main_tree = tree.add("custom_gcodes_per_print_z", "");

        for (const CustomGCode::Item& code : model.custom_gcode_per_print_z.gcodes) {
            pt::ptree& code_tree = main_tree.add("code", "");

            // store data of custom_gcode_per_print_z
            code_tree.put("<xmlattr>.print_z"   , code.print_z  );
            code_tree.put("<xmlattr>.type"      , static_cast<int>(code.type));
            code_tree.put("<xmlattr>.extruder"  , code.extruder );
            code_tree.put("<xmlattr>.color"     , code.color    );
            code_tree.put("<xmlattr>.extra"     , code.extra    );

            // add gcode field data for the old version of the PrusaSlicer
            std::string gcode = code.type == CustomGCode::ColorChange ? config->opt_string("color_change_gcode")    :
                                code.type == CustomGCode::PausePrint  ? config->opt_string("pause_print_gcode")     :
                                code.type == CustomGCode::Template    ? config->opt_string("template_custom_gcode") :
                                code.type == CustomGCode::ToolChange  ? "tool_change"   : code.extra; 
            code_tree.put("<xmlattr>.gcode"     , gcode   );
        }

        pt::ptree& mode_tree = main_tree.add("mode", "");
        // store mode of a custom_gcode_per_print_z 
        mode_tree.put("<xmlattr>.value", model.custom_gcode_per_print_z.mode == CustomGCode::Mode::SingleExtruder ? CustomGCode::SingleExtruderMode :
                                         model.custom_gcode_per_print_z.mode == CustomGCode::Mode::MultiAsSingle ?  CustomGCode::MultiAsSingleMode :
                                         CustomGCode::MultiExtruderMode);

        if (!tree.empty()) {
            std::ostringstream oss;
            boost::property_tree::write_xml(oss, tree);
            out = oss.str();

            // Post processing("beautification") of the output string
            boost::replace_all(out, "><", ">\n<");
        }
    } 

    if (!out.empty()) {
        if (!mz_zip_writer_add_mem(&archive, CUSTOM_GCODE_PER_PRINT_Z_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add custom Gcodes per print_z file to archive");
            return false;
        }
    }

    return true;
}

// Perform conversions based on the config values available.
static void handle_legacy_project_loaded(unsigned int version_project_file, DynamicPrintConfig& config, const boost::optional<Semver>& prusaslicer_generator_version)
{
    if (! config.has("brim_separation")) {
        if (auto *opt_elephant_foot   = config.option<ConfigOptionFloat>("elefant_foot_compensation", false); opt_elephant_foot) {
            // Conversion from older PrusaSlicer which applied brim separation equal to elephant foot compensation.
            auto *opt_brim_separation = config.option<ConfigOptionFloat>("brim_separation", true);
            opt_brim_separation->value = opt_elephant_foot->value;
        }
    }
    
    // In PrusaSlicer 2.5.0-alpha2 and 2.5.0-alpha3, we introduce several parameters for Arachne that depend
    // on nozzle size . Later we decided to make default values for those parameters computed automatically
    // until the user changes them.
    if (prusaslicer_generator_version && *prusaslicer_generator_version >= *Semver::parse("2.5.0-alpha2") && *prusaslicer_generator_version <= *Semver::parse("2.5.0-alpha3")) {
        if (auto *opt_wall_transition_length = config.option<ConfigOptionFloatOrPercent>("wall_transition_length", false);
            opt_wall_transition_length && !opt_wall_transition_length->percent && opt_wall_transition_length->value == 0.4) {
            opt_wall_transition_length->percent = true;
            opt_wall_transition_length->value   = 100;
        }

        if (auto *opt_min_feature_size = config.option<ConfigOptionFloatOrPercent>("min_feature_size", false);
            opt_min_feature_size && !opt_min_feature_size->percent && opt_min_feature_size->value == 0.1) {
            opt_min_feature_size->percent = true;
            opt_min_feature_size->value   = 25;
        }
    }
}

bool is_project_3mf(const std::string& filename)
{
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, filename))
        return false;

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    // loop the entries to search for config
    mz_zip_archive_file_stat stat;
    bool config_found = false;
    for (mz_uint i = 0; i < num_entries; ++i) {
        if (mz_zip_reader_file_stat(&archive, i, &stat)) {
            std::string name(stat.m_filename);
            std::replace(name.begin(), name.end(), '\\', '/');

            if (boost::algorithm::iequals(name, PRINT_CONFIG_FILE)) {
                config_found = true;
                break;
            }
        }
    }

    close_zip_reader(&archive);

    return config_found;
}

bool load_3mf(const char* path, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, Model* model, bool check_version)
{
    if (path == nullptr || model == nullptr)
        return false;

    // All import should use "C" locales for number formatting.
    CNumericLocalesSetter locales_setter;
    _3MF_Importer         importer;
    bool res = importer.load_model_from_file(path, *model, config, config_substitutions, check_version);
    importer.log_errors();
    handle_legacy_project_loaded(importer.version(), config, importer.prusaslicer_generator_version());

    return res;
}

bool store_3mf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data, bool zip64)
{
    // All export should use "C" locales for number formatting.
    CNumericLocalesSetter locales_setter;

    if (path == nullptr || model == nullptr)
        return false;

    _3MF_Exporter exporter;
    bool res = exporter.save_model_to_file(path, *model, config, fullpath_sources, thumbnail_data, zip64);
    if (!res)
        exporter.log_errors();

    return res;
}

namespace{

// Conversion with bidirectional map
// F .. first, S .. second
template<typename F, typename S>
F bimap_cvt(const boost::bimap<F, S> &bmap, S s, const F & def_value) {
    const auto &map = bmap.right;
    auto found_item = map.find(s);

    // only for back and forward compatibility
    assert(found_item != map.end()); 
    if (found_item == map.end())
        return def_value;

    return found_item->second;
}

template<typename F, typename S> 
S bimap_cvt(const boost::bimap<F, S> &bmap, F f, const S &def_value)
{
    const auto &map = bmap.left;
    auto found_item = map.find(f);

    // only for back and forward compatibility
    assert(found_item != map.end());
    if (found_item == map.end())
        return def_value;

    return found_item->second;
}

} // namespace

/// <summary>
/// TextConfiguration serialization
/// </summary>
const TextConfigurationSerialization::TypeToName TextConfigurationSerialization::type_to_name =
    boost::assign::list_of<TypeToName::relation>
    (EmbossStyle::Type::file_path, "file_name")
    (EmbossStyle::Type::wx_win_font_descr, "wxFontDescriptor_Windows")
    (EmbossStyle::Type::wx_lin_font_descr, "wxFontDescriptor_Linux")
    (EmbossStyle::Type::wx_mac_font_descr, "wxFontDescriptor_MacOsX");

const TextConfigurationSerialization::HorizontalAlignToName TextConfigurationSerialization::horizontal_align_to_name =
    boost::assign::list_of<HorizontalAlignToName::relation>
    (FontProp::HorizontalAlign::left, "left")
    (FontProp::HorizontalAlign::center, "center")
    (FontProp::HorizontalAlign::right, "right");

const TextConfigurationSerialization::VerticalAlignToName TextConfigurationSerialization::vertical_align_to_name =
    boost::assign::list_of<VerticalAlignToName::relation>
    (FontProp::VerticalAlign::top, "top")
    (FontProp::VerticalAlign::center, "middle")
    (FontProp::VerticalAlign::bottom, "bottom");


void TextConfigurationSerialization::to_xml(std::stringstream &stream, const TextConfiguration &tc)
{
    stream << "   <" << TEXT_TAG << " ";

    stream << TEXT_DATA_ATTR << "=\"" << xml_escape_double_quotes_attribute_value(tc.text) << "\" ";
    // font item
    const EmbossStyle &style = tc.style;
    stream << STYLE_NAME_ATTR <<  "=\"" << xml_escape_double_quotes_attribute_value(style.name) << "\" ";
    stream << FONT_DESCRIPTOR_ATTR << "=\"" << xml_escape_double_quotes_attribute_value(style.path) << "\" ";
    constexpr std::string_view dafault_type{"undefined"};
    std::string_view style_type = bimap_cvt(type_to_name, style.type, dafault_type);
    stream << FONT_DESCRIPTOR_TYPE_ATTR << "=\"" << style_type << "\" ";

    // font property
    const FontProp &fp = tc.style.prop;
    if (fp.char_gap.has_value())
        stream << CHAR_GAP_ATTR << "=\"" << *fp.char_gap << "\" ";
    if (fp.line_gap.has_value())
        stream << LINE_GAP_ATTR << "=\"" << *fp.line_gap << "\" ";

    stream << LINE_HEIGHT_ATTR << "=\"" << fp.size_in_mm << "\" ";
    if (fp.boldness.has_value())
        stream << BOLDNESS_ATTR << "=\"" << *fp.boldness << "\" ";
    if (fp.skew.has_value())
        stream << SKEW_ATTR << "=\"" << *fp.skew << "\" ";
    if (fp.per_glyph)
        stream << PER_GLYPH_ATTR << "=\"" << 1 << "\" ";
    stream << HORIZONTAL_ALIGN_ATTR << "=\"" << bimap_cvt(horizontal_align_to_name, fp.align.first, dafault_type) << "\" ";
    stream << VERTICAL_ALIGN_ATTR   << "=\"" << bimap_cvt(vertical_align_to_name,  fp.align.second, dafault_type) << "\" ";
    if (fp.collection_number.has_value())
        stream << COLLECTION_NUMBER_ATTR << "=\"" << *fp.collection_number << "\" ";
    // font descriptor
    if (fp.family.has_value())
        stream << FONT_FAMILY_ATTR << "=\"" << *fp.family << "\" ";
    if (fp.face_name.has_value())
        stream << FONT_FACE_NAME_ATTR << "=\"" << *fp.face_name << "\" ";
    if (fp.style.has_value())
        stream << FONT_STYLE_ATTR << "=\"" << *fp.style << "\" ";
    if (fp.weight.has_value())
        stream << FONT_WEIGHT_ATTR << "=\"" << *fp.weight << "\" ";

    stream << "/>\n"; // end TEXT_TAG
}
namespace {

FontProp::HorizontalAlign read_horizontal_align(const char **attributes, unsigned int num_attributes, const TextConfigurationSerialization::HorizontalAlignToName& horizontal_align_to_name){
    std::string horizontal_align_str = get_attribute_value_string(attributes, num_attributes, HORIZONTAL_ALIGN_ATTR);

    // Back compatibility
    // PS 2.6.0 do not have align
    if (horizontal_align_str.empty())
        return FontProp::HorizontalAlign::center;

    // Back compatibility
    // PS 2.6.1 store indices(0|1|2) instead of text for align
    if (horizontal_align_str.length() == 1) {
        int horizontal_align_int = 0;
        if(boost::spirit::qi::parse(horizontal_align_str.c_str(), horizontal_align_str.c_str() + 1, boost::spirit::qi::int_, horizontal_align_int))
            return static_cast<FontProp::HorizontalAlign>(horizontal_align_int);
    }

    return bimap_cvt(horizontal_align_to_name, std::string_view(horizontal_align_str), FontProp::HorizontalAlign::center);    
}


FontProp::VerticalAlign read_vertical_align(const char **attributes, unsigned int num_attributes, const TextConfigurationSerialization::VerticalAlignToName& vertical_align_to_name){
    std::string vertical_align_str = get_attribute_value_string(attributes, num_attributes, VERTICAL_ALIGN_ATTR);

    // Back compatibility
    // PS 2.6.0 do not have align
    if (vertical_align_str.empty())
        return FontProp::VerticalAlign::center;

    // Back compatibility
    // PS 2.6.1 store indices(0|1|2) instead of text for align
    if (vertical_align_str.length() == 1) {
        int vertical_align_int = 0;
        if(boost::spirit::qi::parse(vertical_align_str.c_str(), vertical_align_str.c_str() + 1, boost::spirit::qi::int_, vertical_align_int))
            return static_cast<FontProp::VerticalAlign>(vertical_align_int);
    }

    return bimap_cvt(vertical_align_to_name, std::string_view(vertical_align_str), FontProp::VerticalAlign::center);
}

} // namespace

std::optional<TextConfiguration> TextConfigurationSerialization::read(const char **attributes, unsigned int num_attributes)
{
    FontProp fp;
    int char_gap = get_attribute_value_int(attributes, num_attributes, CHAR_GAP_ATTR);
    if (char_gap != 0) fp.char_gap = char_gap;
    int line_gap = get_attribute_value_int(attributes, num_attributes, LINE_GAP_ATTR); 
    if (line_gap != 0) fp.line_gap = line_gap;
    float boldness = get_attribute_value_float(attributes, num_attributes, BOLDNESS_ATTR);
    if (std::fabs(boldness) > std::numeric_limits<float>::epsilon())
        fp.boldness = boldness;
    float skew = get_attribute_value_float(attributes, num_attributes, SKEW_ATTR);
    if (std::fabs(skew) > std::numeric_limits<float>::epsilon())
        fp.skew = skew;
    int per_glyph = get_attribute_value_int(attributes, num_attributes, PER_GLYPH_ATTR);
    if (per_glyph == 1) fp.per_glyph = true;

    fp.align = FontProp::Align(
        read_horizontal_align(attributes, num_attributes, horizontal_align_to_name),
        read_vertical_align(attributes, num_attributes, vertical_align_to_name));

    int collection_number = get_attribute_value_int(attributes, num_attributes, COLLECTION_NUMBER_ATTR);
    if (collection_number > 0) fp.collection_number = static_cast<unsigned int>(collection_number);

    fp.size_in_mm = get_attribute_value_float(attributes, num_attributes, LINE_HEIGHT_ATTR);

    std::string family = get_attribute_value_string(attributes, num_attributes, FONT_FAMILY_ATTR);
    if (!family.empty()) fp.family = family;
    std::string face_name = get_attribute_value_string(attributes, num_attributes, FONT_FACE_NAME_ATTR);
    if (!face_name.empty()) fp.face_name = face_name;
    std::string style = get_attribute_value_string(attributes, num_attributes, FONT_STYLE_ATTR);
    if (!style.empty()) fp.style = style;
    std::string weight = get_attribute_value_string(attributes, num_attributes, FONT_WEIGHT_ATTR);
    if (!weight.empty()) fp.weight = weight;

    std::string style_name = get_attribute_value_string(attributes, num_attributes, STYLE_NAME_ATTR);
    std::string font_descriptor = get_attribute_value_string(attributes, num_attributes, FONT_DESCRIPTOR_ATTR);
    std::string type_str = get_attribute_value_string(attributes, num_attributes, FONT_DESCRIPTOR_TYPE_ATTR);
    EmbossStyle::Type type = bimap_cvt(type_to_name, std::string_view{type_str}, EmbossStyle::Type::undefined);

    std::string text = get_attribute_value_string(attributes, num_attributes, TEXT_DATA_ATTR);
    EmbossStyle es{style_name, std::move(font_descriptor), type, std::move(fp)};
    return TextConfiguration{std::move(es), std::move(text)};
}

EmbossShape TextConfigurationSerialization::read_old(const char **attributes, unsigned int num_attributes)
{
    EmbossShape es;
    std::string fix_tr_mat_str = get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR);
    if (!fix_tr_mat_str.empty())
        es.fix_3mf_tr = get_transform_from_3mf_specs_string(fix_tr_mat_str);


    if (get_attribute_value_int(attributes, num_attributes, USE_SURFACE_ATTR) == 1)
        es.projection.use_surface = true;

    es.projection.depth = get_attribute_value_float(attributes, num_attributes, DEPTH_ATTR);

    int use_surface = get_attribute_value_int(attributes, num_attributes, USE_SURFACE_ATTR);
    if (use_surface == 1)
        es.projection.use_surface = true;

    return es;
}

namespace {
Transform3d create_fix(const std::optional<Transform3d> &prev, const ModelVolume &volume)
{
    // IMPROVE: check if volume was modified (translated, rotated OR scaled)
    // when no change do not calculate transformation only store original fix matrix

    // Create transformation used after load actual stored volume
    const Transform3d &actual_trmat = volume.get_matrix();

    const auto &vertices = volume.mesh().its.vertices;
    Vec3d       min      = actual_trmat * vertices.front().cast<double>();
    Vec3d       max      = min;
    for (const Vec3f &v : vertices) {
        Vec3d vd = actual_trmat * v.cast<double>();
        for (size_t i = 0; i < 3; ++i) {
            if (min[i] > vd[i])
                min[i] = vd[i];
            if (max[i] < vd[i])
                max[i] = vd[i];
        }
    }
    Vec3d       center     = (max + min) / 2;
    Transform3d post_trmat = Transform3d::Identity();
    post_trmat.translate(center);

    Transform3d fix_trmat = actual_trmat.inverse() * post_trmat;
    if (!prev.has_value())
        return fix_trmat;

    // check whether fix somehow differ previous
    if (fix_trmat.isApprox(Transform3d::Identity(), 1e-5))
        return *prev;

    return *prev * fix_trmat;
}

bool to_xml(std::stringstream &stream, const EmbossShape::SvgFile &svg, const ModelVolume &volume, mz_zip_archive &archive){
    if (svg.path_in_3mf.empty())
        return true; // EmbossedText OR unwanted store .svg file into .3mf (protection of copyRight)

    if (!svg.path.empty())
        stream << SVG_FILE_PATH_ATTR << "=\"" << xml_escape_double_quotes_attribute_value(svg.path) << "\" ";
    stream << SVG_FILE_PATH_IN_3MF_ATTR << "=\"" << xml_escape_double_quotes_attribute_value(svg.path_in_3mf) << "\" ";

    std::shared_ptr<std::string> file_data = svg.file_data;
    assert(file_data != nullptr); 
    if (file_data == nullptr && !svg.path.empty())
        file_data = read_from_disk(svg.path);
    if (file_data == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "Can't write svg file no filedata";
        return false;
    }
    const std::string &file_data_str = *file_data; 

    return mz_zip_writer_add_mem(&archive, svg.path_in_3mf.c_str(), 
        (const void *) file_data_str.c_str(), file_data_str.size(), MZ_DEFAULT_COMPRESSION);
}

} // namespace

void to_xml(std::stringstream &stream, const EmbossShape &es, const ModelVolume &volume, mz_zip_archive &archive)
{
    stream << "   <" << SHAPE_TAG << " ";
    if (es.svg_file.has_value())
        if(!to_xml(stream, *es.svg_file, volume, archive))
            BOOST_LOG_TRIVIAL(warning) << "Can't write svg file defiden embossed shape into 3mf";
    
    stream << SHAPE_SCALE_ATTR << "=\"" << es.scale << "\" ";

    if (!es.final_shape.is_healed)
        stream << UNHEALED_ATTR << "=\"" << 1 << "\" ";

    // projection
    const EmbossProjection &p = es.projection;
    stream << DEPTH_ATTR << "=\"" << p.depth << "\" ";
    if (p.use_surface)
        stream << USE_SURFACE_ATTR << "=\"" << 1 << "\" ";
    
    // FIX of baked transformation
    Transform3d fix = create_fix(es.fix_3mf_tr, volume);
    stream << TRANSFORM_ATTR << "=\"";
    _3MF_Exporter::add_transformation(stream, fix);
    stream << "\" ";

    stream << "/>\n"; // end SHAPE_TAG    
}

std::optional<EmbossShape> read_emboss_shape(const char **attributes, unsigned int num_attributes) {    
    double scale = get_attribute_value_float(attributes, num_attributes, SHAPE_SCALE_ATTR);
    int unhealed = get_attribute_value_int(attributes, num_attributes, UNHEALED_ATTR);
    bool is_healed = unhealed != 1;

    EmbossProjection projection;
    projection.depth = get_attribute_value_float(attributes, num_attributes, DEPTH_ATTR);
    if (is_approx(projection.depth, 0.))
        projection.depth = 10.;

    int use_surface  = get_attribute_value_int(attributes, num_attributes, USE_SURFACE_ATTR);
    if (use_surface == 1)
        projection.use_surface = true;     

    std::optional<Transform3d> fix_tr_mat;
    std::string fix_tr_mat_str = get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR);
    if (!fix_tr_mat_str.empty()) { 
        fix_tr_mat = get_transform_from_3mf_specs_string(fix_tr_mat_str);
    }

    std::string file_path = get_attribute_value_string(attributes, num_attributes, SVG_FILE_PATH_ATTR);
    std::string file_path_3mf = get_attribute_value_string(attributes, num_attributes, SVG_FILE_PATH_IN_3MF_ATTR);

    // MayBe: store also shapes to not store svg
    // But be carefull curve will be lost -> scale will not change sampling
    // shapes could be loaded from SVG
    ExPolygonsWithIds shapes; 
    // final shape could be calculated from shapes
    HealedExPolygons final_shape;
    final_shape.is_healed = is_healed;

    EmbossShape::SvgFile svg{file_path, file_path_3mf};
    return EmbossShape{std::move(shapes), std::move(final_shape), scale, std::move(projection), std::move(fix_tr_mat), std::move(svg)};
}


} // namespace Slic3r
