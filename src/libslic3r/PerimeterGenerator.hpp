#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include "libslic3r.h"
#include <vector>
#include "Flow.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "SurfaceCollection.hpp"
#include "PNGReadWrite.hpp"

namespace Slic3r {

class BackendImage
{
  private:
    // PNGDescr* descr;
    png::ImageGreyscale image;
    std::string image_path;
    bool LoadPng(std::string path);
    bool Clamp(size_t& x, size_t& y);
    bool busy;
    bool error_shown; // Another thread already has shown an error so don't try load again (prevent multiple error dialogs).
    bool dump(); // dump rgb values as CSV for debugging (HUGE stdout dump)
  public:
    BackendImage() :
        // descr(nullptr),
        error_shown(false),
        busy(false)
    {}
    ~BackendImage() {
        /*
        if (descr != nullptr) {
            delete descr;
        }
        */
    }
    bool LoadGreyscalePng(std::string path);
    bool LoadFile(std::string path);
    std::string path();
    size_t GetWidth();
    size_t GetHeight();
     
    uint8_t GetRed(size_t x, size_t y);
    uint8_t GetGreen(size_t x, size_t y);
    uint8_t GetBlue(size_t x, size_t y);
    bool IsOk();
    void Destroy();
};

class PerimeterGenerator {
public:
    // Inputs:
    const SurfaceCollection     *slices;
    const ExPolygons            *lower_slices;
    double                       layer_height;
    int                          layer_id;
    Flow                         perimeter_flow;
    Flow                         ext_perimeter_flow;
    Flow                         overhang_flow;
    Flow                         solid_infill_flow;
    const PrintRegionConfig     *config;
    const PrintObjectConfig     *object_config;
    const PrintConfig           *print_config;
    // Outputs:
    ExtrusionEntityCollection   *loops;
    ExtrusionEntityCollection   *gap_fill;
    SurfaceCollection           *fill_surfaces;
    const double                 z_of_current_layer;
    
    PerimeterGenerator(
        // Input:
        const SurfaceCollection*    slices,
        double                      layer_height,
        Flow                        flow,
        const PrintRegionConfig*    config,
        const PrintObjectConfig*    object_config,
        const PrintConfig*          print_config,
        const bool                  spiral_vase,
        // Output:
        // Loops with the external thin walls
        ExtrusionEntityCollection*  loops,
        // Gaps without the thin walls
        ExtrusionEntityCollection*  gap_fill,
        // Infills without the gap fills
        SurfaceCollection*          fill_surfaces,
        double                      z
        )
        : slices(slices), lower_slices(nullptr), layer_height(layer_height),
            layer_id(-1), perimeter_flow(flow), ext_perimeter_flow(flow),
            overhang_flow(flow), solid_infill_flow(flow),
            config(config), object_config(object_config), print_config(print_config),
            m_spiral_vase(spiral_vase),
            m_scaled_resolution(scaled<double>(print_config->gcode_resolution.value)),
            loops(loops), gap_fill(gap_fill), fill_surfaces(fill_surfaces),
            m_ext_mm3_per_mm(-1), m_mm3_per_mm(-1), m_mm3_per_mm_overhang(-1),
            z_of_current_layer(z)
        {}

    void        process();

    double      ext_mm3_per_mm()        const { return m_ext_mm3_per_mm; }
    double      mm3_per_mm()            const { return m_mm3_per_mm; }
    double      mm3_per_mm_overhang()   const { return m_mm3_per_mm_overhang; }
    Polygons    lower_slices_polygons() const { return m_lower_slices_polygons; }

private:
    bool        m_spiral_vase;
    double      m_scaled_resolution;
    double      m_ext_mm3_per_mm;
    double      m_mm3_per_mm;
    double      m_mm3_per_mm_overhang;
    Polygons    m_lower_slices_polygons;
};

}

#endif
