#include "LabelObjects.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <cassert>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"


namespace Slic3r::GCode {


namespace {

Polygon instance_outline(const PrintInstance* pi)
{
    ExPolygons outline;
    const ModelObject* mo = pi->model_instance->get_object();
    const ModelInstance* mi = pi->model_instance;
    for (const ModelVolume *v : mo->volumes) {
        Polygons vol_outline;
        vol_outline = project_mesh(v->mesh().its,
                                    mi->get_matrix() * v->get_matrix(),
                                    [] {});
        switch (v->type()) {
        case ModelVolumeType::MODEL_PART: outline = union_ex(outline, vol_outline); break;
        case ModelVolumeType::NEGATIVE_VOLUME: outline = diff_ex(outline, vol_outline); break;
        default:;
        }
    }

    // The projection may contain multiple polygons, which is not supported by Klipper.
    // When that happens, calculate and use a 2d convex hull instead.
    if (outline.size() == 1u)
        return outline.front().contour;
    else
        return pi->model_instance->get_object()->convex_hull_2d(pi->model_instance->get_matrix());
}

}; // anonymous namespace


void LabelObjects::init(const SpanOfConstPtrs<PrintObject>& objects, LabelObjectsStyle label_object_style, GCodeFlavor gcode_flavor)
{
    m_label_objects_style = label_object_style;
    m_flavor = gcode_flavor;

    if (m_label_objects_style == LabelObjectsStyle::Disabled)
        return;

    std::map<const ModelObject*, std::vector<const PrintInstance*>> model_object_to_print_instances;

    // Iterate over all PrintObjects and their PrintInstances, collect PrintInstances which
    // belong to the same ModelObject.
    for (const PrintObject* po : objects)
        for (const PrintInstance& pi : po->instances())
            model_object_to_print_instances[pi.model_instance->get_object()].emplace_back(&pi);

    // Now go through the map, assign a unique_id to each of the PrintInstances and get the indices of the
    // respective ModelObject and ModelInstance so we can use them in the tags. This will maintain
    // indices even in case that some instances are rotated (those end up in different PrintObjects)
    // or when some are out of bed (these ModelInstances have no corresponding PrintInstances).
    int unique_id = 0;
    for (const auto& [model_object, print_instances] : model_object_to_print_instances) {
        const ModelObjectPtrs& model_objects = model_object->get_model()->objects;
        int object_id = int(std::find(model_objects.begin(), model_objects.end(), model_object) - model_objects.begin());
        for (const PrintInstance* const pi : print_instances) {
            bool object_has_more_instances = print_instances.size() > 1u;
            int instance_id = int(std::find(model_object->instances.begin(), model_object->instances.end(), pi->model_instance) - model_object->instances.begin());

            // Get object name and trim it so we do not run into https://github.com/prusa3d/PrusaSlicer/issues/13314.
            // The limit in FW is 96 chars, OctoPrint may add no more than 12 chars at the end (checksum).
            // PrusaSlicer may add instance designation couple of lines below. Let's limit the name itself
            // to 60 characters in all cases so we do not complicate it too much.
            std::string name = model_object->name;
            const size_t len_lim = 60;
            if (name.size() > len_lim) {
                // Make sure that we tear no UTF-8 sequence apart.
                auto is_utf8_start_byte = [](char c) {
                    return (c & 0b10000000) == 0           // ASCII byte (0xxxxxxx)
                        || (c & 0b11100000) == 0b11000000  // Start of 2-byte sequence
                        || (c & 0b11110000) == 0b11100000  // Start of 3-byte sequence
                        || (c & 0b11111000) == 0b11110000; // Start of 4-byte sequence
                };
                size_t i = len_lim;
                while (i > 0 && ! is_utf8_start_byte(name[i]))
                    --i;
                name = name.substr(0,i) + "...";
            }

            // Now compose the name of the object and define whether indexing is 0 or 1-based.
            if (m_label_objects_style == LabelObjectsStyle::Octoprint) {
                // use zero-based indexing for objects and instances, as we always have done
                name += " id:" + std::to_string(object_id) + " copy " + std::to_string(instance_id); 
            }
            else if (m_label_objects_style == LabelObjectsStyle::Firmware) {
                // use one-based indexing for objects and instances so indices match what we see in PrusaSlicer.
                ++object_id;
                ++instance_id;

                if (object_has_more_instances)
                    name += " (Instance " + std::to_string(instance_id) + ")";
                if (m_flavor == gcfKlipper) {
                    // Disallow Klipper special chars, common illegal filename chars, etc.
                    const std::string banned = "\b\t\n\v\f\r \"#%&\'*-./:;<>\\";
                    std::replace_if(name.begin(), name.end(), [&banned](char c) { return banned.find(c) != std::string::npos; }, '_');
                }
            }

            // Now calculate the polygon and center for Cancel Object (this is not always used).
            Polygon outline = instance_outline(pi);
            assert(! outline.empty());
            outline.douglas_peucker(50000.f);
            Point center = outline.centroid();
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer) - 1, "%.3f,%.3f", unscale<float>(center[0]), unscale<float>(center[1]));
            std::string center_str(buffer);
            std::string polygon_str = std::string("[");
            for (const Point& point : outline) {
                std::snprintf(buffer, sizeof(buffer) - 1, "[%.3f,%.3f],", unscale<float>(point[0]), unscale<float>(point[1]));
                polygon_str += buffer;
            }
            polygon_str.pop_back();
            polygon_str += "]";

            m_label_data.emplace_back(LabelData{pi, name, center_str, polygon_str, unique_id});
            ++unique_id;
        }
    }
}

bool LabelObjects::update(const PrintInstance *instance) {
    if (this->last_operation_instance == instance) {
        return false;
    }
    this->last_operation_instance = instance;
    return true;
}

std::string LabelObjects::maybe_start_instance(GCodeWriter& writer) {
    if (current_instance == nullptr && last_operation_instance != nullptr) {
        current_instance = last_operation_instance;

        std::string result{this->start_object(*current_instance, LabelObjects::IncludeName::No)};
        result += writer.reset_e(true);
        return result;
    }
    return "";
}

std::string LabelObjects::maybe_stop_instance() {
    if (current_instance != nullptr) {
        const std::string result{this->stop_object(*current_instance)};
        current_instance = nullptr;
        return result;
    }
    return "";
}

std::string LabelObjects::maybe_change_instance(GCodeWriter& writer) {
    if (last_operation_instance != current_instance) {
        const std::string stop_instance_gcode{this->maybe_stop_instance()};
        // Be carefull with refactoring: this->maybe_stop_instance() + this->maybe_start_instance()
        // may not be evaluated in order. The order is indeed undefined!
        return stop_instance_gcode + this->maybe_start_instance(writer);
    }
    return "";
}

bool LabelObjects::has_active_instance() {
    return this->current_instance != nullptr;
}

std::string LabelObjects::all_objects_header() const
{
    if (m_label_objects_style == LabelObjectsStyle::Disabled)
        return std::string();

    std::string out;   

    out += "\n";
    for (const LabelData& label : m_label_data) {
        if (m_label_objects_style == LabelObjectsStyle::Firmware && m_flavor == gcfKlipper)
            out += "EXCLUDE_OBJECT_DEFINE NAME='" + label.name + "' CENTER=" + label.center + " POLYGON=" + label.polygon + "\n";
        else {
            out += start_object(*label.pi, IncludeName::Yes);
            out += stop_object(*label.pi);
        }
    }
    out += "\n";
    return out;
}

std::string LabelObjects::all_objects_header_singleline_json() const
{
    std::string out;
    out = "{\"objects\":[";
    for (size_t i=0; i<m_label_data.size(); ++i) {
        const LabelData& label = m_label_data[i];
        out += std::string("{\"name\":\"") + label.name + "\",";
        out += "\"polygon\":" + label.polygon + "}";
        if (i != m_label_data.size() - 1)
            out += ",";
    }
    out += "]}";
    return out;
}



std::string LabelObjects::start_object(const PrintInstance& print_instance, IncludeName include_name) const
{
    if (m_label_objects_style == LabelObjectsStyle::Disabled)
        return std::string();

    const LabelData& label = *std::find_if(m_label_data.begin(), m_label_data.end(), [&print_instance](const LabelData& ld) { return ld.pi == &print_instance; });

    std::string out;
    if (m_label_objects_style == LabelObjectsStyle::Octoprint)
        out += std::string("; printing object ") + label.name + "\n";
    else if (m_label_objects_style == LabelObjectsStyle::Firmware) {
        if (m_flavor == GCodeFlavor::gcfMarlinFirmware || m_flavor == GCodeFlavor::gcfMarlinLegacy || m_flavor == GCodeFlavor::gcfRepRapFirmware) {
            out += std::string("M486 S") + std::to_string(label.unique_id);
            if (include_name == IncludeName::Yes) {
                out += (m_flavor == GCodeFlavor::gcfRepRapFirmware ? " A" : "\nM486 A");
                out += (m_flavor == GCodeFlavor::gcfRepRapFirmware ? (std::string("\"") + label.name + "\"") : label.name);
            }
            out += "\n";
        } else if (m_flavor == gcfKlipper)
            out += "EXCLUDE_OBJECT_START NAME='" + label.name + "'\n";
        else {
            // Not supported by / implemented for the other firmware flavors.
        }
    }
    return out;
}



std::string LabelObjects::stop_object(const PrintInstance& print_instance) const
{
    if (m_label_objects_style == LabelObjectsStyle::Disabled)
        return std::string();

    const LabelData& label = *std::find_if(m_label_data.begin(), m_label_data.end(), [&print_instance](const LabelData& ld) { return ld.pi == &print_instance; });

    std::string out;
    if (m_label_objects_style == LabelObjectsStyle::Octoprint)
        out += std::string("; stop printing object ") + label.name + "\n";
    else if (m_label_objects_style == LabelObjectsStyle::Firmware) {
        if (m_flavor == GCodeFlavor::gcfMarlinFirmware || m_flavor == GCodeFlavor::gcfMarlinLegacy || m_flavor == GCodeFlavor::gcfRepRapFirmware)
            out += std::string("M486 S-1\n");
        else if (m_flavor ==gcfKlipper)
            out += "EXCLUDE_OBJECT_END NAME='" + label.name + "'\n";
        else {
            // Not supported by / implemented for the other firmware flavors.
        }
    }
    return out;
}



} // namespace Slic3r::GCode
