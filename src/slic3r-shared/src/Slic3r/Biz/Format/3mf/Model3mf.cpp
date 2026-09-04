#include "Model3mf.hpp"

#include <array>
#include "fast_float.h"
#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <boost/spirit/include/qi_int.hpp> // text to int
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp> // uuid to stream
#include <boost/algorithm/string.hpp>
#include <expat.h>
#include "Slic3r/Biz/Format/Metadata.hpp"
#include "Relations.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp"
#include "Slic3r/Biz/Utils/XmlEscape.hpp"
#include "Slic3r/Biz/Format/3mf/LegacyPaintingCompat.hpp"

#include "Slic3r/Time.hpp" // utc_timestamp
#include "LocalesUtils.hpp" // CNumericLocalesSetter

#include <boost/spirit/include/karma.hpp>
#include <libdeflate.h> // ZipBackend::Compressed

#include "Slic3r/Utils.hpp" // ScopeGuard
#include "Slic3r/Version.hpp"

using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Vec3f;

using Slic3r::Domain::is_approx;


using ModelObject = Slic3r::Domain::ModelObject;
using ModelVolume = Slic3r::Domain::ModelVolume;
using ModelInstance = Slic3r::Domain::ModelInstance;
using Model = Slic3r::Domain::Model;
using ModelVolumeType = Slic3r::Domain::ModelVolumeType;
using ModelVolumePtrs = Slic3r::Domain::ModelVolumePtrs;

using namespace Slic3r;
using namespace format_3MF;
using TriangleMesh = Slic3r::Domain::TriangleMesh;

/// <summary>
/// Since C++17, the best way to keep string literal is:
/// constexpr std::string_view str = "some string key";
/// str will be substituted by the compiler to the places where it is used at compile time.
/// Memory-wise: You got rid of storing the str in run-time
///              since it is only available at compile time.
/// Speed-wise: Less indirections to get the data in runtime than const char *.
/// Consistency-wise: Constexpr is solely used for expressions that are immutable
///    and available at compile time. Also string_view is solely used for immutable strings
///    so you are using the exact data type needed for you.
/// </summary>

namespace {

// Used names inside of xml document use convention:
// _TAG  .. suffix for name of XML <tag>
// _ATTR .. suffix for name of attribute inside of tag

// Root <tag> in '*.model' file (XML formated)
constexpr const char *MODEL_TAG = "model";

// model attributes
constexpr const char *XMLNS_ATTR = "xmlns";
constexpr const char *XMLNS_PREFIX_ATTR = "xmlns:";
constexpr const char *UNIT_ATTR = "unit";
constexpr const char *LANG_ATTR = "xml:lang";
constexpr const char *REQUIRED_EXTENSION_ATTR = "requiredextensions";
constexpr const char *RECOMMENDED_EXTENSION_ATTR = "recommendedextensions";

// Core specification of namespace - define 3mf version
constexpr const char *XMLNS_VALUE =            "http://schemas.microsoft.com/3dmanufacturing/core/2015/02";
constexpr const char *XMLNS_PRODUCTION_VALUE = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06";
constexpr const char *XMLNS_MIRRORMESH_VALUE = "http://schemas.microsoft.com/3dmanufacturing/mirroring/2021/07";
constexpr const char *XMLNS_SLIC3R_VALUE =     "http://schemas.slic3r.org/3mf/2017/06"; // slic3rpe: prefix, also written when dual-writing MM segmentation for 2.x readers

// model nodes
constexpr const char *METADATA_TAG = "metadata";
constexpr const char *RESOURCES_TAG = "resources";
constexpr const char *BUILD_TAG = "build";

// metadata attributes
constexpr const char *NAME_ATTR = "name";
constexpr const char *TYPE_ATTR = "type";
constexpr const char *PRESERVE_ATTR = "preserve";

// resources nodes
constexpr const char *BASEMATERIALS_TAG = "basematerials";
constexpr const char *OBJECT_TAG = "object";

// Build node
constexpr const char *ITEM_TAG = "item";

// Item attributes
constexpr const char *OBJECTID_ATTR = "objectid";
constexpr const char *TRANSFORM_ATTR = "transform";
constexpr const char *PARTNUMBER_ATTR = "partnumber";

// Item nodes
constexpr const char *METADATAGROUP_TAG = "metadatagroup"; // zero OR one tag

// object attr
constexpr const char *ID_ATTR = "id";
// constexpr const char *TYPE_ATTR = "type"; // also in metadata
constexpr const char *THUMBNAIL_ATTR = "thumbnail";
// constexpr const char *PARTNUMBER_ATTR = "partnumber"; // also in item
// constexpr const char *NAME_ATTR = "name"; // also in metada
constexpr const char *PID_ATTR = "pid";
constexpr const char *PINDEX_ATTR = "pindex";

// ...
constexpr const char *MESH_TAG = "mesh";

// Mirror mesh
constexpr const char *ORIGINALMESH_ATTR = "originalmesh";
constexpr const char *N_X_ATTR = "nx";
constexpr const char *N_Y_ATTR = "ny";
constexpr const char *N_Z_ATTR = "nz";
constexpr const char *DISTANCE_ATTR = "d";

constexpr const char *VERTICES_TAG = "vertices";
constexpr const char *VERTEX_TAG = "vertex";
constexpr const char *X_ATTR = "x";
constexpr const char *Y_ATTR = "y";
constexpr const char *Z_ATTR = "z";

constexpr const char *TRIANGLES_TAG = "triangles";
constexpr const char *TRIANGLE_TAG = "triangle";
constexpr const char *V1_ATTR = "v1";
constexpr const char *V2_ATTR = "v2";
constexpr const char *V3_ATTR = "v3";

constexpr const char *TRIANGLESETS_TAG = "t:trianglesets";
constexpr const char *MIRRORMESH_TAG = "mm:mirrormesh";

constexpr const char *COMPONENTS_TAG = "components";
constexpr const char *COMPONENT_TAG = "component";

// production attribute
constexpr const char *PROD_NS = "p:";
constexpr const char *PATH_ATTR = "path";
constexpr const char *UUID_ATTR = "UUID";

const Slic3r::Semver last_old_stored_version = *Semver::parse("2.99.1"); // last version to be parsed by "legacy" parser

using UnitToName = boost::bimap<ST_Unit, std::string_view>;
const UnitToName unit_to_name = boost::assign::list_of<UnitToName::relation>
    (ST_Unit::millimeter, "millimeter")
    (ST_Unit::inch,       "inch")
    (ST_Unit::micron,     "micron")
    (ST_Unit::centimeter, "centimeter")
    (ST_Unit::foot,       "foot")
    (ST_Unit::meter,      "meter");

using ObjectTypeToName = boost::bimap<ObjectType, std::string_view>;
const ObjectTypeToName object_type_to_name = boost::assign::list_of<ObjectTypeToName::relation>
    (ObjectType::model,       "model")
    (ObjectType::solidsupport,"solidsupport")
    (ObjectType::support,     "support")
    (ObjectType::surface,     "surface")
    (ObjectType::other,       "other");

// status machine
enum class XmlState {
start,
 model,
  metadata,
  resources,
   basematerials,
   object,
    metadatagroup,
    mesh,
     vertices,
      vertex,
     triangles,
      triangle,
     trianglesets,
     mirrormesh,
    components,
     component,
  build,
   item,
finished,

// inside of unknown tag
// LoadContext::unknown_depth store unknown depth
unknown 
};

struct LoadedModelFile {
    std::optional<format_3MF::Model> model;

    LoadedModelFile() = default;
    LoadedModelFile(format_3MF::Model &&model_) : model{model_} {}
};

struct LoadContext {
    LoadContext(XML_Parser* xml_parser, Read3mfIssues& collected_issues) :
        xml_parser{ xml_parser }, collected_issues{ collected_issues }
    {}

    XML_Parser *xml_parser;
    XmlState state = XmlState::start;
    size_t unknown_depth = 0;
    XmlState unknown_in_state = XmlState::unknown;
    LoadedModelFile model;

    Read3mfIssues& collected_issues;

    // keep unfinished characters between tags
    std::string xml_characters;

    bool exist_model_resources = false;
    bool exist_model_build = false;
};

struct Attribute{
    const XML_Char *name;
    const XML_Char *value;
};
using Attributes = std::vector<Attribute>;
Attributes create_attributes(const XML_Char **atts, int num_atts) {
    if (num_atts < 1) return {};
    assert(num_atts % 2 == 0);
    //if (num_atts % 2 != 0) return {}; // should not appear    
    Attributes result;
    result.reserve(num_atts / 2);
    for (int a = 0; a < num_atts; a += 2)
        result.push_back({atts[a], atts[a + 1]});
    return result;
}

static const fast_float::parse_options po;
void parse_float(const XML_Char *value, float &v, Read3mfIssues& collected_issues, Read3mfIssueType issue) {
    assert(value != nullptr);
    if (value == nullptr){
        collected_issues.add_issue(Read3mfIssue(issue, "No value to parse"));
        return;
    }

    fast_float::from_chars_result r =
        fast_float::from_chars_advanced(value, value + strlen(value), v, po);
    if (r.ec != std::errc()){
        collected_issues.add_issue(Read3mfIssue(issue, std::to_string(static_cast<int>(r.ec))));
    }
}

void parse_float(const XML_Char *value, float &v){
    assert(value != nullptr);
    if (value == nullptr)
        return;
    //fast_float::from_chars_result r = 
    fast_float::from_chars_advanced(value, value + strlen(value), v, po);
}

std::array<char, 3> vertex_attrs = {X_ATTR[0], Y_ATTR[0], Z_ATTR[0]};
void process_vertex_atts(LoadedModelFile &model, const XML_Char **atts, int num_atts, Read3mfIssues& collected_issues){
    std::vector<Domain::Vec3f>& vertices = model.model->resource.objects.back().mesh.its.vertices;
    vertices.emplace_back();
    Domain::Vec3f& vertex = vertices.back();
    for (int a = 0, i = 0; a < num_atts; a += 2,++i) {
        const XML_Char *name = atts[a];
        const XML_Char *value = atts[a+1];
        // expected order of attributes with one character{x,y,z}
        if (i < 3 && name[0] == vertex_attrs[i] && name[1] == '\0'){
            parse_float(value, vertex[i]);
        } else {
            // un expected order
            if (::strcmp(name, X_ATTR) == 0) {
                parse_float(value, vertex.x());
            } else if (::strcmp(name, Y_ATTR) == 0) {
                parse_float(value, vertex.y());
            } else if (::strcmp(name, Z_ATTR) == 0) {
                parse_float(value, vertex.z());
            } else {
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_vertex_unknown_attr,
                    std::string(name), std::string(value)));
            }
        }
    }
    // TODO: check that x,y and z was setted?
}

int parse_int(const XML_Char *value_str) {
    assert(value_str != nullptr);
    int value = 0;    
    if (value_str == nullptr)
        return value;

    boost::spirit::qi::parse(value_str, value_str + strlen(value_str), 
        boost::spirit::qi::int_, value);
    return value;
}

bool parse_id(const XML_Char *id_str, ST_ResourceID& id) {
    int id_int = parse_int(id_str);
    if (id_int < 1)
        return false;
    id = static_cast<ST_ResourceID>(id_int);
    return true;
}

constexpr const char *v123 = "123";
void process_triangle_atts(LoadedModelFile &model, const XML_Char **atts, int num_atts, Read3mfIssues& collected_issues){
    std::vector<Domain::Index3>& triangles = model.model->resource.objects.back().mesh.its.indices;
    triangles.emplace_back();
    Domain::Index3 &triangle = triangles.back();
    for (int a = 0, i = 0; a < num_atts; a += 2, ++i) {
        const XML_Char *name = atts[a];
        const XML_Char *value = atts[a+1];
        // expected order of attributes
        if (i < 3 && name[0] == 'v' && name[1] == v123[i] && name[2] == '\0') {
            triangle[i] = parse_int(value);
        } else {
            // un expected order
            if (::strcmp(name, V1_ATTR) == 0) {
                triangle[0] = parse_int(value);
            } else if (::strcmp(name, V2_ATTR) == 0) {
                triangle[1] = parse_int(value);
            } else if (::strcmp(name, V3_ATTR) == 0) {
                triangle[2] = parse_int(value);
            } else if (::strcmp(name, Legacy::MM_SEGMENTATION_ATTR) == 0) {
                // Dual-written for 2.x readers (see store_meshes()). This reader gets the
                // same data from Metadata/Slic3r_facets_annotation.json, so just ignore it here.
            } else {
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_triangle_unknown_attr,
                    std::string(name), std::string(value)));
            }
        }
    }
}

/// <summary>
/// Split null terminated string by delimiter
/// </summary>
/// <param name="s">null terminated string</param>
/// <param name="delim">delimiter</param>
/// <returns>splitted strings</returns>
std::vector<std::string> split(const char *s, char delim) {
    std::vector<std::string> result;
    std::string value_str(s); // copy
    boost::split(result, value_str, boost::is_any_of(" "), boost::token_compress_on);
    return result;
}
std::vector<std::string> split_by_space(const char *s) { return split(s, ' '); }
std::vector<std::string> parse_extensions(const char *value) { return split_by_space(value); }

void process_unknown_attr(
    Read3mfIssueType type,
    const Attribute &attribute,
    Read3mfIssues& collected_issues,
    bool is_3mf_allowed = true
) {
    std::string name_str(attribute.name);
    if (attribute.value == nullptr) {
        collected_issues.add_issue(Read3mfIssue(type, name_str));
    } else {
        collected_issues.add_issue(Read3mfIssue(type, name_str, std::string(attribute.value)));
    }
}

void process_model_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    // initialize model object
    model.model = format_3MF::Model{}; 
    if (attributes.empty()) return;

    for (const Attribute& attr: attributes) {
        if (::strcmp(attr.name, UNIT_ATTR) == 0) {
            const auto& name_to_unit = unit_to_name.right;
            auto it = name_to_unit.find(attr.value);
            if (it == name_to_unit.end()) {
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_unknown_language, attr.value));
                continue;
            }
            model.model->unit = it->second;        
        } else if (::strcmp(attr.name, LANG_ATTR) == 0) {
            model.model->lang = (attr.value == nullptr) ? "" : attr.value;
        } else if (::strcmp(attr.name, REQUIRED_EXTENSION_ATTR) == 0) {
            // Space-delimited list of namespace prefixes,
            // representing the set of extensions that are required
            // for processing the document.
            // MUST NOT process not supported extensions
            model.model->required_extensions = parse_extensions(attr.value);
        } else if (::strcmp(attr.name, RECOMMENDED_EXTENSION_ATTR) == 0) {
            // Space-delimited list of namespace prefixes,
            // representing the set of extensions that are recommended
            // for processing the document with its design intent.
            // Editors and manufacturing devices SHOULD warn and inform the user
            // if they do not support the recommended extensions
            // and ask for input how to proceed.
            // Required extensions MUST NOT be recommended at the same time.
            model.model->recomended_extensions = parse_extensions(attr.value);
        } else if (::strcmp(attr.name, XMLNS_ATTR) == 0) {
            if (::strcmp(attr.value, XMLNS_VALUE) != 0)
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_bad_xmlns, attr.value));
        } else if (boost::starts_with(attr.name, XMLNS_PREFIX_ATTR)) {
            size_t prefix_size = std::char_traits<char>::length(XMLNS_PREFIX_ATTR);
            std::string xml_ns(attr.name + prefix_size);
            if (::strcmp(attr.value, XMLNS_SLIC3R_VALUE) == 0) {
                // silent skip namespace 
                // xml_ns value SHOULD BE "slic3rpe"
                continue;
            } else if (::strcmp(attr.value, XMLNS_PRODUCTION_VALUE) == 0) {
                // Define namespace prefix for production extension
                // used in required and recomended sets + attributes defined by extension
                model.model->prod_ns = xml_ns;
                continue;
            } else if (::strcmp(attr.value, XMLNS_MIRRORMESH_VALUE) == 0) {
                // Define namespace prefix for production extension
                // used in required and recomended sets + attributes defined by extension
                model.model->mirror_ns = xml_ns;
                continue;
            }
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_unknown_namespace, attr.value, xml_ns));
        } else {
            process_unknown_attr(Read3mfIssueType::model_unknown_attr, attr, collected_issues);
        }
    }
}

void process_metadata_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    CT_Metadata_Model& metadata = model.model->metadata;
    metadata.emplace_back(); // insert new one
    ModelMetadata& meta = metadata.back();
    for (const Attribute &attr : attributes) {
        if (::strcmp(attr.name, NAME_ATTR) == 0) {
            read_name(meta.name, attr.value);
        } else if (::strcmp(attr.name, PRESERVE_ATTR) == 0) {
            meta.preserve = true;
            // TODO: what to do with value?
        } else if (::strcmp(attr.name, TYPE_ATTR) == 0) {
            meta.type = std::string(attr.value);
        } else {
            process_unknown_attr(Read3mfIssueType::model_metadata_unknown_attr, attr, collected_issues);
        }
    }

    if (!std::holds_alternative<std::string>(meta.name) &&
        !std::holds_alternative<ModelMetadataNames>(meta.name)) {
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_metadata_require_name));
    }
}

Domain::Transform3d parse_transformation(const XML_Char *data, Read3mfIssues& collected_issues, Read3mfIssueType issue_type) {
    // check: https://3mf.io/3d-manufacturing-format/ 
    // or https://github.com/3MFConsortium/spec_core/blob/master/3MF%20Core%20Specification.md 
    // to see how matrices are stored inside 3mf according to specifications
    
    if (data == nullptr)
        // empty string means default identity matrix
        return Domain::Transform3d::Identity();

    std::vector<std::string> mat_elements_str = split_by_space(data);
    if (mat_elements_str.size() != 12) {
        collected_issues.add_issue(Read3mfIssue(issue_type, std::string(data)));
        // invalid data, return identity matrix
        return Domain::Transform3d::Identity();
    }

    auto ret = Domain::Transform3d::Identity();
    unsigned int i = 0;
    // matrices are stored into 3mf files as 4x3
    // we need to transpose them
    for (unsigned int c = 0; c < 4; ++c) {
        for (unsigned int r = 0; r < 3; ++r) {
            const std::string& element_str = mat_elements_str[i++];
            fast_float::from_chars_result res = 
                fast_float::from_chars<double>(element_str.data(), element_str.data() + element_str.size(), ret(r, c));
            if (res.ec != std::errc()) {
                collected_issues.add_issue(Read3mfIssue(issue_type, std::to_string(static_cast<int>(res.ec))));
                return Domain::Transform3d::Identity();
            }
        }
    }
    return ret;
}

bool exists(ST_ResourceID id, const CT_Objects &objects) {
    return std::any_of(objects.begin(), objects.end(), 
        [id](const CT_Object &object) {
            return object.id == id;
        });
}

void process_object_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    // invalid object should be threated later
    CT_Objects &objects = model.model->resource.objects;
    objects.emplace_back(); // add new object
    CT_Object& object = objects.back(); 

    bool has_attribute_id = false;
    for (const Attribute &attr : attributes) {
        if (::strcmp(attr.name, ID_ATTR) == 0) {
            has_attribute_id = true;
            ST_ResourceID object_id = 0;
            if (!parse_id(attr.value, object_id))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_object_id_is_invalid,
                    std::string(attr.value)));
            else if (exists(object_id, objects))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_object_id_is_not_unique, 
                    std::string(attr.value)));
            object.id = object_id;
        } else if (::strcmp(attr.name, TYPE_ATTR) == 0) {
            const auto &name_to_type = object_type_to_name.right;
            auto it = name_to_type.find(attr.value);
            if (it == name_to_type.end()) {
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_object_unknown_type, attr.value));
                object.type = std::string(attr.value);
            } else {
                object.type = it->second;
            }
        } else if (::strcmp(attr.name, THUMBNAIL_ATTR) == 0) {
            object.thumbnail = std::string(attr.value);
        } else if (::strcmp(attr.name, PARTNUMBER_ATTR) == 0) {
            object.part_number = std::string(attr.value);
        } else if (::strcmp(attr.name, NAME_ATTR) == 0) {
            object.name = std::string(attr.value);
        } else if (::strcmp(attr.name, PID_ATTR) == 0) {
            object.pid = parse_int(attr.value);
        } else if (::strcmp(attr.name, PINDEX_ATTR) == 0) {
            object.pindex = parse_int(attr.value);
        } else if (!model.model->prod_ns.empty() && 
                   ::strcmp(attr.name, (model.model->prod_ns + ':' + UUID_ATTR).c_str()) == 0) {
            boost::uuids::string_generator gen;
            object.uuid = gen(attr.value);
        } else {
            process_unknown_attr(Read3mfIssueType::model_object_unknown_attr, attr, collected_issues);
        }
    }

    // Production extension REQUIRE uuid for object
    if (!model.model->prod_ns.empty() && object.uuid.is_nil())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_object_missing_uuid));

    // Object MUST contain mesh or component
    if (object.mesh.its.empty() == object.components.empty())
        collected_issues.add_issue(Read3mfIssue(object.components.empty()?
            Read3mfIssueType::model_object_need_mesh_or_component:
            Read3mfIssueType::model_object_cant_contain_mesh_with_component));

    if (!has_attribute_id)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_object_require_id_attr));
}

void process_mirror_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    CT_Objects &objects = model.model->resource.objects;
    
    assert(!objects.empty());
    if (objects.empty())
        return;
 
    CT_MirrorMesh mirror;
    bool has_original_mesh_id = false;
    for (const Attribute &attr : attributes) {
        if (::strcmp(attr.name, ORIGINALMESH_ATTR) == 0) {            
            ST_ResourceID object_id = 0;
            if (!parse_id(attr.value, object_id))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_mesh_mirror_invalid_id, std::string(attr.value)));
            else if (!exists(object_id, objects))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_mesh_mirror_unknown_id, std::string(attr.value)));
            else {
                has_original_mesh_id = true;            
                mirror.original_mesh = object_id;
            }
        } else if (::strcmp(attr.name, N_X_ATTR) == 0) {
            parse_float(attr.value, mirror.nx, collected_issues, Read3mfIssueType::model_mesh_mirror_nx_issue);
        } else if (::strcmp(attr.name, N_Y_ATTR) == 0) {
            parse_float(attr.value, mirror.ny, collected_issues, Read3mfIssueType::model_mesh_mirror_ny_issue);
        } else if (::strcmp(attr.name, N_Z_ATTR) == 0) {
            parse_float(attr.value, mirror.nz, collected_issues, Read3mfIssueType::model_mesh_mirror_nz_issue);
        } else if (::strcmp(attr.name, DISTANCE_ATTR) == 0) {
            parse_float(attr.value, mirror.d, collected_issues, Read3mfIssueType::model_mesh_mirror_d_issue);
        } else {
            process_unknown_attr(Read3mfIssueType::model_mesh_mirror_unknown_attr, attr, collected_issues);
        }
    }

    if (!has_original_mesh_id) {
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_mesh_mirror_required_id));
        return;
    }

    // write mirror into model, mirror is part of last processed object mesh
    objects.back().mesh.mirror_mesh = mirror;
}

void process_build_atts(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    for (const Attribute &attr : attributes) {
        if (!model.model->prod_ns.empty() &&
            ::strcmp(attr.name, (model.model->prod_ns + ':' + UUID_ATTR).c_str()) == 0) {
            boost::uuids::string_generator gen;
            model.model->build.uuid = gen(attr.value);
        } else {
            process_unknown_attr(Read3mfIssueType::model_build_unknown_attr, attr, collected_issues);
        }
    }

    if (!model.model->prod_ns.empty() && model.model->build.uuid.is_nil())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_build_need_uuid));
}

void process_component_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    // current processed object componnets
    CT_Components &components = model.model->resource.objects.back().components;
    components.emplace_back();
    CT_Component &component = components.back();

    const CT_Objects &objects = model.model->resource.objects;
    ST_ResourceID parent_object_id = objects.back().id;
    bool has_attribute_objectid = false;
    for (const Attribute &attr : attributes) {
        if (::strcmp(attr.name, OBJECTID_ATTR) == 0) {
            has_attribute_objectid = true;
            if (!parse_id(attr.value, component.object_id))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_component_bad_objectid,
                    std::string(attr.value)));
            else if (component.object_id == parent_object_id)
                // Self referencing component - need prevent crash made by recursion !!
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_component_has_parent_objectid, 
                    std::to_string(parent_object_id), std::to_string(component.object_id)));
            else if (!exists(component.object_id, objects))
                // TODO: Solve bad order of objects(component is used before definition of object)
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_component_unknown_objectid, 
                    std::to_string(parent_object_id), std::to_string(component.object_id)));            
        } else if (::strcmp(attr.name, TRANSFORM_ATTR) == 0) {
            component.transform = parse_transformation(attr.value, collected_issues, Read3mfIssueType::model_component_bad_transformation_format);
        } else if (!model.model->prod_ns.empty()) {
            if (::strcmp(attr.name, (model.model->prod_ns + ':' + PATH_ATTR).c_str()) == 0) {
                component.path = std::string(attr.value);
            } else if (::strcmp(attr.name, (model.model->prod_ns + ':' + UUID_ATTR).c_str()) == 0) {
                boost::uuids::string_generator gen;
                component.uuid = gen(attr.value);
            }
        } else {
            process_unknown_attr(Read3mfIssueType::model_object_unknown_attr, attr, collected_issues);
        }
    }
    if (!model.model->prod_ns.empty() && component.uuid.is_nil())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_component_require_uuid_attr));

    if (!has_attribute_objectid)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_component_require_objectid_attr,
            std::to_string(parent_object_id)));
}

void process_item_attr(LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    // current processed object componnets
    CT_Items &items = model.model->build.items;
    items.emplace_back();
    CT_Item &item = items.back();

    bool has_attribute_objectid = false;
    for (const Attribute &attr : attributes) {
        if (::strcmp(attr.name, OBJECTID_ATTR) == 0) {
            has_attribute_objectid = true;
            if (!parse_id(attr.value, item.object_id))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_item_bad_objectid, std::string(attr.value)));
            else if (!exists(item.object_id, model.model->resource.objects))
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_item_unknown_objectid));            
        } else if (::strcmp(attr.name, TRANSFORM_ATTR) == 0) {
            item.transform = parse_transformation(attr.value, collected_issues, Read3mfIssueType::model_item_bad_transformation_format);
        } else if (::strcmp(attr.name, PARTNUMBER_ATTR) == 0) {
            item.part_number = std::string(attr.value);
        } else if (!model.model->prod_ns.empty()) {
            if (::strcmp(attr.name, (model.model->prod_ns + ':' + PATH_ATTR).c_str()) == 0) {
                item.path = std::string(attr.value);
            } else if (::strcmp(attr.name, (model.model->prod_ns + ':' + UUID_ATTR).c_str()) == 0) {
                boost::uuids::string_generator gen;
                item.uuid = gen(attr.value);
            }
        } else {
            process_unknown_attr(Read3mfIssueType::model_item_unknown_attr, attr, collected_issues);
        }
    }

    if (!model.model->prod_ns.empty() && item.uuid.is_nil())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_item_require_uuid_attr));

    if (!has_attribute_objectid)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_item_require_objectid_attr));
}

void process_unknown_atts(Read3mfIssueType type, LoadedModelFile &model, const Attributes &attributes, Read3mfIssues& collected_issues) {
    if (attributes.empty()) return;
    for (const Attribute& att: attributes)
        process_unknown_attr(type, att, collected_issues);
}

void start_unknown_tag(LoadContext &context, Read3mfIssueType type, const XML_Char *name, 
    const Attributes& atts, bool is_3mf_allowed = true){    
    context.collected_issues.add_issue(Read3mfIssue(type, std::string(name)));
    assert(context.unknown_depth == 0);
    context.unknown_depth = 1;
    assert(context.unknown_in_state == XmlState::unknown);
    assert(context.state != XmlState::unknown);
    context.unknown_in_state = context.state;
    context.state = XmlState::unknown;
    // TODO: what about attrs? and sub tags?
}

void process_unknown_tag(LoadContext &context, const XML_Char *name, const Attributes& atts){
    assert(context.state == XmlState::unknown);
    assert(context.unknown_in_state != XmlState::unknown);
    assert(context.unknown_depth >= 1);
    ++context.unknown_depth;
}

void process_unknown_end_tag(LoadContext &context){
    assert(context.unknown_depth >= 1);
    assert(context.unknown_in_state != XmlState::unknown);
    --context.unknown_depth;
    if (context.unknown_depth == 0) {
        // return back to known tag
        context.state = context.unknown_in_state;
        context.unknown_in_state = XmlState::unknown;
    }
}

void XMLCALL xml_characters_handler(void *user_data, const XML_Char *s, int len) {
    LoadContext *context_ptr = static_cast<LoadContext *>(user_data);
    assert(context_ptr != nullptr);
    if (context_ptr == nullptr)
        return;

    // protection against too long texts
    if (context_ptr->xml_characters.length() >= 2048)
        return;

    context_ptr->xml_characters.append(s, len);
}

void XMLCALL start_element_handler(void *user_data, const XML_Char *name, const XML_Char **atts);
void XMLCALL start_vertex_handler(void *user_data, const XML_Char *name, const XML_Char **atts) {
    LoadContext *context_ptr = static_cast<LoadContext *>(user_data);
    if (context_ptr == nullptr)
        return start_element_handler(user_data, name, atts);

    LoadContext &context = *context_ptr;
    if (context.xml_parser == nullptr)
        return start_element_handler(user_data, name, atts);

    XmlState &state = context.state;
    assert(state == XmlState::vertices);
    if (state != XmlState::vertices) 
        return start_element_handler(user_data, name, atts);

    if (::strcmp(name, VERTEX_TAG) != 0)
        return start_element_handler(user_data, name, atts);

    state = XmlState::vertex;
    int num_atts = XML_GetSpecifiedAttributeCount(*context.xml_parser);
    process_vertex_atts(context.model, atts, num_atts, context.collected_issues);
}

void XMLCALL start_triangle_handler(void *user_data, const XML_Char *name, const XML_Char **atts) {
    LoadContext *context_ptr = static_cast<LoadContext *>(user_data);
    if (context_ptr == nullptr)
        return start_element_handler(user_data, name, atts);
    LoadContext &context = *context_ptr;
    if (context.xml_parser == nullptr)
        return start_element_handler(user_data, name, atts);

    XmlState &state = context.state;
    assert(state == XmlState::triangles);
    if (::strcmp(name, TRIANGLE_TAG) != 0)
        return start_element_handler(user_data, name, atts);

    state = XmlState::triangle;
    int num_atts = XML_GetSpecifiedAttributeCount(*context.xml_parser);
    process_triangle_atts(context.model, atts, num_atts, context.collected_issues);
}

void XMLCALL start_element_handler(void *user_data, const XML_Char *name, const XML_Char **atts) {
    LoadContext *context_ptr = static_cast<LoadContext *>(user_data);
    // Q: should it stop the parser ?? When it can appear?
    assert(context_ptr != nullptr);
    if (context_ptr == nullptr)
        return;
    LoadContext &context = *context_ptr;
    XmlState &state = context.state;
    LoadedModelFile &model = context.model;
    Read3mfIssues& collected_issues = context.collected_issues;

    // Q: When it can appear?
    assert(context.xml_parser != nullptr);
    if (context.xml_parser == nullptr)
        return;

    // TODO: Process text before start tags
    // NOTE: Not used in Core 3mf.
    context_ptr->xml_characters.clear();
    //if (!context_ptr->xml_characters.empty()) process_xml_characters(*context_ptr);

    int num_atts = XML_GetSpecifiedAttributeCount(*context.xml_parser);
    Attributes attributes = create_attributes(atts, num_atts);
    // xml state machine remember current immersion
    switch (state) {
    case XmlState::start:
        if (::strcmp(name,    MODEL_TAG) == 0) {
            state = XmlState::model;
            process_model_attr(model, attributes, collected_issues);
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_bad_root_tag, name, attributes, false);
        }
        break;
    case XmlState::model:
        if (::strcmp(name,    METADATA_TAG) == 0) {
            state = XmlState::metadata;
            // Parse character between tags
            XML_SetCharacterDataHandler(*context_ptr->xml_parser, ::xml_characters_handler);
            process_metadata_attr(model, attributes, collected_issues);
        } else if (::strcmp(  RESOURCES_TAG, name) == 0) {
            state = XmlState::resources;
            if (context.exist_model_resources)
                context.collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_resource_multiple_appear));
            context.exist_model_resources = true;
            process_unknown_atts(Read3mfIssueType::model_resource_unknown_attr, model, attributes, collected_issues);
        } else if (::strcmp(  BUILD_TAG, name) == 0) {
            state = XmlState::build;
            if (context.exist_model_build)
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_build_multiple_appear));
            context.exist_model_build = true;
            process_build_atts(model, attributes, collected_issues);
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_unknown_tag, name, attributes);
        }
        break;
    case XmlState::resources:
        if (::strcmp(name,    OBJECT_TAG) == 0) {
            state = XmlState::object;
            process_object_attr(model, attributes, context.collected_issues);
        //} else if (::strcmp(  BASEMATERIALS_TAG, name) == 0) {
        //    state = XmlState::basematerials;
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_resources_unknown_tag, name, attributes);
        }
        break;
    case XmlState::object:
        if (::strcmp(name,    MESH_TAG) == 0) {
            state = XmlState::mesh;
            process_unknown_atts(Read3mfIssueType::model_mesh_unknown_attr, model, attributes, collected_issues);
        } else if (::strcmp(  COMPONENTS_TAG, name) == 0) {
            state = XmlState::components;
            process_unknown_atts(Read3mfIssueType::model_components_unknown_attr, model, attributes, collected_issues);
        //} else if (::strcmp(METADATAGROUP_TAG, name) == 0) {
        //    state = XmlState::metadatagroup;
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_object_unknown_tag, name, attributes, false);
        }
        break;
    case XmlState::mesh:
        if (::strcmp(name,    VERTICES_TAG) == 0) {
            state = XmlState::vertices;
            process_unknown_atts(Read3mfIssueType::model_vertices_unknown_attr, model, attributes, collected_issues);
            XML_SetStartElementHandler(*context.xml_parser, start_vertex_handler);
            // Note: start element is processed by 'start_vertex_handler' function til </vertices>
        } else if (::strcmp(  TRIANGLES_TAG, name) == 0) {
            state = XmlState::triangles;
            process_unknown_atts(Read3mfIssueType::model_triangles_unknown_attr, model, attributes, collected_issues);
            XML_SetStartElementHandler(*context.xml_parser, start_triangle_handler);
            // Note: start element is processed by 'start_triangle_handler' function til </triangles>

        } else if (!model.model->mirror_ns.empty() && 
            ::strcmp((model.model->mirror_ns + ':' + MIRRORMESH_TAG).c_str(), name) == 0) {
            state = XmlState::mirrormesh;
            process_mirror_attr(model, attributes, collected_issues);
        //} else if (::strcmp(  TRIANGLESETS_TAG, name) == 0) {
        //    state = XmlState::trianglesets;
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_mesh_unknown_tag, name, attributes);
        }
        break;
    case XmlState::components:
        if (::strcmp(name,    COMPONENT_TAG) == 0) {
            state = XmlState::component;
            process_component_attr(model, attributes, collected_issues);
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_components_unknown_tag, name, attributes, false);
        }
        break;
    case XmlState::build:
        if (::strcmp(name,    ITEM_TAG) == 0) {
            state = XmlState::item;
            process_item_attr(model, attributes, collected_issues);
        } else {
            start_unknown_tag(context, Read3mfIssueType::model_build_unknown_tag, name, attributes, false);
        }
        break;
    case XmlState::triangles:
        // common states is proccess by start_triangle_handler
        start_unknown_tag(context, Read3mfIssueType::model_triangles_unknown_tag, name, attributes, false);
        break;
    case XmlState::vertices: 
        // common states is proccess by start_vertices_handler
        start_unknown_tag(context, Read3mfIssueType::model_vertices_unknown_tag, name, attributes, false);
        break;
    case XmlState::triangle: [[fallthrough]];
    case XmlState::vertex: [[fallthrough]];
    case XmlState::metadata:
        // This tags shoudl be self closing: without content
        start_unknown_tag(context, Read3mfIssueType::model_unknown_tag, name, attributes, false);    
        break;
    case XmlState::unknown: process_unknown_tag(context, name, attributes); break;
    default:
        // There should not be unporcessed state
        assert(false);
    }
}

bool is_old_stored_version(const ModelMetadata &meta) {
    if (!std::holds_alternative<ModelMetadataNames>(meta.name))
        return false;
    if (std::get<ModelMetadataNames>(meta.name) != ModelMetadataNames::Application)
        return false;
    if (!boost::starts_with(meta.value, "PrusaSlicer-"))
        return false;
    auto version = Semver::parse(meta.value.substr(12));
    if (!version.has_value())
        return false;
    if (*version > last_old_stored_version)
        return false;
    return true;
}

void process_xml_characters(LoadContext& context) {
    const std::string& text = context.xml_characters;

    // Is text whitespaces only?
    if(std::all_of(text.begin(), text.end(), isspace))
        return context.xml_characters.clear();

    XmlState state = context.state;
    switch (state) {
    case XmlState::metadata: {   
        ModelMetadata &metadata = context.model.model->metadata.back();
        metadata.value = text; // copy
        if (is_old_stored_version(metadata)) {
            context.collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::legacy_loader_required));
            // Abort parsing after detect old 3mf
            // stop expat xml pareser
            XML_Bool resumable = false;
            // XML_Status status =
            XML_StopParser(*context.xml_parser, resumable);
        }
        break;
    }        
    case XmlState::unknown:
        context.collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_unknown_xml_characters, 
            std::to_string(static_cast<int>(context.unknown_in_state)),
            text, context.unknown_depth));
        break;
    default:
        context.collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_unexpected_xml_characters,
            std::to_string(static_cast<int>(state)), text));
    }
    // clear temp strig and prepare to next one.
    context.xml_characters.clear();
}

void XMLCALL end_element_handler(void *user_data, const XML_Char *name) {
    LoadContext *context_ptr = static_cast<LoadContext *>(user_data);
    assert(context_ptr != nullptr);
    if (context_ptr == nullptr)
        return;
    assert(context_ptr->xml_parser != nullptr);
    if (context_ptr->xml_parser == nullptr)
        return;
    XML_Parser &xml_parser = *context_ptr->xml_parser;

    // Process text between tags
    if (!context_ptr->xml_characters.empty())
        process_xml_characters(*context_ptr);

    XmlState &state = context_ptr->state;
    switch (state) {
    case XmlState::model:        state = XmlState::finished;  assert(::strcmp(name, MODEL_TAG) == 0);         break;
    case XmlState::resources:    state = XmlState::model;     assert(::strcmp(name, RESOURCES_TAG) == 0);     break;
    case XmlState::basematerials:state = XmlState::resources; assert(::strcmp(name, BASEMATERIALS_TAG) == 0); break;
    case XmlState::object:       state = XmlState::resources; assert(::strcmp(name, OBJECT_TAG) == 0);        break;
    case XmlState::metadatagroup:state = XmlState::object;    assert(::strcmp(name, METADATAGROUP_TAG) == 0); break;
    case XmlState::mesh:         state = XmlState::object;    assert(::strcmp(name, MESH_TAG) == 0);          break;
    case XmlState::vertex:       state = XmlState::vertices;  assert(::strcmp(name, VERTEX_TAG) == 0);        break;
    case XmlState::triangle:     state = XmlState::triangles; assert(::strcmp(name, TRIANGLE_TAG) == 0);      break;
    case XmlState::trianglesets: state = XmlState::mesh;      assert(::strcmp(name, TRIANGLESETS_TAG) == 0);  break;
    case XmlState::mirrormesh:   state = XmlState::mesh;      assert(::strcmp(name, MIRRORMESH_TAG) == 0);    break;
    case XmlState::components:   state = XmlState::object;    assert(::strcmp(name, COMPONENTS_TAG) == 0);    break;
    case XmlState::component:    state = XmlState::components;assert(::strcmp(name, COMPONENT_TAG) == 0);     break;
    case XmlState::build:        state = XmlState::model;     assert(::strcmp(name, BUILD_TAG) == 0);         break;
    case XmlState::item:         state = XmlState::build;     assert(::strcmp(name, ITEM_TAG) == 0);          break;        
    case XmlState::vertices:     state = XmlState::mesh;      assert(::strcmp(name, VERTICES_TAG) == 0);  XML_SetStartElementHandler(xml_parser, start_element_handler); break;
    case XmlState::triangles:    state = XmlState::mesh;      assert(::strcmp(name, TRIANGLES_TAG) == 0); XML_SetStartElementHandler(xml_parser, start_element_handler); break;
    case XmlState::metadata:     state = XmlState::model;     assert(::strcmp(name, METADATA_TAG) == 0);  XML_SetCharacterDataHandler(xml_parser, NULL); break; // stop parsing characters between tags
    case XmlState::unknown: process_unknown_end_tag(*context_ptr); break;
    case XmlState::start: [[fallthrough]];
    case XmlState::finished: [[fallthrough]];
    default:
        assert(false);
    }

}

tl::expected<LoadedModelFile, Read3mfIssue> read_modelfile(mz_zip_archive &archive, int model_file_index, Read3mfIssues& collected_issues) {
    // mz_zip_archive_file_stat stat;
    // if (!mz_zip_reader_file_stat(&archive, model_file_index, &stat))
    //     return Read3mfIssueType::cant_read_model_stats;
    mz_uint flags = 0;
    mz_zip_reader_extract_iter_state *iterator_ptr =
        mz_zip_reader_extract_iter_new(&archive, model_file_index, flags);
    if (iterator_ptr == nullptr)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::cant_extract_iterator));

    // NULL if there is none specified
    const XML_Char *encoding = nullptr;
    XML_Parser xml_parser = XML_ParserCreate(encoding);
    if (xml_parser == nullptr)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::expat_cant_create_parser));

    ScopeGuard sg_parser([&xml_parser]() { XML_ParserFree(xml_parser); });
    Slic3r::Biz::Algorithms::MzIterType file_data_iterator(iterator_ptr);

    // Parser data itss
    LoadContext context{&xml_parser, collected_issues};
    XML_SetUserData(xml_parser, (void *) &context);
    XML_SetElementHandler(xml_parser, ::start_element_handler, ::end_element_handler);

    bool done = false;
    size_t nbyte = BUFSIZ * BUFSIZ;
    do {
        void *const buf = XML_GetBuffer(xml_parser, (int) nbyte);
        if (buf == nullptr)
            return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::expat_cant_create_buffer));

        size_t readed = mz_zip_reader_extract_iter_read(file_data_iterator.get(), buf, nbyte);

        // last readed chunk of data has different size
        done = (readed != nbyte);

        XML_Status status = XML_ParseBuffer(xml_parser, (int) readed, done);
        if (status == XML_STATUS_ERROR) {
            if (context.collected_issues.has_issue(Read3mfIssueType::legacy_loader_required))
                return context.model;
            XML_Size line_number = XML_GetCurrentLineNumber(xml_parser);
            XML_Error error_code = XML_GetErrorCode(xml_parser);
            std::string error_code_str = std::to_string(static_cast<int>(error_code));
            std::string error_message = XML_ErrorString(error_code);
            return tl::make_unexpected(Read3mfIssue(
                Read3mfIssueType::expat_parse_error, std::nullopt, error_message + " (" + error_code_str + ")", line_number)
            );
        }
    } while (!done);

    if (!context.exist_model_resources)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_resource_missing));
    // TODO: Should I remove invalid model?

    if (!context.exist_model_build)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::model_build_missing));

    return context.model;
}

bool is_valid_sub_model(const format_3MF::Model &model_3mf) {
    // MUST be production extension - with UUIDs
    // MUST not contain path
    // MUST not contain build
    if (model_3mf.prod_ns.empty())
        return false;
    for (const format_3MF::CT_Object &object : model_3mf.resource.objects) {       
        for (const format_3MF::CT_Component &componnent : object.components) {
            if (!componnent.path.empty())
                return false;
        }
    }
    if (!model_3mf.build.items.empty())
        return false;
    return true;
}

void append_sub_model(mz_zip_archive &archive, const std::string &filepath, LoadedModel& result, Read3mfIssues& collected_issues) {
    assert(!filepath.empty());
    if (filepath.empty())
        return; // invalid path

    if (result.sub_models.find(filepath) != result.sub_models.end())
        return; // already loaded

    const char *zip_path = filepath.c_str();
    if (*zip_path == '\\' || *zip_path == '/')
        ++zip_path;

    int file_index = mz_zip_reader_locate_file(&archive, zip_path, nullptr, 0);
    if (file_index < 0) {
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::unable_to_locate_model_file, filepath));
        return;
    }
    result.used_files[file_index] = true;

    tl::expected<LoadedModelFile, Read3mfIssue> sub_model = read_modelfile(archive, file_index, collected_issues);
    if (! sub_model || !sub_model.value().model.has_value())
        return;

    if (!is_valid_sub_model(*sub_model.value().model)) {
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::sub_model_issue, filepath));
        return;
    }

    // MUST not contain path
    // MUST be production extension - with UUIDs
    // MUST not contain build
    result.sub_models.emplace(filepath, *sub_model.value().model);
}

} // namespace

LoadedModel Slic3r::read_model3mf(mz_zip_archive &archive, const char * root_file_path, Read3mfIssues& collected_issues){
    int root_model_file_index = mz_zip_reader_locate_file(&archive, root_file_path, nullptr, 0);
    if (root_model_file_index < 0) {
        // 3mf without .model file is invalid.
        throw Slic3r::Loaded3MFException(Read3mfIssue(Read3mfIssueType::unable_to_locate_model_file, root_file_path));
    }

    tl::expected<LoadedModelFile, Read3mfIssue> root_model = read_modelfile(archive, root_model_file_index, collected_issues);

    LoadedModel result;
    if (root_model)
        result.model = std::move(*root_model.value().model);
    result.used_files = std::vector<bool>(mz_zip_reader_get_num_files(&archive), {false});
    result.used_files[root_model_file_index] = true;

    if (!result.model.has_value() || collected_issues.has_issue(Read3mfIssueType::legacy_loader_required))
        return result;

    // Read model from .model addresed by path(production extension)
    const format_3MF::Model &root_model_3mf = *result.model;
    for (const format_3MF::CT_Object &object: root_model_3mf.resource.objects) {
        for (const format_3MF::CT_Component &componnent : object.components) {
            if (componnent.path.empty())
                continue; // component do not use optional path
            append_sub_model(archive, componnent.path, result, collected_issues);
        }
    }
    for (const format_3MF::CT_Item &item : root_model_3mf.build.items) {
        if (item.path.empty())
            continue; // item do not use optional path
        append_sub_model(archive, item.path, result, collected_issues);
    }
    return result;
}

// May be move write function into separate file because of include Model.hpp !!!



// write to 3mf - help functions
namespace{

using namespace Slic3r;

// Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point formatter,
// https://github.com/boostorg/spirit/pull/586
// where the exported string is one digit shorter than it should be to guarantee lossless round trip.
// The code is left here for the ocasion boost guys improve.
#define EXPORT_3MF_USE_SPIRIT_KARMA_FP 0    
#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
template<typename Num> struct coordinate_policy_fixed : boost::spirit::karma::real_policies<Num>
{
    static int floatfield(Num n) { return fmtflags::fixed; }
    // Number of decimal digits to maintain float accuracy when storing into a text file and parsing back.
    static unsigned precision(Num /* n */) { return std::numeric_limits<Num>::max_digits10 + 1; }
    // No trailing zeros, thus for fmtflags::fixed usually much less than max_digits10 decimal
    // numbers will be produced.
    static bool trailing_zeros(Num /* n */) { return false; }
};
template<typename Num> struct coordinate_policy_scientific : coordinate_policy_fixed<Num>
{
    static int floatfield(Num n) { return fmtflags::scientific; }
};
// Define a new generator type based on the new coordinate policy.
using coordinate_type_fixed =
    boost::spirit::karma::real_generator<float, coordinate_policy_fixed<float>>;
using coordinate_type_scientific =
    boost::spirit::karma::real_generator<float, coordinate_policy_scientific<float>>;
#endif // EXPORT_3MF_USE_SPIRIT_KARMA_FP

char * format_coordinate(float f, char *buf)
{
    assert(is_decimal_separator_point());
#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
    // Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point
    // formatter, https://github.com/boostorg/spirit/pull/586 where the exported string is one
    // digit shorter than it should be to guarantee lossless round trip. The code is left here
    // for the ocasion boost guys improve.
    coordinate_type_fixed const coordinate_fixed = coordinate_type_fixed();
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

struct ZipSink {
    std::string *buffer = nullptr;
    mz_uint32 *running_crc = nullptr;
};

void write_chunk(ZipSink &sink, const char *data, size_t n)
{
    sink.buffer->append(data, n);
    *sink.running_crc = (mz_uint32) mz_crc32(*sink.running_crc, (const mz_uint8*) data, n);
}

// Appends a legacy slic3rpe:mmu_segmentation attribute for one triangle, if painted. Written
// in addition to Metadata/Slic3r_facets_annotation.json so 2.x readers, which never look at
// that JSON, still see MM segmentation (see store_meshes() for when this is called at all).
void append_mm_segmentation_attr(const Domain::FacetsAnnotation &facets, int triangle_idx, std::string &output_buffer)
{
    std::string data = facets.get_triangle_as_string(triangle_idx);
    if (data.empty())
        return;
    output_buffer += " ";
    output_buffer += Legacy::MM_SEGMENTATION_ATTR;
    output_buffer += "=\"";
    output_buffer += data;
    output_buffer += "\"";
}

// mm_segmentation_facets is null when dual-writing is off (see store_model3mf), or non-null
// but still produces no attribute for a given triangle when that triangle isn't painted.
void store_geometry(ZipSink &sink, const indexed_triangle_set &its, std::string &output_buffer,
    bool is_mirrored = false, const Domain::FacetsAnnotation *mm_segmentation_facets = nullptr)
{
    ASSERT(its.vertices.size() >= 4);
    ASSERT(! its.indices.empty());

    output_buffer += std::string() +
        "   <" + MESH_TAG + ">\n" + 
        "    <" + VERTICES_TAG + ">\n";

    auto flush = [&output_buffer, &sink]() {
        if (output_buffer.size() >= 65536 * 16) {
            write_chunk(sink, output_buffer.data(), output_buffer.size());
            output_buffer.clear();
        }
    };

    char buf[256];
    for (const Domain::Vec3f &v : its.vertices) {
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
        flush();
    }

    output_buffer += std::string() + 
        "    </" + VERTICES_TAG + ">\n" +
        "    <" + TRIANGLES_TAG + ">\n";    

    size_t vertices_count = its.vertices.size();

    // keep count of addressing of vertex in triangles
    std::vector<unsigned> is_vertex_used;
    auto unused_vertex_write = [&is_vertex_used, vertices_count](const Domain::Index3& t) { 
        if (is_vertex_used.empty()){
            // first call of function initialize vector
            is_vertex_used = std::vector<unsigned>(vertices_count, {0});
        }
        for (int ti : t)
            ++is_vertex_used[ti];
        return true; // return bool to be possible check only in debug
    };

    auto is_valid = [vertices_count, &unused_vertex_write](const Domain::Index3 &t) {
        // check unused vertex - each vertex SHOULD be addressed at least 3 times
        assert(unused_vertex_write(t));

        // check range of vertex adress
        assert(t[0] >= 0 && t[0] < vertices_count);
        assert(t[1] >= 0 && t[1] < vertices_count);
        assert(t[2] >= 0 && t[2] < vertices_count);
        return t[0] >= 0 && t[0] < vertices_count &&
               t[1] >= 0 && t[1] < vertices_count &&
               t[2] >= 0 && t[2] < vertices_count;
    };

    // Fills the fixed buffer via the fast, unchecked karma generator - only safe because
    // v1/v2/v3 are plain integers with a small bounded text length.
    auto triangle_to_string = [&buf](int t0, int t1, int t2){
        char *ptr = buf;
        boost::spirit::karma::generate(ptr, boost::spirit::lit("     <")
                << TRIANGLE_TAG << " v1=\"" << boost::spirit::int_ << "\" v2=\""
                << boost::spirit::int_ << "\" v3=\"" << boost::spirit::int_ << "\"",
            t0, t1, t2);
        *ptr = '\0'; // TODO: try to add it into spirit
        return buf;
    };

    // Writes one complete <triangle .../> element, including the optional legacy MM
    // segmentation attribute. That attribute can't be generated into the fixed buffer above:
    // its encoded length is unbounded (a recursively split triangle's bitstream can be
    // arbitrarily long), unlike the small, bounded v1/v2/v3 integers.
    auto write_triangle = [&](int t0, int t1, int t2, int triangle_idx) {
        output_buffer += triangle_to_string(t0, t1, t2);
        if (mm_segmentation_facets)
            append_mm_segmentation_attr(*mm_segmentation_facets, triangle_idx, output_buffer);
        output_buffer += " />\n";
    };

    // Triangle index passed to write_triangle matches FacetsAnnotation's indexing (position
    // within its.indices), same convention used to key Slic3r_facets_annotation.json.
    int triangle_idx = 0;

    // condition SHOULD be before loop(which could be HUGE)
    if (!is_mirrored) {
        for (const Domain::Index3& t : its.indices) {
            // Disallowe storing of negative idices or bigger than vertices count
            if (!is_valid(t)) { ++triangle_idx; continue; }
            write_triangle(t[0], t[1], t[2], triangle_idx);
            ++triangle_idx;
            flush();
        }
    } else {
        // same as above only indices 2 and 0 are swaped to invert volume
        for (const Domain::Index3& t : its.indices) {
            if (!is_valid(t)) { ++triangle_idx; continue; }
            write_triangle(t[2], t[1], t[0], triangle_idx);
            ++triangle_idx;
            flush();
        }
    }
    
    // check unused vertex - each vertex SHOULD be addressed at least 3 times
    // otherwise there is bad volume
    auto is_each_vertex_used_three_times = [&is_vertex_used]() {
        for (unsigned count: is_vertex_used)
            if (count < 3)
                return false;
        return true;
    };

    // 3md SHOULD contain water tide meshes only:
    assert(is_each_vertex_used_three_times());
    assert(Slic3r::Biz::Algorithms::TriangleMesh::its_get_open_edges(its).empty());

    output_buffer += std::string() +
        "    </" + TRIANGLES_TAG + ">\n" +
        "   </" + MESH_TAG + ">\n";
}

// write volume geometry as object into model file
void store_resource_object_geometry(ZipSink &sink, const indexed_triangle_set &its, unsigned object_id,
    const Domain::FacetsAnnotation *mm_segmentation_facets = nullptr)
{
    std::string output_buffer;
    output_buffer += std::string() + "  <" + OBJECT_TAG + " " + ID_ATTR + "=\"" + std::to_string(object_id) + "\" >\n";
    store_geometry(sink, its, output_buffer, false, mm_segmentation_facets);
    output_buffer += std::string() + "  </" + OBJECT_TAG + ">\n";
    write_chunk(sink, output_buffer.data(), output_buffer.size());
}

void write_to_context(ZipSink &sink, std::stringstream& stream){
    // C++ 20 implementation
    // https://stackoverflow.com/questions/69299784/can-i-get-the-raw-pointer-for-a-stdstringstream-accumulated-data-with-0-copy
    //auto view = stream.view();
    //if (view.size()>0 && !mz_zip_writer_add_staged_data(&context, view.data(), view.size()))
    //    throw boost::filesystem::filesystem_error("Unable to add model file to archive.", {});

    // https://stackoverflow.com/questions/4432793/size-of-stringstream
    // https://en.cppreference.com/w/cpp/io/basic_stringstream/rdbuf

    // TODO: do not copy stream data
    std::string buf = stream.str();
    if (!buf.empty()) {
        write_chunk(sink, buf.data(), buf.size());
    }
}

void write_xml_commnet(std::stringstream &stream, std::string_view message, 
    std::string_view indent = "  "){
    stream << indent << "<!-- " << message << " -->\n";
}

void add_transformation(std::stringstream &stream, const Domain::Transform3d &tr) {
    // https://en.cppreference.com/w/cpp/types/numeric_limits/max_digits10
    // Conversion of a floating-point value to text and back is exact as long as at least
    // max_digits10 were used (9 for float, 17 for double). It is guaranteed to produce the same
    // floating-point value, even though the intermediate text representation is not exact. The
    // default value of std::stream precision is 6 digits only!
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (unsigned c = 0; c < 4; ++c) {
        for (unsigned r = 0; r < 3; ++r) {
            stream << tr(r, c);
            if (r != 2 || c != 3) // not the last one
                stream << " ";
        }
    }

    // The matrix SHOULD NOT be singular or nearly singular.
    // NonSingular is also known as invertible matrix and have non zero determinant
    assert(!is_approx(tr.matrix().determinant(), 0.));

    // After applying all transforms to an object,
    // the model SHOULD have positive volume 
    // and SHOULD be located in the positive octant of the coordinate space.
}

bool is_identity(const Domain::Transform3d &tr){
    // In Eigen 3.4.9 is possible to chekck identity directly by:
    // tr.isIdentity();
    return (tr.matrix() - SquareMatrix4d::Identity()).cwiseAbs().maxCoeff() < 1e-10;
    //return is_approx((Vec3d) (tr * Vec3d::UnitX()), Vec3d::UnitX()) && 
    //       is_approx((Vec3d) (tr * Vec3d::UnitY()), Vec3d::UnitY()) &&
    //       is_approx((Vec3d) (tr * Vec3d::UnitZ()), Vec3d::UnitZ());
    // 
    // NOTE: 1m long slab rotate with 0.1mm --> 0.1/500 = 2e-4 deg -> sin(a) = 3.5e-6; 1-cos(a) = 6e-12
    // So preccision under 1e-6 should be enough
}

// When transformation is close to identity than do not write anything
void add_transformation_attr(std::stringstream &stream, const Domain::Transform3d &tr){
    if (is_identity(tr))
        return; // not necessary to storre identity
    stream << TRANSFORM_ATTR << "=\"";
    add_transformation(stream, tr);
    stream << "\" ";
}

void store_component(std::stringstream& stream, unsigned objectid,
     /*const ST_UUID& uuid,*/ const Domain::Transform3d* tr = nullptr) {
    //assert(!uuid.is_nil());
    stream << "    <" << COMPONENT_TAG << " " 
        << OBJECTID_ATTR << "=\"" << objectid << "\" ";
    //  << PROD_NS << UUID_ATTR<< "=\"" << uuid << "\" ";
    if (tr != nullptr)
        add_transformation_attr(stream, *tr);
    stream << "/>\n";
}

void store_component(std::stringstream &stream, unsigned objectid, 
    /*const ST_UUID& uuid, */const std::string& path) {
    //assert(!uuid.is_nil());
    stream << "    <" << COMPONENT_TAG << " " << 
        OBJECTID_ATTR << "=\"" << objectid << "\" " <<
    //  PROD_NS << PATH_ATTR<< "=\"" << path << "\" " <<
    //  PROD_NS << UUID_ATTR<< "=\"" << uuid << "\" " <<
        "/>\n";
}

bool is_valid_object(const ModelObject *object_ptr) 
{
    if (object_ptr == nullptr)
        return false;
    const ModelObject &object = *object_ptr;
    if (object.volumes.empty())
        return false;
    if (object.instances.empty())
        return false;
    return true;
};

const TriangleMesh * get_mesh_ptr(const ModelVolume *volume_ptr)
{
    if (volume_ptr == nullptr)
        return nullptr;

    const ModelVolume &volume = *volume_ptr;
    if (volume.mesh_ptr() == nullptr)
        return nullptr;

    return volume.mesh_ptr().get();
};

MeshToObjectid store_meshes(ZipSink& sink, const Slic3r::Domain::Model &model,
    unsigned &object_id, bool dual_write_mm_segmentation) {
    MeshToObjectid stored_meshes;
    for (const ModelObject *object_ptr : model.objects) {
        if (!is_valid_object(object_ptr))
            continue;
        for (const ModelVolume *volume_ptr : object_ptr->volumes) {
            const TriangleMesh *mesh_ptr = get_mesh_ptr(volume_ptr);
            if (mesh_ptr == nullptr
             || mesh_ptr->its.vertices.size() < 4
             || mesh_ptr->its.indices.empty())
                continue;
            if (auto it = stored_meshes.find(mesh_ptr);
                it != stored_meshes.end())
                // already stored volume geometry
                continue;
            // Null means "don't dual-write": either off for the whole file, or this particular
            // volume isn't MM-painted (append_mm_segmentation_attr would just no-op anyway,
            // but skip it outright to avoid a get_triangle_as_string() call per triangle).
            const Domain::FacetsAnnotation *mm_segmentation_facets =
                (dual_write_mm_segmentation && !volume_ptr->mm_segmentation_facets.empty()) ?
                    &volume_ptr->mm_segmentation_facets : nullptr;
            store_resource_object_geometry(sink, mesh_ptr->its, object_id, mm_segmentation_facets);
            stored_meshes[mesh_ptr] = {object_id};
            ++object_id;
        }
    }
    return stored_meshes;
}

enum class ZipBackend
{
    Compressed, // libdeflate
    Uncompressed, // stored, no compression
};

class ZipContextScoped
{
public:
    ZipContextScoped(
        mz_zip_archive& archive,
        const char* filepath,
        bool zip64,
        ZipBackend backend
    ) :
        m_backend(backend),
        m_archive(archive),
        m_filepath(filepath),
        m_zip64(zip64)
    {
        m_buffer = std::make_unique<std::string>();
        m_sink   = ZipSink{m_buffer.get(), &m_running_crc};
    }

    ~ZipContextScoped()
    {
        if (m_backend == ZipBackend::Uncompressed) {
            ASSERT(finish_uncompressed());
        } else {
            ASSERT(finish_compressed());
        }
    }

    ZipSink& sink()
    {
        return m_sink;
    }

private:
    // mz_zip_writer_add_mem_ex_v2() has no max_size parameter; enforce the same
    // non-zip64 4GiB-1 interop limit manually so param.zip64's intent still holds.
    bool exceeds_non_zip64_limit() const
    {
        return !m_zip64 && m_buffer->size() > (uint64_t(1) << 32) - 1;
    }

    bool finish_uncompressed()
    {
        if (exceeds_non_zip64_limit()) {
            return false;
        }
        // level_and_flags = 0 (no compression, no MZ_ZIP_FLAG_COMPRESSED_DATA): miniz
        // auto-computes size/CRC and writes this entry with method "stored".
        return mz_zip_writer_add_mem_ex_v2(
            &m_archive,
            m_filepath.c_str(),
            m_buffer->data(),
            m_buffer->size(),
            nullptr,
            0,
            /*level_and_flags=*/0,
            0,
            0,
            nullptr,
            nullptr,
            0,
            nullptr,
            0
        );
    }

    bool finish_compressed()
    {
        if (exceeds_non_zip64_limit()) {
            return false;
        }

        libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
        if (compressor == nullptr) {
            return false;
        }
        ScopeGuard sg_compressor([compressor]() { libdeflate_free_compressor(compressor); });

        size_t bound = libdeflate_deflate_compress_bound(compressor, m_buffer->size());
        std::string compressed(bound, '\0');
        // Raw DEFLATE (not the zlib/gzip-wrapped variants) -- this is what a
        // MZ_ZIP_FLAG_COMPRESSED_DATA entry with method MZ_DEFLATED expects.
        size_t compressed_size = libdeflate_deflate_compress(
            compressor,
            m_buffer->data(),
            m_buffer->size(),
            compressed.data(),
            compressed.size()
        );
        if (compressed_size == 0) {
            return false;
        }
        compressed.resize(compressed_size);

        return mz_zip_writer_add_mem_ex_v2(
            &m_archive,
            m_filepath.c_str(),
            compressed.data(),
            compressed.size(),
            nullptr,
            0,
            MZ_ZIP_FLAG_COMPRESSED_DATA,
            m_buffer->size(),
            m_running_crc,
            nullptr,
            nullptr,
            0,
            nullptr,
            0
        );
    }

    ZipBackend m_backend;
    mz_zip_archive& m_archive;
    std::string m_filepath;
    bool m_zip64 = true;
    mz_uint32 m_running_crc = MZ_CRC32_INIT;
    std::unique_ptr<std::string> m_buffer;
    ZipSink m_sink{};
};

const std::string separated_mesh_model_file_prefix(
R""""(<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">
 <resources>
)"""");
const std::string separated_mesh_model_file_tail(
R""""( </resources>
 <build/>
</model>

)"""");

// function to store meshes into separate file
MeshToObjectid store_separate_meshes(
    mz_zip_archive& archive,
    const Slic3r::Domain::Model& model,
    bool zip64,
    unsigned& object_id,
    ZipBackend backend
)
{
    const std::string filepath_prefix = "3D/Objects/mesh_";

    int file_counter = 0;
    MeshToObjectid stored_meshes;
    for (const ModelObject *object_ptr : model.objects) {
        assert(is_valid_object(object_ptr));
        if (!is_valid_object(object_ptr))
            continue;
        for (const ModelVolume *volume_ptr : object_ptr->volumes) {
            const TriangleMesh *mesh_ptr = get_mesh_ptr(volume_ptr);
            ASSERT(mesh_ptr != nullptr);
            if (mesh_ptr->its.vertices.size() < 4
             || mesh_ptr->its.indices.empty())
                continue;
            if (auto it = stored_meshes.find(mesh_ptr); it != stored_meshes.end())
                // already stored volume geometry
                continue;

            const std::string filepath = filepath_prefix + std::to_string(++file_counter) + ".model";
            ZipContextScoped context(archive, filepath.c_str(), zip64, backend);

            write_chunk(context.sink(), separated_mesh_model_file_prefix.data(), separated_mesh_model_file_prefix.size());

            store_resource_object_geometry(context.sink(), mesh_ptr->its, object_id);
            // In 3mf filepath starts with path separator
            stored_meshes[mesh_ptr] = {object_id, "/" + filepath };
            ++object_id;

            write_chunk(context.sink(), separated_mesh_model_file_tail.data(), separated_mesh_model_file_tail.size());

        }
    }
    return stored_meshes;
}

VolumeToObjectid write_volumes(std::stringstream &stream, const Slic3r::Domain::Model &model, unsigned &object_id,
    const MeshToObjectid& stored_mesh) {
    write_xml_commnet(stream, "List of PrusaSlic3r:ModelVolume contian reference on mesh + volume name");        
    VolumeToObjectid stored_volumes;
    for (const ModelObject *object_ptr : model.objects) {
        if (!is_valid_object(object_ptr))
            continue;
        for (const ModelVolume *volume_ptr : object_ptr->volumes) {
            const TriangleMesh *mesh_ptr = get_mesh_ptr(volume_ptr);
            if (mesh_ptr == nullptr)
                continue;

            auto it = stored_mesh.find(mesh_ptr);
            if (it == stored_mesh.end())
                continue;

            const ObjectIdWithPath &id_path = it->second;
            unsigned mesh_id = id_path.id;
            const std::string &mesh_path = id_path.path;
            //assert(g_load_from_3mf);
            //if (g_load_from_3mf == nullptr)
            //    continue;

            //const VolumesWithUUID &volumes_uuid = g_load_from_3mf->volumes_uuid;
            //auto volume_uuid_it = std::find_if(volumes_uuid.begin(), volumes_uuid.end(),
            //    [id = volume_ptr->id().id](const VolumeWithUUID &v_uuid) {
            //        return v_uuid.volume_id == id;
            //    }
            //);

            //assert(volume_uuid_it != volumes_uuid.end());
            //if (volume_uuid_it == volumes_uuid.end())
            //    continue;

            //assert(!volume_uuid_it->object_uuid.is_nil() && 
            //       !volume_uuid_it->componnent_uuid.is_nil());
            //if (volume_uuid_it->object_uuid.is_nil() ||
            //    volume_uuid_it->componnent_uuid.is_nil())
            //    continue;

            stream << "  <" << OBJECT_TAG << " "
                << ID_ATTR << "=\"" << object_id << "\" "
                << NAME_ATTR << "=\"" << Slic3r::Biz::Utils::xml_escape(volume_ptr->name) + "\" "
            //  << PROD_NS << UUID_ATTR<< "=\"" << volume_uuid_it->object_uuid << "\" " 
                << ">\n";
            stored_volumes[volume_ptr] = object_id;
            ++object_id;

            stream << "   <" << COMPONENTS_TAG << ">\n";
            store_component(stream, mesh_id, /*volume_uuid_it->componnent_uuid,*/ mesh_path);
            stream << "   </" << COMPONENTS_TAG << ">\n";
            stream << "  </" << OBJECT_TAG << ">\n";
        }
    }
    return stored_volumes;
}

ObjectToObjectid write_objects(std::stringstream &stream, const Slic3r::Domain::Model &model, 
    unsigned &object_id, const VolumeToObjectid &stored_volumes)
{
    write_xml_commnet(stream, "List of PrusaSlicer:ModelObject(with object name) contain 1+ references on PrusaSlic3r:ModelVolume (with volume transformation)");
    ObjectToObjectid stored_objects;
    for (const ModelObject *object_ptr : model.objects) {
        if (!is_valid_object(object_ptr))
            continue;

        //assert(g_load_from_3mf);
        //if (g_load_from_3mf == nullptr)
        //    continue;

        //const ObjectsWithUUID &objects_uuid = g_load_from_3mf->objects_uuid;
        //auto object_uuid_it = std::find_if(objects_uuid.begin(), objects_uuid.end(),
        //    [id = object_ptr->id().id](const ObjectWithUUID &o_uuid) { return o_uuid.object_id == id; }
        //);

        //assert(object_uuid_it != objects_uuid.end());
        //if (object_uuid_it == objects_uuid.end())
        //    continue;

        //assert(!object_uuid_it->object_uuid.is_nil());
        //if (object_uuid_it->object_uuid.is_nil())
        //    continue;

        //assert(object_uuid_it->components_uuid.size() == object_ptr->volumes.size());
        //if (object_uuid_it->components_uuid.size() != object_ptr->volumes.size())
        //    continue;
        //auto component_uuid_it = object_uuid_it->components_uuid.begin();

        std::vector<std::pair<unsigned, const ModelVolume *>> valid_volumes;
        for (const ModelVolume *volume_ptr : object_ptr->volumes) {
        //  ScopeGuard sg_component_increase([&component_uuid_it]() { ++component_uuid_it; });
            if (volume_ptr == nullptr || volume_ptr->mesh_ptr() == nullptr)
                continue;
            if (auto it = stored_volumes.find(volume_ptr); it != stored_volumes.end())
                valid_volumes.emplace_back(it->second, volume_ptr);
        }
        if (valid_volumes.empty())
            continue;

        stream << "  <" << OBJECT_TAG << " "
            << ID_ATTR << "=\"" << object_id << "\" "
            << NAME_ATTR << "=\"" << Slic3r::Biz::Utils::xml_escape(object_ptr->name) + "\" "
        //  << PROD_NS << UUID_ATTR<< "=\"" << object_uuid_it->object_uuid << "\" "
            << ">\n";
        stored_objects[object_ptr->id().id] = {object_id};
        ++object_id;

        stream << "   <" << COMPONENTS_TAG << ">\n";
        for (auto [volume_id, volume_ptr] : valid_volumes)
            store_component(stream, volume_id, /*component_uuid_it->component_uuid,*/ &volume_ptr->get_matrix());
        stream << "   </" << COMPONENTS_TAG << ">\n";
        stream << "  </" << OBJECT_TAG << ">\n";
    }
    return stored_objects;
}

InstanceToBuildOrder write_instances(std::stringstream &stream, const Slic3r::Domain::Model &model,
    const ObjectToObjectid& stored_objects)
{
    InstanceToBuildOrder stored_instances;
    //assert(g_load_from_3mf != nullptr);
    //if (g_load_from_3mf == nullptr)
    //    return stored_instances;
    //assert(!g_load_from_3mf->build_uuid.is_nil());
    //stream << " <" << BUILD_TAG  << " " << PROD_NS << UUID_ATTR << "=\"" << g_load_from_3mf->build_uuid << "\" >\n";
    stream << " <" << BUILD_TAG  << ">\n";
    write_xml_commnet(stream, "List of PrusaSlicer:ModelInstance(with instance transformation)");
    unsigned stored_instance_index = 0;
    //const ItemsWithUUID &items_uuid = g_load_from_3mf->items_uuid;
    for (const ModelObject *object_ptr : model.objects) {
        if (!is_valid_object(object_ptr))
            continue;
        auto it = stored_objects.find(object_ptr->id().id);
        if (it == stored_objects.end())
            continue;
        unsigned instance_id = it->second;
        for (const ModelInstance *instance_ptr : object_ptr->instances) {
            // NOTE: items MUST NOT reference objects of type "other", either directly or recursively
            //auto item_uuid_it = std::find_if(items_uuid.begin(), items_uuid.end(), 
            //    [id = instance_ptr->id().id](const ItemWithUUID &item) {
            //        return item.instance_id == id;
            //    });
            //assert(item_uuid_it != items_uuid.end());
            //if (item_uuid_it == items_uuid.end())
            //    continue;

            stream << "  <" << ITEM_TAG << " " << 
                OBJECTID_ATTR << "=\"" << instance_id << "\" ";
                //PROD_NS << UUID_ATTR << "=\"" << item_uuid_it->item_uuid << "\" ";
            add_transformation_attr(stream, instance_ptr->get_matrix());
            stream << "/>\n";
            stored_instances[instance_ptr->id().id] = {stored_instance_index};
            ++stored_instance_index;
            // Check valid transformation of instances()
            // When False it is missinterpreted instance and should be separated from object.
            assert(
                instance_ptr->id().id == object_ptr->instances.front()->id().id ||
                Biz::Algorithms::Geometry::trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(                    
                    instance_ptr->get_matrix(), object_ptr->instances.front()->get_matrix())
            );
        }
    }
    stream << " </" << BUILD_TAG << ">\n";
    return stored_instances;
}

CT_Metadata_Model &update(CT_Metadata_Model &metadata, const Slic3r::Domain::Model &model) {
    // Copy permanent Creation Date
    //if (g_load_from_3mf != nullptr &&
    //    !g_load_from_3mf->creation_date.empty())
    //    metadata.insert(
    //        metadata.begin(),
    //        ModelMetadata{ModelMetadataNames::CreationDate, g_load_from_3mf->creation_date, true}
    //    );

    // Remove non unique names
    std::set<std::string_view> used;
    auto is_overriden_metadata = [&used](const ModelMetadata &m) {
        if (std::holds_alternative<ModelMetadataNames>(m.name)) {
            ModelMetadataNames name = std::get<ModelMetadataNames>(m.name);
            if (name == ModelMetadataNames::ModificationDate ||
                //name == ModelMetadataNames::Slic3r_version ||                
                name == ModelMetadataNames::Application)
                return true;
        }
        // Unify metadata name and left only first occurance
        if (!used.insert(to_name(m.name)).second)
            return true; // not first occurance of name

        return false;
    };
    auto it = std::remove_if(metadata.begin(), metadata.end(), is_overriden_metadata);
    metadata.erase(it, metadata.end());

    // Override metadata by current data
    //metadata.push_back(ModelMetadata{ModelMetadataNames::Slic3r_version, VERSION_3MF});
    std::string date = Slic3r::Utils::utc_timestamp(Slic3r::Utils::get_current_time_utc());
    // keep only the date part of the string
    date = date.substr(0, 10);
    metadata.push_back(ModelMetadata{ModelMetadataNames::ModificationDate, date});
    if (used.find(to_name(ModelMetadataNames::CreationDate)) == used.end())
        metadata.push_back(ModelMetadata{ModelMetadataNames::CreationDate, date, true});
    metadata.push_back(ModelMetadata{ModelMetadataNames::Application, std::string(SLIC3R_BUILD_ID)});

    // Sort to write in stable order
    std::sort(metadata.begin(), metadata.end());
    return metadata;
}
}

// NOTE: There can be more than one 3D payload in a 3MF Document, but only one primary 3D payload
StoredStructure Slic3r::store_model3mf(mz_zip_archive &archive, const Domain::Model &model, const char* filepath, const Store3mfParam &param) {
    StoredStructure result;

    // Inside of 3mf model/resources/object id start with 1 is unique and increase every time when use
    unsigned object_id = 1;

    ZipBackend backend = param.use_uncompressed_version ? ZipBackend::Uncompressed : ZipBackend::Compressed;

    // Dual-write MM segmentation into legacy slic3rpe:mmu_segmentation triangle attributes
    // (in addition to Metadata/Slic3r_facets_annotation.json) so 2.x readers, which only
    // understand the inline attribute, still see it. Only worthwhile when the whole model's
    // painting fits version 1 - a 2.x reader can't decode version 2 states regardless of where
    // they are stored, and the version is a single file-wide flag, not per volume.
    // Not supported together with the production extension: that stores geometry in separate
    // 3D/Objects/*.model parts, a structure 2.x readers never had.
    bool dual_write_mm = !param.use_production_extension
        && model.is_mm_painted()
        && model.minimum_required_painting_version(&ModelVolume::mm_segmentation_facets) == 1;

    if (param.use_production_extension) {
        // store meshes to separate files by production extension
        result.meshes = store_separate_meshes(archive, model, param.zip64, object_id, backend);
        if (!result.meshes.empty()) {
            // store 3D/_rels/filepath.rels for separated meshes
            std::vector<std::string> model_paths;
            model_paths.reserve(result.meshes.size());
            for (const auto &[_, objectid] : result.meshes)
                model_paths.push_back(objectid.path);

            Relationships relationships = get_relationships(model_paths);

            std::string filepath_str(filepath);
            auto const pos = filepath_str.find_last_of('/');
            std::string rels_filepath = "3D/_rels/" + filepath_str.substr(pos + 1) + ".rels";
            store(archive, relationships, rels_filepath.c_str());
        }
    }

    ZipContextScoped context(archive, filepath, param.zip64, backend);
    { // context of stream
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<" << MODEL_TAG << " " 
            << UNIT_ATTR << "=\"" << unit_to_name.left.at(ST_Unit::millimeter) << "\" "
            << LANG_ATTR << "=\"en-US\" "
            << XMLNS_ATTR << "=\"" << XMLNS_VALUE << "\"";
        if (param.use_production_extension)
            // Use "p" as production extension namespace name and set it as required
            stream << " xmlns:p=\"" << XMLNS_PRODUCTION_VALUE << "\""
                   << " requiredextensions=\"p\"";
        if (dual_write_mm)
            // "slic3rpe" is the namespace prefix 2.x readers expect for their custom attributes.
            stream << " xmlns:slic3rpe=\"" << XMLNS_SLIC3R_VALUE << "\"";
        stream << ">\n";

        auto metadata = param.metadata; // copy from const
        if (dual_write_mm)
            metadata.push_back(ModelMetadata{std::string(Legacy::MM_PAINTING_VERSION_METADATA_NAME), "1"});
        write(stream, update(metadata, model));

        // Start write resources
        stream << " <" << RESOURCES_TAG << ">\n";
        write_xml_commnet(stream, "Geometry of mesh - vertices and triangles");
        write_to_context(context.sink(), stream);
    } // context of stream

    if (!param.use_production_extension) // store meshes into main model
        result.meshes = store_meshes(context.sink(), model, object_id, dual_write_mm);
    
    { // context of stream
        std::stringstream stream;
        result.volumes = write_volumes(stream, model, object_id, result.meshes);
        result.objects = write_objects(stream, model, object_id, result.volumes);
        stream << " </" << RESOURCES_TAG << ">\n";
        result.instances = write_instances(stream, model, result.objects);
        stream << "</" << MODEL_TAG << ">\n";
        write_to_context(context.sink(), stream);
    }

    return result;
}
