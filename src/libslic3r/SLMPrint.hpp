///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLMPrint_hpp_
#define slic3r_SLMPrint_hpp_

#include <boost/functional/hash.hpp>
#include <stdlib.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "PrintBase.hpp"
#include "Point.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/AnyPtr.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/SLMScanPath.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

enum SLMPrintStep : unsigned int {
    slmpsSlice,
    slmpsGenerateHatches,
    slmpsCount
};

enum SLMPrintObjectStep : unsigned int {
    slmposSlice,
    slmposGenerateHatches,
    slmposGenerateScanPaths,
    slmposCount
};

class SLMPrint;
class SLMPrintObject;  // Forward declaration
class GLCanvas;

using _SLMPrintObjectBase =
    PrintObjectBaseWithState<SLMPrint, SLMPrintObjectStep, slmposCount>;

} // namespace Slic3r

namespace Slic3r {

/**
 * @brief This class is the high level FSM for the SLM printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the SLM slicing. It should also dispatch the SLM printing
 * configuration values to the appropriate calculation steps.
 */
class SLMPrint : public PrintBaseWithState<SLMPrintStep, slmpsCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<SLMPrintStep, slmpsCount> Inherited;
    
    class Steps; // See SLMPrintSteps.cpp
    
public:

    SLMPrint() = default;

    virtual ~SLMPrint() override { this->clear(); }

    PrinterTechnology	technology() const noexcept override { return ptSLM; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<ObjectID> print_object_ids() const override;
    ApplyStatus         apply(const Model &model, DynamicPrintConfig config, std::vector<std::string> *warnings = nullptr) override;
    void                set_task(const TaskParams &params) override { PrintBaseWithState<SLMPrintStep, slmpsCount>::set_task_impl(params, m_objects); }
    void                process() override;
    void                finalize() override { PrintBaseWithState<SLMPrintStep, slmpsCount>::finalize_impl(m_objects); }
    void                cleanup() override {}
    // Returns true if an object step is done on all objects and there's at least one object.
    bool                is_step_done(SLMPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool                finished() const override { return this->is_step_done(slmposGenerateScanPaths) && this->Inherited::is_step_done(slmpsGenerateHatches); }

    // Returns true if a print step is done
    bool                is_step_done(SLMPrintStep step) const { return this->Inherited::is_step_done(step); }

    const std::vector<SLMPrintObject*>& objects() const { return m_objects; }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const SLMPrintObject* get_print_object_by_model_object_id(ObjectID object_id) const;
    const SLMPrintObject* get_object(ObjectID object_id) const;

    const SLMPrintConfig&       print_config() const { return m_print_config; }
    const SLMPrinterConfig&     printer_config() const { return m_printer_config; }

    std::string                 output_filename(const std::string &filename_base = std::string()) const override;

    std::string validate(std::vector<std::string>* warnings = nullptr) const override;

    // Export to .slm file format (using libSLM)
    void export_print(const std::string &fname);
    
    // Export with progress reporting
    void export_print(const std::string &fname, std::function<void(int, const std::string&)> progress_callback);

    // Invalidate steps based on a set of parameters changed.
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects);

private:
    // Print objects
    std::vector<SLMPrintObject*> m_objects;

    // Model being printed
    Model m_model;

    // SLM-specific configs
    SLMPrintConfig m_print_config;
    SLMPrinterConfig m_printer_config;
};

class SLMPrintObject : public _SLMPrintObjectBase
{
private: // Prevents erroneous use by other classes.
    using Inherited = _SLMPrintObjectBase;

public:

    // I refuse to grantee copying
    SLMPrintObject(const SLMPrintObject&) = delete;
    SLMPrintObject& operator=(const SLMPrintObject&) = delete;

    // TODO: Add SLM-specific config getter when SLMPrintObjectConfig is created
    // const SLMPrintObjectConfig& config() const { return m_config; }

    struct Instance {
        Instance(ObjectID inst_id, const Point &shft, float rot) : instance_id(inst_id), shift(shft), rotation(rot) {}
        bool operator==(const Instance &rhs) const { return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation; }
        // ID of the corresponding ModelInstance.
        ObjectID instance_id;
        // Slic3r::Point objects in scaled G-code coordinates
        Point 	shift;
        // Rotation along the Z axis, in radians.
        float 	rotation;
    };
    const std::vector<Instance>& instances() const { return m_instances; }

    // Get the model object this print object is based on
    const ModelObject* model_object() const { return m_model_object; }

    // Get sliced layers (will be populated during slicing)
    const std::vector<ExPolygons>& model_slices() const { return m_model_slices; }
    std::vector<ExPolygons>& model_slices() { return m_model_slices; }

    // Get layer height levels
    const std::vector<float>& layer_height_levels() const { return m_layer_height_levels; }
    std::vector<float>& layer_height_levels() { return m_layer_height_levels; }

    // Get hatch patterns for each layer
    const std::vector<Polylines>& hatch_patterns() const { return m_hatch_patterns; }
    std::vector<Polylines>& hatch_patterns() { return m_hatch_patterns; }

    // Get contours for each layer
    const std::vector<Polylines>& contours() const { return m_contours; }
    std::vector<Polylines>& contours() { return m_contours; }

    // Get scan paths for each layer
    const std::vector<LayerScanPaths>& scan_paths() const { return m_scan_paths; }
    std::vector<LayerScanPaths>& scan_paths() { return m_scan_paths; }

protected:
    // to be called from SLMPrint only.
    friend class SLMPrint;
    friend class PrintBaseWithState<SLMPrintStep, slmpsCount>;

    SLMPrintObject(SLMPrint* print, ModelObject* model_object);
    ~SLMPrintObject();

    template<class InstVec> inline void set_instances(InstVec&& instances) { m_instances = std::forward<InstVec>(instances); }

    // Invalidates the step, and its depending steps in SLMPrintObject and SLMPrint.
    bool                    invalidate_step(SLMPrintObjectStep step);
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    bool                    invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

private:
    // Model object this print object is based on
    ModelObject *m_model_object;

    // Instances of this object on the print bed
    std::vector<Instance> m_instances;

    // Individual 2d slice polygons from lower z to higher z levels
    std::vector<ExPolygons> m_model_slices;

    // Exact (float) height levels for each slice
    std::vector<float> m_layer_height_levels;

    // Hatch patterns for each layer (one per slice)
    // Each layer contains hatch lines (Polylines) for the infill pattern
    std::vector<Polylines> m_hatch_patterns;

    // Contours for each layer (extracted from slices)
    // Each layer contains contour polylines (outer perimeters)
    std::vector<Polylines> m_contours;

    // Scan paths for each layer (ready for export)
    // Each layer contains scan vectors with laser parameters
    std::vector<LayerScanPaths> m_scan_paths;

    // TODO: Add SLM-specific config when created
    // SLMPrintObjectConfig m_config;
};

} // namespace Slic3r

#endif /* slic3r_SLMPrint_hpp_ */

