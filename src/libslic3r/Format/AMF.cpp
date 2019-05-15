#include <limits>
#include <string.h>
#include <map>
#include <string>
#include <expat.h>

#include <boost/nowide/cstdio.hpp>

#include "../libslic3r.h"
#include "../Model.hpp"
#include "../GCode.hpp"
#include "../PrintConfig.hpp"
#include "../Utils.hpp"
#include "AMF.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/nowide/fstream.hpp>
#include <miniz/miniz_zip.h>

#if 0
// Enable debugging and assert in this file.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

// VERSION NUMBERS
// 0 : .amf, .amf.xml and .zip.amf files saved by older slic3r. No version definition in them.
// 1 : Introduction of amf versioning. No other change in data saved into amf files.
// 2 : Added z component of offset
//     Added x and y components of rotation
//     Added x, y and z components of scale
//     Added x, y and z components of mirror
const unsigned int VERSION_AMF = 2;
const char* SLIC3RPE_AMF_VERSION = "slic3rpe_amf_version";

const char* SLIC3R_CONFIG_TYPE = "slic3rpe_config";

namespace Slic3r
{

struct AMFParserContext
{
    AMFParserContext(XML_Parser parser, DynamicPrintConfig *config, Model *model) :
        m_version(0),
        m_parser(parser),
        m_model(*model),
        m_object(nullptr),
        m_volume(nullptr),
        m_material(nullptr),
        m_instance(nullptr),
        m_config(config)
    {
        m_path.reserve(12);
    }

    void stop()
    {
        XML_StopParser(m_parser, 0);
    }

    void startElement(const char *name, const char **atts);
    void endElement(const char *name);
    void endDocument();
    void characters(const XML_Char *s, int len);

    static void XMLCALL startElement(void *userData, const char *name, const char **atts)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->startElement(name, atts);
    }

    static void XMLCALL endElement(void *userData, const char *name)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->endElement(name);
    }

    /* s is not 0 terminated. */
    static void XMLCALL characters(void *userData, const XML_Char *s, int len)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->characters(s, len);
    }

    static const char* get_attribute(const char **atts, const char *id) {
        if (atts == nullptr)
            return nullptr;
        while (*atts != nullptr) {
            if (strcmp(*(atts ++), id) == 0)
                return *atts;
            ++ atts;
        }
        return nullptr;
    }

    enum AMFNodeType {
        NODE_TYPE_INVALID = 0,
        NODE_TYPE_UNKNOWN,
        NODE_TYPE_AMF,                  // amf
                                        // amf/metadata
        NODE_TYPE_MATERIAL,             // amf/material
                                        // amf/material/metadata
        NODE_TYPE_OBJECT,               // amf/object
                                        // amf/object/metadata
        NODE_TYPE_MESH,                 // amf/object/mesh
        NODE_TYPE_VERTICES,             // amf/object/mesh/vertices
        NODE_TYPE_VERTEX,               // amf/object/mesh/vertices/vertex
        NODE_TYPE_COORDINATES,          // amf/object/mesh/vertices/vertex/coordinates
        NODE_TYPE_COORDINATE_X,         // amf/object/mesh/vertices/vertex/coordinates/x
        NODE_TYPE_COORDINATE_Y,         // amf/object/mesh/vertices/vertex/coordinates/y
        NODE_TYPE_COORDINATE_Z,         // amf/object/mesh/vertices/vertex/coordinates/z
        NODE_TYPE_VOLUME,               // amf/object/mesh/volume
                                        // amf/object/mesh/volume/metadata
        NODE_TYPE_TRIANGLE,             // amf/object/mesh/volume/triangle
        NODE_TYPE_VERTEX1,              // amf/object/mesh/volume/triangle/v1
        NODE_TYPE_VERTEX2,              // amf/object/mesh/volume/triangle/v2
        NODE_TYPE_VERTEX3,              // amf/object/mesh/volume/triangle/v3
        NODE_TYPE_CONSTELLATION,        // amf/constellation
        NODE_TYPE_INSTANCE,             // amf/constellation/instance
        NODE_TYPE_DELTAX,               // amf/constellation/instance/deltax
        NODE_TYPE_DELTAY,               // amf/constellation/instance/deltay
        NODE_TYPE_DELTAZ,               // amf/constellation/instance/deltaz
        NODE_TYPE_RX,                   // amf/constellation/instance/rx
        NODE_TYPE_RY,                   // amf/constellation/instance/ry
        NODE_TYPE_RZ,                   // amf/constellation/instance/rz
        NODE_TYPE_SCALE,                // amf/constellation/instance/scale
        NODE_TYPE_SCALEX,               // amf/constellation/instance/scalex
        NODE_TYPE_SCALEY,               // amf/constellation/instance/scaley
        NODE_TYPE_SCALEZ,               // amf/constellation/instance/scalez
        NODE_TYPE_MIRRORX,              // amf/constellation/instance/mirrorx
        NODE_TYPE_MIRRORY,              // amf/constellation/instance/mirrory
        NODE_TYPE_MIRRORZ,              // amf/constellation/instance/mirrorz
        NODE_TYPE_METADATA,             // anywhere under amf/*/metadata
    };

    struct Instance {
        Instance()
            : deltax_set(false), deltay_set(false), deltaz_set(false)
            , rx_set(false), ry_set(false), rz_set(false)
            , scalex_set(false), scaley_set(false), scalez_set(false)
            , mirrorx_set(false), mirrory_set(false), mirrorz_set(false) {}
        // Shift in the X axis.
        float deltax;
        bool  deltax_set;
        // Shift in the Y axis.
        float deltay;
        bool  deltay_set;
        // Shift in the Z axis.
        float deltaz;
        bool  deltaz_set;
        // Rotation around the X axis.
        float rx;
        bool  rx_set;
        // Rotation around the Y axis.
        float ry;
        bool  ry_set;
        // Rotation around the Z axis.
        float rz;
        bool  rz_set;
        // Scaling factors
        float scalex;
        bool  scalex_set;
        float scaley;
        bool  scaley_set;
        float scalez;
        bool  scalez_set;
        // Mirroring factors
        float mirrorx;
        bool  mirrorx_set;
        float mirrory;
        bool  mirrory_set;
        float mirrorz;
        bool  mirrorz_set;

        bool anything_set() const { return deltax_set || deltay_set || deltaz_set ||
                                           rx_set || ry_set || rz_set ||
                                           scalex_set || scaley_set || scalez_set ||
                                           mirrorx_set || mirrory_set || mirrorz_set; }
    };

    struct Object {
        Object() : idx(-1) {}
        int                     idx;
        std::vector<Instance>   instances;
    };

    // Version of the amf file
    unsigned int m_version;
    // Current Expat XML parser instance.
    XML_Parser               m_parser;
    // Model to receive objects extracted from an AMF file.
    Model                   &m_model;
    // Current parsing path in the XML file.
    std::vector<AMFNodeType> m_path;
    // Current object allocated for an amf/object XML subtree.
    ModelObject             *m_object;
    // Map from obect name to object idx & instances.
    std::map<std::string, Object> m_object_instances_map;
    // Vertices parsed for the current m_object.
    std::vector<float>       m_object_vertices;
    // Current volume allocated for an amf/object/mesh/volume subtree.
    ModelVolume             *m_volume;
    // Faces collected for the current m_volume.
    std::vector<int>         m_volume_facets;
    // Current material allocated for an amf/metadata subtree.
    ModelMaterial           *m_material;
    // Current instance allocated for an amf/constellation/instance subtree.
    Instance                *m_instance;
    // Generic string buffer for vertices, face indices, metadata etc.
    std::string              m_value[3];
    // Pointer to config to update if config data are stored inside the amf file
    DynamicPrintConfig      *m_config;

private:
    AMFParserContext& operator=(AMFParserContext&);
};

void AMFParserContext::startElement(const char *name, const char **atts)
{
    AMFNodeType node_type_new = NODE_TYPE_UNKNOWN;
    switch (m_path.size()) {
    case 0:
        // An AMF file must start with an <amf> tag.
        node_type_new = NODE_TYPE_AMF;
        if (strcmp(name, "amf") != 0)
            this->stop();
        break;
    case 1:
        if (strcmp(name, "metadata") == 0) {
            const char *type = get_attribute(atts, "type");
            if (type != nullptr) {
                m_value[0] = type;
                node_type_new = NODE_TYPE_METADATA;
            }
        } else if (strcmp(name, "material") == 0) {
            const char *material_id = get_attribute(atts, "id");
            m_material = m_model.add_material((material_id == nullptr) ? "_" : material_id);
            node_type_new = NODE_TYPE_MATERIAL;
        } else if (strcmp(name, "object") == 0) {
            const char *object_id = get_attribute(atts, "id");
            if (object_id == nullptr)
                this->stop();
            else {
				assert(m_object_vertices.empty());
                m_object = m_model.add_object();
                m_object_instances_map[object_id].idx = int(m_model.objects.size())-1;
                node_type_new = NODE_TYPE_OBJECT;
            }
        } else if (strcmp(name, "constellation") == 0) {
            node_type_new = NODE_TYPE_CONSTELLATION;
        }
        break;
    case 2:
        if (strcmp(name, "metadata") == 0) {
            if (m_path[1] == NODE_TYPE_MATERIAL || m_path[1] == NODE_TYPE_OBJECT) {
                m_value[0] = get_attribute(atts, "type");
                node_type_new = NODE_TYPE_METADATA;
            }
        } else if (strcmp(name, "mesh") == 0) {
            if (m_path[1] == NODE_TYPE_OBJECT)
                node_type_new = NODE_TYPE_MESH;
        } else if (strcmp(name, "instance") == 0) {
            if (m_path[1] == NODE_TYPE_CONSTELLATION) {
                const char *object_id = get_attribute(atts, "objectid");
                if (object_id == nullptr)
                    this->stop();
                else {
                    m_object_instances_map[object_id].instances.push_back(AMFParserContext::Instance());
                    m_instance = &m_object_instances_map[object_id].instances.back();
                    node_type_new = NODE_TYPE_INSTANCE;
                }
            }
            else
                this->stop();
        }
        break;
    case 3:
        if (m_path[2] == NODE_TYPE_MESH) {
			assert(m_object);
            if (strcmp(name, "vertices") == 0)
                node_type_new = NODE_TYPE_VERTICES;
			else if (strcmp(name, "volume") == 0) {
				assert(! m_volume);
                m_volume = m_object->add_volume(TriangleMesh());
                node_type_new = NODE_TYPE_VOLUME;
			}
        } else if (m_path[2] == NODE_TYPE_INSTANCE) {
            assert(m_instance);
            if (strcmp(name, "deltax") == 0)
                node_type_new = NODE_TYPE_DELTAX;
            else if (strcmp(name, "deltay") == 0)
                node_type_new = NODE_TYPE_DELTAY;
            else if (strcmp(name, "deltaz") == 0)
                node_type_new = NODE_TYPE_DELTAZ;
            else if (strcmp(name, "rx") == 0)
                node_type_new = NODE_TYPE_RX;
            else if (strcmp(name, "ry") == 0)
                node_type_new = NODE_TYPE_RY;
            else if (strcmp(name, "rz") == 0)
                node_type_new = NODE_TYPE_RZ;
            else if (strcmp(name, "scalex") == 0)
                node_type_new = NODE_TYPE_SCALEX;
            else if (strcmp(name, "scaley") == 0)
                node_type_new = NODE_TYPE_SCALEY;
            else if (strcmp(name, "scalez") == 0)
                node_type_new = NODE_TYPE_SCALEZ;
            else if (strcmp(name, "scale") == 0)
                node_type_new = NODE_TYPE_SCALE;
            else if (strcmp(name, "mirrorx") == 0)
                node_type_new = NODE_TYPE_MIRRORX;
            else if (strcmp(name, "mirrory") == 0)
                node_type_new = NODE_TYPE_MIRRORY;
            else if (strcmp(name, "mirrorz") == 0)
                node_type_new = NODE_TYPE_MIRRORZ;
        }
        break;
    case 4:
        if (m_path[3] == NODE_TYPE_VERTICES) {
            if (strcmp(name, "vertex") == 0)
                node_type_new = NODE_TYPE_VERTEX;
        } else if (m_path[3] == NODE_TYPE_VOLUME) {
            if (strcmp(name, "metadata") == 0) {
                const char *type = get_attribute(atts, "type");
                if (type == nullptr)
                    this->stop();
                else {
                    m_value[0] = type;
                    node_type_new = NODE_TYPE_METADATA;
                }
            } else if (strcmp(name, "triangle") == 0)
                node_type_new = NODE_TYPE_TRIANGLE;
        }
        break;
    case 5:
        if (strcmp(name, "coordinates") == 0) {
            if (m_path[4] == NODE_TYPE_VERTEX) {
                node_type_new = NODE_TYPE_COORDINATES;
            } else
                this->stop();
        } else if (name[0] == 'v' && name[1] >= '1' && name[1] <= '3' && name[2] == 0) {
            if (m_path[4] == NODE_TYPE_TRIANGLE) {
                node_type_new = AMFNodeType(NODE_TYPE_VERTEX1 + name[1] - '1');
            } else
                this->stop();
        }
        break;
    case 6:
        if ((name[0] == 'x' || name[0] == 'y' || name[0] == 'z') && name[1] == 0) {
            if (m_path[5] == NODE_TYPE_COORDINATES)
                node_type_new = AMFNodeType(NODE_TYPE_COORDINATE_X + name[0] - 'x');
            else
                this->stop();
        }
        break;
    default:
        break;
    }

    m_path.push_back(node_type_new);
}

void AMFParserContext::characters(const XML_Char *s, int len)
{
    if (m_path.back() == NODE_TYPE_METADATA) {
        m_value[1].append(s, len);
    }
    else
    {
        switch (m_path.size()) {
        case 4:
            if (m_path.back() == NODE_TYPE_DELTAX ||
                m_path.back() == NODE_TYPE_DELTAY ||
                m_path.back() == NODE_TYPE_DELTAZ ||
                m_path.back() == NODE_TYPE_RX ||
                m_path.back() == NODE_TYPE_RY ||
                m_path.back() == NODE_TYPE_RZ ||
                m_path.back() == NODE_TYPE_SCALEX ||
                m_path.back() == NODE_TYPE_SCALEY ||
                m_path.back() == NODE_TYPE_SCALEZ ||
                m_path.back() == NODE_TYPE_SCALE ||
                m_path.back() == NODE_TYPE_MIRRORX ||
                m_path.back() == NODE_TYPE_MIRRORY ||
                m_path.back() == NODE_TYPE_MIRRORZ)
                m_value[0].append(s, len);
            break;
        case 6:
            switch (m_path.back()) {
                case NODE_TYPE_VERTEX1: m_value[0].append(s, len); break;
                case NODE_TYPE_VERTEX2: m_value[1].append(s, len); break;
                case NODE_TYPE_VERTEX3: m_value[2].append(s, len); break;
                default: break;
            }
        case 7:
            switch (m_path.back()) {
                case NODE_TYPE_COORDINATE_X: m_value[0].append(s, len); break;
                case NODE_TYPE_COORDINATE_Y: m_value[1].append(s, len); break;
                case NODE_TYPE_COORDINATE_Z: m_value[2].append(s, len); break;
                default: break;
            }
        default:
            break;
        }
    }
}

void AMFParserContext::endElement(const char * /* name */)
{
    switch (m_path.back()) {

    // Constellation transformation:
    case NODE_TYPE_DELTAX:
        assert(m_instance);
        m_instance->deltax = float(atof(m_value[0].c_str()));
        m_instance->deltax_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_DELTAY:
        assert(m_instance);
        m_instance->deltay = float(atof(m_value[0].c_str()));
        m_instance->deltay_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_DELTAZ:
        assert(m_instance);
        m_instance->deltaz = float(atof(m_value[0].c_str()));
        m_instance->deltaz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RX:
        assert(m_instance);
        m_instance->rx = float(atof(m_value[0].c_str()));
        m_instance->rx_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RY:
        assert(m_instance);
        m_instance->ry = float(atof(m_value[0].c_str()));
        m_instance->ry_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RZ:
        assert(m_instance);
        m_instance->rz = float(atof(m_value[0].c_str()));
        m_instance->rz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALE:
        assert(m_instance);
        m_instance->scalex = float(atof(m_value[0].c_str()));
        m_instance->scalex_set = true;
        m_instance->scaley = float(atof(m_value[0].c_str()));
        m_instance->scaley_set = true;
        m_instance->scalez = float(atof(m_value[0].c_str()));
        m_instance->scalez_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEX:
        assert(m_instance);
        m_instance->scalex = float(atof(m_value[0].c_str()));
        m_instance->scalex_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEY:
        assert(m_instance);
        m_instance->scaley = float(atof(m_value[0].c_str()));
        m_instance->scaley_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEZ:
        assert(m_instance);
        m_instance->scalez = float(atof(m_value[0].c_str()));
        m_instance->scalez_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORX:
        assert(m_instance);
        m_instance->mirrorx = float(atof(m_value[0].c_str()));
        m_instance->mirrorx_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORY:
        assert(m_instance);
        m_instance->mirrory = float(atof(m_value[0].c_str()));
        m_instance->mirrory_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORZ:
        assert(m_instance);
        m_instance->mirrorz = float(atof(m_value[0].c_str()));
        m_instance->mirrorz_set = true;
        m_value[0].clear();
        break;

    // Object vertices:
    case NODE_TYPE_VERTEX:
        assert(m_object);
        // Parse the vertex data
        m_object_vertices.emplace_back((float)atof(m_value[0].c_str()));
        m_object_vertices.emplace_back((float)atof(m_value[1].c_str()));
        m_object_vertices.emplace_back((float)atof(m_value[2].c_str()));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    // Faces of the current volume:
    case NODE_TYPE_TRIANGLE:
        assert(m_object && m_volume);
        m_volume_facets.push_back(atoi(m_value[0].c_str()));
        m_volume_facets.push_back(atoi(m_value[1].c_str()));
        m_volume_facets.push_back(atoi(m_value[2].c_str()));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    // Closing the current volume. Create an STL from m_volume_facets pointing to m_object_vertices.
    case NODE_TYPE_VOLUME:
    {
		assert(m_object && m_volume);
        stl_file &stl = m_volume->mesh.stl;
        stl.stats.type = inmemory;
        stl.stats.number_of_facets = int(m_volume_facets.size() / 3);
        stl.stats.original_num_facets = stl.stats.number_of_facets;
        stl_allocate(&stl);
        for (size_t i = 0; i < m_volume_facets.size();) {
            stl_facet &facet = stl.facet_start[i/3];
            for (unsigned int v = 0; v < 3; ++ v)
                memcpy(facet.vertex[v].data(), &m_object_vertices[m_volume_facets[i ++] * 3], 3 * sizeof(float));
        }
        stl_get_size(&stl);
        m_volume->mesh.repair();
        m_volume->center_geometry();
        m_volume->calculate_convex_hull();
        m_volume_facets.clear();
        m_volume = nullptr;
        break;
    }

    case NODE_TYPE_OBJECT:
        assert(m_object);
        m_object_vertices.clear();
        m_object = nullptr;
        break;

    case NODE_TYPE_MATERIAL:
        assert(m_material);
        m_material = nullptr;
        break;

    case NODE_TYPE_INSTANCE:
        assert(m_instance);
        m_instance = nullptr;
        break;

    case NODE_TYPE_METADATA:
        if ((m_config != nullptr) && strncmp(m_value[0].c_str(), SLIC3R_CONFIG_TYPE, strlen(SLIC3R_CONFIG_TYPE)) == 0)
            m_config->load_from_gcode_string(m_value[1].c_str());
        else if (strncmp(m_value[0].c_str(), "slic3r.", 7) == 0) {
            const char *opt_key = m_value[0].c_str() + 7;
            if (print_config_def.options.find(opt_key) != print_config_def.options.end()) {
                DynamicPrintConfig *config = nullptr;
                if (m_path.size() == 3) {
                    if (m_path[1] == NODE_TYPE_MATERIAL && m_material)
                        config = &m_material->config;
                    else if (m_path[1] == NODE_TYPE_OBJECT && m_object)
                        config = &m_object->config;
                } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME && m_volume)
                    config = &m_volume->config;
                if (config)
                    config->set_deserialize(opt_key, m_value[1]);
            } else if (m_path.size() == 3 && m_path[1] == NODE_TYPE_OBJECT && m_object && strcmp(opt_key, "layer_height_profile") == 0) {
                // Parse object's layer height profile, a semicolon separated list of floats.
                char *p = const_cast<char*>(m_value[1].c_str());
                for (;;) {
                    char *end = strchr(p, ';');
                    if (end != nullptr)
	                    *end = 0;
                    m_object->layer_height_profile.push_back(float(atof(p)));
					if (end == nullptr)
						break;
					p = end + 1;
                }
            }
            else if (m_path.size() == 3 && m_path[1] == NODE_TYPE_OBJECT && m_object && strcmp(opt_key, "sla_support_points") == 0) {
                // Parse object's layer height profile, a semicolon separated list of floats.
                unsigned char coord_idx = 0;
                Eigen::Matrix<float, 5, 1, Eigen::DontAlign> point(Eigen::Matrix<float, 5, 1, Eigen::DontAlign>::Zero());
                char *p = const_cast<char*>(m_value[1].c_str());
                for (;;) {
                    char *end = strchr(p, ';');
                    if (end != nullptr)
	                    *end = 0;

                    point(coord_idx) = atof(p);
                    if (++coord_idx == 5) {
                        m_object->sla_support_points.push_back(sla::SupportPoint(point));
                        coord_idx = 0;
                    }
					if (end == nullptr)
						break;
					p = end + 1;
                }
                m_object->sla_points_status = sla::PointsStatus::UserModified;
            }
            else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME && m_volume) {
                if (strcmp(opt_key, "modifier") == 0) {
                    // Is this volume a modifier volume?
                    // "modifier" flag comes first in the XML file, so it may be later overwritten by the "type" flag.
					m_volume->set_type((atoi(m_value[1].c_str()) == 1) ? ModelVolumeType::PARAMETER_MODIFIER : ModelVolumeType::MODEL_PART);
                } else if (strcmp(opt_key, "volume_type") == 0) {
                    m_volume->set_type(ModelVolume::type_from_string(m_value[1]));
                }
            }
        } else if (m_path.size() == 3) {
            if (m_path[1] == NODE_TYPE_MATERIAL) {
                if (m_material)
                    m_material->attributes[m_value[0]] = m_value[1];
            } else if (m_path[1] == NODE_TYPE_OBJECT) {
                if (m_object && m_value[0] == "name")
                    m_object->name = std::move(m_value[1]);
            }
        } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME) {
            if (m_volume && m_value[0] == "name")
                m_volume->name = std::move(m_value[1]);
        }
        else if (strncmp(m_value[0].c_str(), SLIC3RPE_AMF_VERSION, strlen(SLIC3RPE_AMF_VERSION)) == 0) {
            m_version = (unsigned int)atoi(m_value[1].c_str());
        }

        m_value[0].clear();
        m_value[1].clear();
        break;
    default:
        break;
    }
    m_path.pop_back();
}

void AMFParserContext::endDocument()
{
    for (const auto &object : m_object_instances_map) {
        if (object.second.idx == -1) {
            printf("Undefined object %s referenced in constellation\n", object.first.c_str());
            continue;
        }
        for (const Instance &instance : object.second.instances)
            if (instance.anything_set()) {
                ModelInstance *mi = m_model.objects[object.second.idx]->add_instance();
                mi->set_offset(Vec3d(instance.deltax_set ? (double)instance.deltax : 0.0, instance.deltay_set ? (double)instance.deltay : 0.0, instance.deltaz_set ? (double)instance.deltaz : 0.0));
                mi->set_rotation(Vec3d(instance.rx_set ? (double)instance.rx : 0.0, instance.ry_set ? (double)instance.ry : 0.0, instance.rz_set ? (double)instance.rz : 0.0));
                mi->set_scaling_factor(Vec3d(instance.scalex_set ? (double)instance.scalex : 1.0, instance.scaley_set ? (double)instance.scaley : 1.0, instance.scalez_set ? (double)instance.scalez : 1.0));
                mi->set_mirror(Vec3d(instance.mirrorx_set ? (double)instance.mirrorx : 1.0, instance.mirrory_set ? (double)instance.mirrory : 1.0, instance.mirrorz_set ? (double)instance.mirrorz : 1.0));
        }
    }
}

// Load an AMF file into a provided model.
bool load_amf_file(const char *path, DynamicPrintConfig *config, Model *model)
{
    if ((path == nullptr) || (model == nullptr))
        return false;

    XML_Parser parser = XML_ParserCreate(nullptr); // encoding
    if (!parser) {
        printf("Couldn't allocate memory for parser\n");
        return false;
    }

    FILE *pFile = boost::nowide::fopen(path, "rt");
    if (pFile == nullptr) {
        printf("Cannot open file %s\n", path);
        return false;
    }

    AMFParserContext ctx(parser, config, model);
    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, AMFParserContext::startElement, AMFParserContext::endElement);
    XML_SetCharacterDataHandler(parser, AMFParserContext::characters);

    char buff[8192];
    bool result = false;
    for (;;) {
        int len = (int)fread(buff, 1, 8192, pFile);
        if (ferror(pFile)) {
            printf("AMF parser: Read error\n");
            break;
        }
        int done = feof(pFile);
        if (XML_Parse(parser, buff, len, done) == XML_STATUS_ERROR) {
            printf("AMF parser: Parse error at line %ul:\n%s\n",
                  XML_GetCurrentLineNumber(parser),
                  XML_ErrorString(XML_GetErrorCode(parser)));
            break;
        }
        if (done) {
            result = true;
            break;
        }
    }

    XML_ParserFree(parser);
    ::fclose(pFile);

    if (result)
        ctx.endDocument();

    return result;
}

bool extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, DynamicPrintConfig* config, Model* model, unsigned int& version)
{
    if (stat.m_uncomp_size == 0)
    {
        printf("Found invalid size\n");
        mz_zip_reader_end(&archive);
        return false;
    }

    XML_Parser parser = XML_ParserCreate(nullptr); // encoding
    if (!parser) {
        printf("Couldn't allocate memory for parser\n");
        mz_zip_reader_end(&archive);
        return false;
    }

    AMFParserContext ctx(parser, config, model);
    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, AMFParserContext::startElement, AMFParserContext::endElement);
    XML_SetCharacterDataHandler(parser, AMFParserContext::characters);

    void* parser_buffer = XML_GetBuffer(parser, (int)stat.m_uncomp_size);
    if (parser_buffer == nullptr)
    {
        printf("Unable to create buffer\n");
        mz_zip_reader_end(&archive);
        return false;
    }

    mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
    if (res == 0)
    {
        printf("Error while reading model data to buffer\n");
        mz_zip_reader_end(&archive);
        return false;
    }

    if (!XML_ParseBuffer(parser, (int)stat.m_uncomp_size, 1))
    {
        printf("Error (%s) while parsing xml file at line %d\n", XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
        mz_zip_reader_end(&archive);
        return false;
    }

    ctx.endDocument();

    version = ctx.m_version;

    return true;
}

// Load an AMF archive into a provided model.
bool load_amf_archive(const char *path, DynamicPrintConfig *config, Model *model)
{
    if ((path == nullptr) || (model == nullptr))
        return false;

    unsigned int version = 0;

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    mz_bool res = mz_zip_reader_init_file(&archive, path, 0);
    if (res == 0)
    {
        printf("Unable to init zip reader\n");
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    mz_zip_archive_file_stat stat;
    // we first loop the entries to read from the archive the .amf file only, in order to extract the version from it
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            if (boost::iends_with(stat.m_filename, ".amf"))
            {
                if (!extract_model_from_archive(archive, stat, config, model, version))
                {
                    mz_zip_reader_end(&archive);
                    printf("Archive does not contain a valid model");
                    return false;
                }

                break;
            }
        }
    }

#if 0 // forward compatibility
    // we then loop again the entries to read other files stored in the archive
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            // add code to extract the file
        }
    }
#endif // forward compatibility

    mz_zip_reader_end(&archive);
    return true;
}

// Load an AMF file into a provided model.
// If config is not a null pointer, updates it if the amf file/archive contains config data
bool load_amf(const char *path, DynamicPrintConfig *config, Model *model)
{
    if (boost::iends_with(path, ".amf.xml"))
        // backward compatibility with older slic3r output
        return load_amf_file(path, config, model);
    else if (boost::iends_with(path, ".amf"))
    {
        boost::nowide::ifstream file(path, boost::nowide::ifstream::binary);
        if (!file.good())
            return false;

        std::string zip_mask(2, '\0');
        file.read(const_cast<char*>(zip_mask.data()), 2);
        file.close();

        return (zip_mask == "PK") ? load_amf_archive(path, config, model) : load_amf_file(path, config, model);
    }
    else
        return false;
}

bool store_amf(const char *path, Model *model, const DynamicPrintConfig *config)
{
    if ((path == nullptr) || (model == nullptr))
        return false;

    // forces ".zip.amf" extension
    std::string export_path = path;
    if (!boost::iends_with(export_path, ".zip.amf"))
        export_path = boost::filesystem::path(export_path).replace_extension(".zip.amf").string();

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    mz_bool res = mz_zip_writer_init_file(&archive, export_path.c_str(), 0);
    if (res == 0)
        return false;

    std::stringstream stream;
    // https://en.cppreference.com/w/cpp/types/numeric_limits/max_digits10
    // Conversion of a floating-point value to text and back is exact as long as at least max_digits10 were used (9 for float, 17 for double).
    // It is guaranteed to produce the same floating-point value, even though the intermediate text representation is not exact.
    // The default value of std::stream precision is 6 digits only!
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<amf unit=\"millimeter\">\n";
    stream << "<metadata type=\"cad\">Slic3r " << SLIC3R_VERSION << "</metadata>\n";
    stream << "<metadata type=\"" << SLIC3RPE_AMF_VERSION << "\">" << VERSION_AMF << "</metadata>\n";

    if (config != nullptr)
    {
        std::string str_config = "\n";
        for (const std::string &key : config->keys())
            if (key != "compatible_printers")
                str_config += "; " + key + " = " + config->serialize(key) + "\n";
        stream << "<metadata type=\"" << SLIC3R_CONFIG_TYPE << "\">" << xml_escape(str_config) << "</metadata>\n";
    }

    for (const auto &material : model->materials) {
        if (material.first.empty())
            continue;
        // note that material-id must never be 0 since it's reserved by the AMF spec
        stream << "  <material id=\"" << material.first << "\">\n";
        for (const auto &attr : material.second->attributes)
            stream << "    <metadata type=\"" << attr.first << "\">" << attr.second << "</metadata>\n";
        for (const std::string &key : material.second->config.keys())
            stream << "    <metadata type=\"slic3r." << key << "\">" << material.second->config.serialize(key) << "</metadata>\n";
        stream << "  </material>\n";
    }
    std::string instances;
    for (size_t object_id = 0; object_id < model->objects.size(); ++ object_id) {
        ModelObject *object = model->objects[object_id];
        stream << "  <object id=\"" << object_id << "\">\n";
        for (const std::string &key : object->config.keys())
            stream << "    <metadata type=\"slic3r." << key << "\">" << object->config.serialize(key) << "</metadata>\n";
        if (!object->name.empty())
            stream << "    <metadata type=\"name\">" << xml_escape(object->name) << "</metadata>\n";
        const std::vector<double> &layer_height_profile = object->layer_height_profile;
        if (layer_height_profile.size() >= 4 && (layer_height_profile.size() % 2) == 0) {
            // Store the layer height profile as a single semicolon separated list.
            stream << "    <metadata type=\"slic3r.layer_height_profile\">";
            stream << layer_height_profile.front();
            for (size_t i = 1; i < layer_height_profile.size(); ++i)
                stream << ";" << layer_height_profile[i];
                stream << "\n    </metadata>\n";
        }
        //FIXME Store the layer height ranges (ModelObject::layer_height_ranges)

        const std::vector<sla::SupportPoint>& sla_support_points = object->sla_support_points;
        if (!sla_support_points.empty()) {
            // Store the SLA supports as a single semicolon separated list.
            stream << "    <metadata type=\"slic3r.sla_support_points\">";
            for (size_t i = 0; i < sla_support_points.size(); ++i) {
                if (i != 0)
                    stream << ";";
                stream << sla_support_points[i].pos(0) << ";" << sla_support_points[i].pos(1) << ";" << sla_support_points[i].pos(2) << ";" << sla_support_points[i].head_front_radius << ";" << sla_support_points[i].is_new_island;
            }
            stream << "\n    </metadata>\n";
        }

        stream << "    <mesh>\n";
        stream << "      <vertices>\n";
        std::vector<int> vertices_offsets;
        int              num_vertices = 0;
        for (ModelVolume *volume : object->volumes) {
            vertices_offsets.push_back(num_vertices);
            if (! volume->mesh.repaired)
                throw std::runtime_error("store_amf() requires repair()");
            auto &stl = volume->mesh.stl;
            if (stl.v_shared == nullptr)
                stl_generate_shared_vertices(&stl);
            const Transform3d& matrix = volume->get_matrix();
            for (size_t i = 0; i < stl.stats.shared_vertices; ++i) {
                stream << "         <vertex>\n";
                stream << "           <coordinates>\n";
                Vec3f v = (matrix * stl.v_shared[i].cast<double>()).cast<float>();
                stream << "             <x>" << v(0) << "</x>\n";
                stream << "             <y>" << v(1) << "</y>\n";
                stream << "             <z>" << v(2) << "</z>\n";
                stream << "           </coordinates>\n";
                stream << "         </vertex>\n";
            }
            num_vertices += stl.stats.shared_vertices;
        }
        stream << "      </vertices>\n";
        for (size_t i_volume = 0; i_volume < object->volumes.size(); ++i_volume) {
            ModelVolume *volume = object->volumes[i_volume];
            int vertices_offset = vertices_offsets[i_volume];
            if (volume->material_id().empty())
                stream << "      <volume>\n";
            else
                stream << "      <volume materialid=\"" << volume->material_id() << "\">\n";
            for (const std::string &key : volume->config.keys())
                stream << "        <metadata type=\"slic3r." << key << "\">" << volume->config.serialize(key) << "</metadata>\n";
            if (!volume->name.empty())
                stream << "        <metadata type=\"name\">" << xml_escape(volume->name) << "</metadata>\n";
            if (volume->is_modifier())
                stream << "        <metadata type=\"slic3r.modifier\">1</metadata>\n";
            stream << "        <metadata type=\"slic3r.volume_type\">" << ModelVolume::type_to_string(volume->type()) << "</metadata>\n";
            for (int i = 0; i < (int)volume->mesh.stl.stats.number_of_facets; ++i) {
                stream << "        <triangle>\n";
                for (int j = 0; j < 3; ++j)
                stream << "          <v" << j + 1 << ">" << volume->mesh.stl.v_indices[i].vertex[j] + vertices_offset << "</v" << j + 1 << ">\n";
                stream << "        </triangle>\n";
            }
            stream << "      </volume>\n";
        }
        stream << "    </mesh>\n";
        stream << "  </object>\n";
        if (!object->instances.empty()) {
            for (ModelInstance *instance : object->instances) {
                char buf[512];
                sprintf(buf,
                    "    <instance objectid=\"" PRINTF_ZU "\">\n"
                    "      <deltax>%lf</deltax>\n"
                    "      <deltay>%lf</deltay>\n"
                    "      <deltaz>%lf</deltaz>\n"
                    "      <rx>%lf</rx>\n"
                    "      <ry>%lf</ry>\n"
                    "      <rz>%lf</rz>\n"
                    "      <scalex>%lf</scalex>\n"
                    "      <scaley>%lf</scaley>\n"
                    "      <scalez>%lf</scalez>\n"
                    "      <mirrorx>%lf</mirrorx>\n"
                    "      <mirrory>%lf</mirrory>\n"
                    "      <mirrorz>%lf</mirrorz>\n"
                    "    </instance>\n",
                    object_id,
                    instance->get_offset(X),
                    instance->get_offset(Y),
                    instance->get_offset(Z),
                    instance->get_rotation(X),
                    instance->get_rotation(Y),
                    instance->get_rotation(Z),
                    instance->get_scaling_factor(X),
                    instance->get_scaling_factor(Y),
                    instance->get_scaling_factor(Z),
                    instance->get_mirror(X),
                    instance->get_mirror(Y),
                    instance->get_mirror(Z));

                //FIXME missing instance->scaling_factor
                instances.append(buf);
            }
        }
    }
    if (! instances.empty()) {
        stream << "  <constellation id=\"1\">\n";
        stream << instances;
        stream << "  </constellation>\n";
    }
    stream << "</amf>\n";

    std::string internal_amf_filename = boost::ireplace_last_copy(boost::filesystem::path(export_path).filename().string(), ".zip.amf", ".amf");
    std::string out = stream.str();

    if (!mz_zip_writer_add_mem(&archive, internal_amf_filename.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
    {
        mz_zip_writer_end(&archive);
        boost::filesystem::remove(export_path);
        return false;
    }

    if (!mz_zip_writer_finalize_archive(&archive))
    {
        mz_zip_writer_end(&archive);
        boost::filesystem::remove(export_path);
        return false;
    }

    mz_zip_writer_end(&archive);

    return true;
}

}; // namespace Slic3r
