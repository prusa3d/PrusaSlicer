# How SLA Was Added to PrusaSlicer (Template for Fiber Printing)

## Overview

PrusaSlicer supports **two main printing technologies**:
1. **FFF/FDM** (Fused Filament Fabrication) - Regular plastic extrusion
2. **SLA** (Stereolithography) - Resin-based printing

They created a **separate module** for SLA, and you need to do the same for **Fiber Printing**!

---

## The Architecture Pattern

### Current Structure:

```
src/libslic3r/
├── Print.hpp/cpp          ← FFF printing (regular plastic)
├── PrintObject.hpp/cpp    ← FFF print objects
├── SLAPrint.hpp/cpp       ← SLA printing (resin) ✨ NEW MODULE
├── SLAPrintSteps.hpp/cpp  ← SLA processing steps
└── SLA/                   ← SLA-specific code directory
    ├── SupportTree.hpp
    ├── Hollowing.hpp
    ├── Pad.hpp
    └── ... (many more files)
```

### What You Need to Create:

```
src/libslic3r/
├── Print.hpp/cpp          ← FFF (existing)
├── SLAPrint.hpp/cpp       ← SLA (existing)
├── FiberPrint.hpp/cpp     ← FIBER ✨ YOUR NEW MODULE
├── FiberPrintSteps.hpp/cpp ← Fiber processing steps
└── Fiber/                 ← Fiber-specific code directory
    ├── PathPlanner.hpp    ← Plan fiber paths
    ├── FiberPlacement.hpp ← Where to place fibers
    ├── ContinuousPath.hpp ← Generate continuous paths
    └── ... (your custom code)
```

---

## Step-by-Step: How SLA Was Done

### 1. **Created Base Print Class**

SLA extends `PrintBase` (the common interface):

```cpp
// src/libslic3r/PrintBase.hpp
class PrintBase {
    // Common interface for all print types
    virtual void process() = 0;
    virtual void export_gcode() = 0;
    // ...
};

// src/libslic3r/Print.hpp (FFF)
class Print : public PrintBase {
    // FFF-specific implementation
};

// src/libslic3r/SLAPrint.hpp (SLA)
class SLAPrint : public PrintBase {
    // SLA-specific implementation
};
```

**For Fiber**: Create `FiberPrint : public PrintBase`

---

### 2. **Created Print Steps System**

SLA has its own processing steps:

```cpp
// src/libslic3r/SLAPrint.hpp
enum SLAPrintStep : unsigned int {
    slapsMergeSlicesAndEval,  // Step 1: Merge slices
    slapsRasterize,           // Step 2: Convert to images
    slapsCount
};

enum SLAPrintObjectStep : unsigned int {
    slaposAssembly,           // Step 1: Assemble model
    slaposHollowing,          // Step 2: Hollow if needed
    slaposDrillHoles,         // Step 3: Add holes
    slaposObjectSlice,        // Step 4: Slice the model
    slaposSupportPoints,      // Step 5: Generate support points
    slaposSupportTree,        // Step 6: Build support tree
    slaposPad,                // Step 7: Add base pad
    slaposSliceSupports,      // Step 8: Slice supports
    slaposCount
};
```

**For Fiber**: Create similar steps:

```cpp
enum FiberPrintStep : unsigned int {
    fiberpsAnalyzeModel,      // Analyze where fiber is needed
    fiberpsPlanPaths,          // Plan fiber paths
    fiberpsGenerateGCode,       // Generate G-code
    fiberpsCount
};

enum FiberPrintObjectStep : unsigned int {
    fiberposSlice,              // Slice model into layers
    fiberposAnalyzeStress,      // Analyze stress (if needed)
    fiberposPlanFiberPaths,     // Plan where fibers go
    fiberposGenerateFiberGCode, // Generate fiber G-code
    fiberposCount
};
```

---

### 3. **Created Dedicated Directory**

All SLA-specific code is in `src/libslic3r/SLA/`:

```
SLA/
├── SupportTree.hpp          ← Support structure generation
├── Hollowing.hpp            ← Hollow model if needed
├── Pad.hpp                  ← Base pad generation
├── SupportPointGenerator.hpp ← Where to put supports
└── ... (30+ files)
```

**For Fiber**: Create `src/libslic3r/Fiber/`:

```
Fiber/
├── PathPlanner.hpp          ← Plan continuous fiber paths
├── FiberPlacement.hpp       ← Decide where to place fiber
├── ContinuousPath.hpp      ← Generate continuous paths
├── FiberPattern.hpp         ← Different patterns (grid, concentric, etc.)
├── FiberGCode.hpp           ← Generate fiber-specific G-code
└── ... (your custom files)
```

---

### 4. **Created Configuration System**

SLA has its own config:

```cpp
// src/libslic3r/PrintConfig.hpp
class SLAPrintConfig : public PrintConfig {
    // SLA-specific settings
    ConfigOptionFloat layer_height;
    ConfigOptionFloat exposure_time;
    ConfigOptionEnum<SLAMaterial> material_type;
    // ...
};
```

**For Fiber**: Create `FiberPrintConfig`:

```cpp
class FiberPrintConfig : public PrintConfig {
    // Fiber-specific settings
    ConfigOptionEnum<FiberType> fiber_type;        // Carbon, Glass, Kevlar
    ConfigOptionFloat fiber_density;                // How much fiber
    ConfigOptionEnum<FiberPattern> fiber_pattern;   // Grid, concentric, etc.
    ConfigOptionFloat fiber_angle;                  // 0°, 45°, 90°, etc.
    ConfigOptionBool fiber_per_layer;               // Fiber every layer?
    // ...
};
```

---

### 5. **Created Processing Pipeline**

SLA has a step-by-step process:

```cpp
// src/libslic3r/SLAPrintSteps.cpp
void SLAPrint::process() {
    // Step 1: Process all objects
    for (auto& obj : m_objects) {
        obj.assemble();           // slaposAssembly
        obj.hollow();            // slaposHollowing
        obj.drill_holes();       // slaposDrillHoles
        obj.slice();             // slaposObjectSlice
        obj.generate_supports(); // slaposSupportPoints, slaposSupportTree
        obj.add_pad();           // slaposPad
    }
    
    // Step 2: Slice supports
    for (auto& obj : m_objects) {
        obj.slice_supports();    // slaposSliceSupports
    }
    
    // Step 3: Merge and rasterize
    merge_slices();              // slapsMergeSlicesAndEval
    rasterize();                 // slapsRasterize
}
```

**For Fiber**: Create similar pipeline:

```cpp
void FiberPrint::process() {
    // Step 1: Slice model (like FFF)
    for (auto& obj : m_objects) {
        obj.slice();                    // fiberposSlice
    }
    
    // Step 2: Analyze where fiber is needed
    for (auto& obj : m_objects) {
        obj.analyze_stress();           // fiberposAnalyzeStress (optional)
    }
    
    // Step 3: Plan fiber paths
    for (auto& obj : m_objects) {
        obj.plan_fiber_paths();         // fiberposPlanFiberPaths
    }
    
    // Step 4: Generate G-code
    generate_fiber_gcode();             // fiberposGenerateFiberGCode
}
```

---

### 6. **Created G-Code Export**

SLA exports to special formats (SL1, etc.):

```cpp
// src/libslic3r/Format/SLAArchiveWriter.hpp
class SLAArchiveWriter {
    void export_print(const SLAPrint& print);
    // Exports to SL1 format (images for each layer)
};
```

**For Fiber**: Create G-code exporter:

```cpp
// src/libslic3r/Fiber/FiberGCode.hpp
class FiberGCodeWriter {
    void export_print(const FiberPrint& print);
    // Exports G-code with fiber commands
    // Example: M104 (plastic temp), M106 (fiber start), etc.
};
```

---

## Key Files to Study

### 1. **SLAPrint.hpp** - Main class structure
   - Location: `src/libslic3r/SLAPrint.hpp`
   - Shows how to extend `PrintBase`
   - Shows step management system

### 2. **SLAPrintSteps.cpp** - Processing logic
   - Location: `src/libslic3r/SLAPrintSteps.cpp`
   - Shows how steps are executed
   - Shows how to slice and process

### 3. **PrintBase.hpp** - Base interface
   - Location: `src/libslic3r/PrintBase.hpp`
   - Common interface you must implement

### 4. **PrintConfig.hpp** - Configuration
   - Location: `src/libslic3r/PrintConfig.hpp`
   - See `SLAPrintConfig` class
   - Add your `FiberPrintConfig` similarly

---

## Your Task Breakdown

### Phase 1: Setup Structure
1. ✅ Create `FiberPrint.hpp/cpp` (similar to `SLAPrint.hpp/cpp`)
2. ✅ Create `FiberPrintSteps.hpp/cpp` (similar to `SLAPrintSteps.hpp/cpp`)
3. ✅ Create `src/libslic3r/Fiber/` directory
4. ✅ Add `FiberPrintConfig` to `PrintConfig.hpp`

### Phase 2: Core Logic
5. ✅ Implement basic slicing (reuse FFF slicing)
6. ✅ Implement fiber path planning (your proprietary logic)
7. ✅ Implement continuous path generation
8. ✅ Implement G-code generation

### Phase 3: Integration
9. ✅ Integrate with GUI (add fiber settings panels)
10. ✅ Add fiber visualization in 3D preview
11. ✅ Add export functionality

---

## Example: Minimal FiberPrint Structure

```cpp
// src/libslic3r/FiberPrint.hpp
#ifndef slic3r_FiberPrint_hpp_
#define slic3r_FiberPrint_hpp_

#include "PrintBase.hpp"
#include "Fiber/FiberPathPlanner.hpp"

namespace Slic3r {

enum FiberPrintStep : unsigned int {
    fiberpsPlanPaths,
    fiberpsGenerateGCode,
    fiberpsCount
};

class FiberPrint : public PrintBase {
public:
    FiberPrint() = default;
    ~FiberPrint() = default;
    
    // Required by PrintBase
    void process() override;
    void export_gcode(const std::string& path) override;
    
    // Fiber-specific
    void plan_fiber_paths();
    void generate_fiber_gcode();
    
private:
    std::vector<FiberLayer> m_fiber_layers;
    FiberPathPlanner m_path_planner;
};

} // namespace Slic3r

#endif
```

---

## Key Differences: FFF vs SLA vs Fiber

| Feature | FFF | SLA | Fiber (Your Task) |
|---------|-----|-----|-------------------|
| **Output** | G-code | Images (SL1) | G-code (with fiber commands) |
| **Slicing** | 2D polygons | 2D polygons → images | 2D polygons + fiber paths |
| **Support** | Detachable | Tree structures | Usually not needed |
| **Special Logic** | Perimeters, infill | Supports, hollowing | **Fiber path planning** |

---

## Next Steps

1. **Study `SLAPrint.hpp`** - Understand the structure
2. **Study `SLAPrintSteps.cpp`** - Understand the processing flow
3. **Create your `FiberPrint` class** - Following the same pattern
4. **Integrate your proprietary algorithms** - Into the new structure
5. **Add GUI support** - So users can configure fiber settings

---

## Summary

**SLA was added as a completely separate module** that:
- Extends `PrintBase`
- Has its own processing steps
- Has its own directory (`SLA/`)
- Has its own configuration
- Has its own G-code/export format

**You need to do the same for Fiber!** Create a parallel structure:
- `FiberPrint` extends `PrintBase`
- `FiberPrintSteps` for processing
- `Fiber/` directory for your code
- `FiberPrintConfig` for settings
- Custom G-code generation

The good news: **The architecture is already set up for this!** You just need to follow the SLA pattern. 🚀

