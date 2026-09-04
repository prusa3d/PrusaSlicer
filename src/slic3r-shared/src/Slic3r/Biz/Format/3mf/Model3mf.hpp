#ifndef slic3r_Format_3mf_Model3mf_hpp_
#define slic3r_Format_3mf_Model3mf_hpp_

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // mini zip archivator
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "admesh/stl.h" // indexed_triangle_set
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp" // Error handling
#include "Slic3r/Biz/Format/Metadata.hpp"
#include <boost/uuid/uuid.hpp>

#include "Slic3r/Biz/Format/3mf.hpp" // Store3mfParam
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::Transform3d;

// Unprefixed: Element names
// CT_ .. prefix of Complex types defined in 3MF specification
// ST_ .. prefix of Simple types defined in 3MF specification

namespace Slic3r::format_3MF{
enum class ST_Unit { millimeter, inch, micron, centimeter, foot, meter };
using Language = std::string;

//                                 name   ,   value
using AnyAttribute = std::pair<std::string, std::string>;
using AnyAttributes = std::vector<AnyAttribute>;

struct AnyTag; 
using AnyTags = std::vector<AnyTag>;
// !! Do not forget to limit depth of AnyTag:anys
struct AnyTag{
    AnyAttributes any_attr;
    std::string text; // content of tag
    AnyTags anys;     // Any other tags ...
};

// 3MF uses sRGB as specified by the World Wide Web Consortium 
// http://www.w3.org/Graphics/Color/sRGB
// Stored as "#RRGGBB" or "#RRGGBBAA"
// RR .. red channel with values from 00 to FF
// GG .. green channel with values from 00 to FF
// BB .. blue channel with values from 00 to FF
// AA .. alpha channel with values from 00 to FF
//       00 .. completely transparent
//       FF .. completely opaque
//       the default if not specified
using ST_ColorValue = std::array<uint8_t, 4>;

/// <summary>
/// Since these materials can be applied at both the object and triangle level, 
/// they are technically only specifying the material at the surface of the object.
/// Consumers may choose how the materials are distributed through the volume,
/// so long as the surfaces have the specified materials
/// </summary>
struct CT_Base {
    // Human readable material name
    std::string name;

    // Specifies the sRGB color for rendering the material
    ST_ColorValue display_color;

    AnyAttributes any_attr;
};
using CT_Bases = std::vector<CT_Base>;

using ST_ResourceID = size_t;
using ST_ResourceIndex = size_t;
struct CT_BaseMaterial{
    // Resource IDs MUST be unique among all materials groups
    ST_ResourceID id;
    AnyAttributes any_attr;
    // The order of these elements forms an implicit 0 - based index 
    // that is referenced by other elements such as the <object>
    CT_Bases base;
};
using CT_BaseMaterials = std::vector<CT_BaseMaterial>;

enum class ObjectType { model, solidsupport, support, surface, other };
using ST_ObjectType = std::variant<ObjectType, std::string>;
using ST_UriReference = std::string;

enum class ObjectMetadataNames {
    // TODO: add known metadata names for object

    // Suggest to use for object specific settings of print
    // Modifier could be non printable object! with this metadata settings
};
using CT_Metadata_Object = std::vector<MetadataBase<ObjectMetadataNames>>;

/// <summary>
/// Refers to the zero-based indexed <triangle> elements
/// that are contained in the triangles node.
/// </summary>
struct CT_Ref {
    // References an index in the mesh triangle list
    ST_ResourceIndex index;
    AnyAttributes any_attr;
    AnyTags anys;
};
using CT_Refs = std::vector<CT_Ref>;

/// <summary>
/// Inclusive range refers to the zero-based indexed <triangle> elements
/// that are contained in the triangles node.
/// </summary>
struct CT_RefRange {
    ST_ResourceIndex start_index;
    ST_ResourceIndex end_index;
    AnyAttributes any_attr;
    AnyTags anys;
};
using CT_RefRanges = std::vector<CT_RefRange>;

/// <summary>
/// A triangle set is a collection of references to triangles
/// </summary>
struct CT_TriangleSet {
    // Human-readable name of the triangle collection. MUST not be empty
    std::string name;

    // Might be used for external identification of the triangle collection data. 
    // The identifier attribute MUST be unique within the mesh and MUST not be empty.
    std::string identifier; // xs:QName

    AnyAttributes any_attr;
    CT_Refs ref; // <t:ref>
    CT_RefRanges ref_range; // <t:refrange>
};

/// <summary>
/// Contains information how triangles are grouped and organized
/// http://schemas.microsoft.com/3dmanufacturing/trianglesets/2021/07
/// e.g. color display and selection workflows
/// </summary>
struct CT_TriangleSets {
    AnyAttributes any_attr;
    std::vector<CT_TriangleSet> triangle_sets;
};

using ST_Number = float;

/// <summary>
/// MUST NOT use transforms with negative determinants to account for mirroring
/// http://schemas.microsoft.com/3dmanufacturing/mirroring/2021/07
/// </summary>
struct CT_MirrorMesh {
    // Resource ID of the original mesh object
    ST_ResourceID original_mesh;

    // Define mirror plane by its [n]ormal and [d]istance from origin
    ST_Number nx;
    ST_Number ny;
    ST_Number nz;
    ST_Number d;

    AnyAttributes any_attr;
};

/// <summary>
/// Possible keep big data. So it doesn't mimic 1:1 to 3MF specification.
/// It use Prusa indexed triangle set as data type for keep vertices and indices
/// A mesh is therefore a continuous surface without holes, gaps, open edges,
/// or non-orientable surfaces (e.g. Klein bottle)
/// </summary>
struct CT_Mesh {
    // Not used attribut for mesh yet
    AnyAttributes any_attr;

    /// <summary>
    /// CT_Vertices as its.vertices 
    ///   .. ignore CT_Vertices anyAttribute
    ///   .. ignore CT_Vertex anyAttribute
    /// CT_Triangles as its.indices
    ///   .. ignore CT_Triangles anyAttribute
    ///   .. ignore CT_Triangle p1, p2, p3, pid and anyAttribute
    /// </summary>
    indexed_triangle_set its;
    // its MUST have: Manifold Edges, Consistent Triangle Orientation, Outward-facing normals
    // NOTE: Positive fill rule
    // NOTE: Non-degeneracy - All triangles SHOULD have a non-zero area

    // Triangle Set Elements - not supported yet
    // CT_TriangleSets triangle_sets; // <t:trianglesets/>
    
    // Triangle Set References - not supported yet // <t:ref>
    // Triangle Set Reference Ranges - not supported yet // <t:refrange>

    // Mesh Mirror Transforms
    std::optional<CT_MirrorMesh> mirror_mesh; // <mm:mirrormesh>
};

using ST_Matrix3d = Domain::Transform3d;
using ST_UUID = boost::uuids::uuid;
using ST_Path = std::string;

/// <summary>
/// Container for all components to be composed into the current object.
/// A component is an object resource that is used in the context
/// of another object definition. 
/// Main purpose of components is multiply use of the same object 
/// to reduce the overall size of the 3MF Document.
/// E.g. Model of the car has 4 wheels and geometry of a wheel is same.
/// </summary>
struct CT_Component {
    // References an object resource with a matching id attribute value.
    // NOTE: valid value start from value 1
    ST_ResourceID object_id = 0;
    ST_Matrix3d transform = Domain::Transform3d::Identity();

    // [Production extension]
    // A file path to the model file being referenced.
    // The path is an absolute path from the root of the 3MF container.
    ST_Path path;

    // [Production extension]
    // A globally unique identifier for each object component in the 3MF package 
    // which allows producers and consumers to track part instances across 3MF packages.
    // NOTE: REQUIRED when production extension is used
    ST_UUID uuid;

    AnyAttributes any_attr;
    AnyTags anys;
};
// Note: ignore CT_Components:AnyAttributes
using CT_Components = std::vector<CT_Component>; 

/// [Production alternative extension]
enum class ST_ModelResolution{fullres, lowres, obfuscated};
// .. fullres: the model is a high resolution, and it is intended for printing.
// .. obfuscated: the intent of the obfuscated model is to provide a modified version 
//                of the fullres model by hiding some confidentially sensitive zones.
//                An "obfuscated" model MUST fully enclose the shape of the "fullres" version, 
//                for example, for packing purposes.
// ... lowres: the model is low resolution, for example for visualization purposes

struct CT_Alternative {
    ST_ResourceID object_id = 0;
    ST_UUID uuid;
    ST_Path path;
    ST_ModelResolution modelresolution = ST_ModelResolution::fullres;
};
using CT_Alternatives = std::vector<CT_Alternative>;

struct CT_Object {
    // Defines the unique identifier for this object.
    ST_ResourceID id = 0;

    ST_ObjectType type = ObjectType::model;

    // Path in 3mf to an Object Thumbnail of type JPEG or PNG 
    // that represents a rendered image of the object
    ST_UriReference thumbnail;

    // Part number, which editors SHOULD maintain during
    // the process of modifying and deriving objects.
    std::string part_number;

    // Name of object to improve readability.
    std::string name;

    // Reference to the property group element with the
    // matching id attribute value(e.g.<basematerials>).
    // It is REQUIRED if pindex is specified.
    ST_ResourceID pid;

    // References a zero-based index into the properties group 
    // specified by pid. This property is used to build the object.
    ST_ResourceID pindex;

    // [Production extension]
    // A globally unique identifier for each <object> in the 3MF package 
    // which allows producers and consumers to track object instances across 3MF packages. 
    // In the case that an <object> is made up of <components>, 
    // the UUID represents a unique ID for that collection of object references.
    // NOTE: REQUIRED when production extension is used
    ST_UUID uuid;

    // [Production alternative extension]
    // Indicates the intended use of the object model when there are alternative representiations.
    //ST_ModelResolution modelresolution = ST_ModelResolution::fullres;

    // [Production alternative extension]
    //CT_Alternatives alternatives;

    AnyAttributes any_attr;

    CT_Metadata_Object metadata;

    // By 3mf core spec it should contain mesh OR components
    CT_Mesh mesh;
    CT_Components components;

    AnyTags anys;
};
using CT_Objects = std::vector<CT_Object>;

struct CT_Resource {
    CT_BaseMaterials base_materials;
    CT_Objects objects;
    AnyAttributes any_attr;
    AnyTags anys;
};

enum class ItemMetadataNames {
    // TODO: add known metadata names for item
};
using CT_Metadata_Item = std::vector<MetadataBase<ItemMetadataNames>>;

struct CT_Item {
    // Reference to the <object> element with the matching id attribute value
    // NOTE: valid value start from value 1
    ST_ResourceID object_id = 0; // required

    ST_Matrix3d transform = Domain::Transform3d::Identity();

    // A unique identifier for the item. SHOULD be maintained by an editor if only the transformation is changed
    std::string part_number;

    // [Production extension]
    // A file path to the model file being referenced.
    // The path is an absolute path from the root of the 3MF container.
    ST_Path path;

    // [Production extension]
    // A globally unique identifier for each item in the 3MF package 
    // which allows producers and consumers to track part instances across 3MF packages.
    // NOTE: REQUIRED when production extension is used
    ST_UUID uuid;

    AnyAttributes any_attr;
    AnyTags anys;

    // An optional group of CT_Metadata elements as specified in the Metadata section of model.
    // Part specific options
    CT_Metadata_Item metadata;
};
using CT_Items = std::vector<CT_Item>;

struct CT_Build {
    // A consumer MUST NOT output any 3D objects not referenced by an <item> element.
    CT_Items items;

    // [Production extension]
    // A universally unique ID that allows the build to be identified over time
    // and across physical clients and printers
    // NOTE: REQUIRED when production extension is used
    ST_UUID uuid;

    AnyAttributes any_attr;
};

/// <summary>
/// Data loaded from .model file 
/// defined by 3MF Core Specification version 1.3.0
/// https://github.com/3MFConsortium/spec_core/releases/tag/1.3.0
/// with production extension 1.2.0
/// https://github.com/3MFConsortium/spec_production/releases/tag/1.2.0
/// 
/// NOTE: In .model file(XML) MUST BE exactly one unempty 'model' tag.
/// </summary>
struct Model{
    //////////////////
    // attribute data
    ///////////////////

    /// <summary>
    /// Specifies the unit used to interpret all vertices, locations, or measurements in the model.
    /// Valid values are micron, millimeter, centimeter, inch, foot, and meter.
    /// </summary>
    ST_Unit unit = ST_Unit::millimeter;

    /// <summary>
    /// Specifies the default language used for the current element and any descendant elements.
    /// The language is specified according to RFC 3066.
    /// </summary>
    Language lang;

    /// <summary>
    /// List of namespace prefixes, representing the set of extensions 
    /// that are required for processing the document.
    /// Editors and manufacturing devices MUST NOT process the document
    /// if they do not support the required extensions.
    /// NOTE: values in XML:atribute is Space-delimited
    /// </summary>
    std::vector<std::string> required_extensions;

    /// <summary>
    /// Representing the set of extensions that are recommended for processing
    /// the document with its design intent. 
    /// Editors and manufacturing devices SHOULD warn and inform the user 
    /// if they do not support the recommended extensions and ask for input how to proceed. 
    /// Required extensions MUST NOT be recommended at the same time. 
    /// NOTE: values in XML:atribute is Space-delimited
    /// </summary>
    std::vector<std::string> recomended_extensions;

    /// <summary>
    /// Production namespace prefix (described as "p")
    /// When empty, ns is not defined.
    /// </summary>
    std::string prod_ns;

    /// <summary>
    /// Mesh Mirror namespace prefix (described as "mm")
    /// When empty, ns is not defined.
    /// </summary>
    std::string mirror_ns;

    /// <summary>
    /// Any future attributes of not supported yet
    /// </summary>
    AnyAttributes any_attr;

    //////////////////
    // inside of tag
    //////////////////

    /// <summary>
    /// Sorted vector of names first known name than lexicograficaly ordered rest of metadatas
    /// </summary>
    CT_Metadata_Model metadata;

    CT_Resource resource; // required

    CT_Build build; // required

    AnyTags anys; // Any other tags ...
};
// By production extension is possible to reference another .model file from archive
// key is filepath inside of 3mf archive
using Models = std::unordered_map<std::string, format_3MF::Model>;
} // namespace Slic3r::format_3MF

namespace Slic3r::Domain { class ModelVolume; }

namespace Slic3r {

struct LoadedModel {
    // loaded root model file
    std::optional<format_3MF::Model> model;

    // loaded data from .model files addresed from model (previous definde property)
    // NOTE: It should be empty when model is not set
    format_3MF::Models sub_models;

    // When proccessed file write it to used files for sum up of unused files.
    // root model file index + sub model file indices
    std::vector<bool> used_files;
};

/// <summary>
/// Read data from root .model file into struct
/// </summary>
/// <param name="archive">Zip archive containing model on specified index</param>
/// <param name="root_file_path">File path in 3mf archive to root .model file</param>
/// <returns>Loaded data with list of founded issues</returns>
LoadedModel read_model3mf(mz_zip_archive &archive, const char *root_file_path, Read3mfIssues& collected_issues);

struct ObjectIdWithPath{
    // objectid is index inside of .model file and start from 1 and increase for stream reading
    unsigned id;

    // Identify .model file where is object stored, Added by Production extension
    // Empty means that object is inside root model 
    // (commonly used root model path: "3D/3dmodel.model" )
    // NOTE: It is optional for separate Object to its own file
    std::string path; 
};

//                           Pointer on data(volume), objectid from .model file
using MeshToObjectid = std::unordered_map<const Domain::TriangleMesh *, ObjectIdWithPath>;
// ModelVolume pointer to objectid (keyed by pointer, not id, because copies share the same id)
using VolumeToObjectid = std::unordered_map<const Domain::ModelVolume *, unsigned>;
// ModelObject::id to objectid
using ObjectToObjectid = std::unordered_map<size_t, unsigned>;
// ModelInstance::id to order inside of build
using InstanceToBuildOrder = std::unordered_map<size_t, unsigned>;

struct StoredStructure {
    MeshToObjectid meshes;
    VolumeToObjectid volumes;
    ObjectToObjectid objects;
    InstanceToBuildOrder instances;
};

/// <summary>
/// Add .model file into archive
/// </summary>
/// <param name="archive">Zip archive</param>
/// <param name="model">Model to store</param>
/// <param name="filepath">Path in 3mf where to store</param>
/// <param name="param">Specify way to store(limit export by filters)</param>
/// <returns>Structure of stored objects to by able pointed on them in 3mf</returns>
StoredStructure store_model3mf(mz_zip_archive &archive, const Domain::Model &model, 
    const char* filepath, const Store3mfParam &param);

} // namespace Slic3r

#endif // slic3r_Format_3mf_Model3mf_hpp_
