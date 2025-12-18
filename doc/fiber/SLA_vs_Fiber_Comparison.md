# SLA vs Fiber: Side-by-Side Comparison

## Quick Reference: How They're Structured

This shows you exactly how SLA was added, so you can mirror it for Fiber.

---

## File Structure Comparison

### SLA Structure (Existing)
```
src/libslic3r/
├── SLAPrint.hpp              ← Main SLA print class
├── SLAPrint.cpp
├── SLAPrintSteps.hpp         ← Processing steps
├── SLAPrintSteps.cpp
└── SLA/                      ← SLA-specific directory
    ├── SupportTree.hpp
    ├── Hollowing.hpp
    ├── Pad.hpp
    ├── SupportPointGenerator.hpp
    └── ... (30+ files)
```

### Fiber Structure (What You Create)
```
src/libslic3r/
├── FiberPrint.hpp            ← Main Fiber print class ✨
├── FiberPrint.cpp            ✨
├── FiberPrintSteps.hpp       ← Processing steps ✨
├── FiberPrintSteps.cpp       ✨
└── Fiber/                    ← Fiber-specific directory ✨
    ├── PathPlanner.hpp       ✨
    ├── FiberPlacement.hpp    ✨
    ├── ContinuousPath.hpp     ✨
    └── ... (your files)
```

---

## Class Hierarchy Comparison

### SLA
```cpp
PrintBase (abstract base)
    └── SLAPrint (implements SLA logic)
```

### Fiber (What You Create)
```cpp
PrintBase (abstract base)
    └── FiberPrint (implements Fiber logic) ✨
```

---

## Processing Steps Comparison

### SLA Steps
```cpp
enum SLAPrintStep {
    slapsMergeSlicesAndEval,  // Merge all slices
    slapsRasterize,           // Convert to images
    slapsCount
};

enum SLAPrintObjectStep {
    slaposAssembly,           // Assemble model
    slaposHollowing,          // Hollow if needed
    slaposDrillHoles,         // Add holes
    slaposObjectSlice,        // Slice model
    slaposSupportPoints,      // Generate support points
    slaposSupportTree,        // Build support tree
    slaposPad,                // Add base pad
    slaposSliceSupports,      // Slice supports
    slaposCount
};
```

### Fiber Steps (What You Create)
```cpp
enum FiberPrintStep {
    fiberpsAnalyzeModel,      // Analyze where fiber needed ✨
    fiberpsPlanPaths,          // Plan fiber paths ✨
    fiberpsGenerateGCode,       // Generate G-code ✨
    fiberpsCount
};

enum FiberPrintObjectStep {
    fiberposSlice,              // Slice model ✨
    fiberposAnalyzeStress,      // Analyze stress (optional) ✨
    fiberposPlanFiberPaths,     // Plan fiber placement ✨
    fiberposGenerateFiberGCode, // Generate fiber G-code ✨
    fiberposCount
};
```

---

## Configuration Comparison

### SLA Config
```cpp
// In PrintConfig.hpp
class SLAPrintConfig : public PrintConfig {
    ConfigOptionFloat layer_height;
    ConfigOptionFloat exposure_time;
    ConfigOptionEnum<SLAMaterial> material_type;
    ConfigOptionBool supports_enable;
    // ... many more
};
```

### Fiber Config (What You Create)
```cpp
// In PrintConfig.hpp
class FiberPrintConfig : public PrintConfig {
    ConfigOptionEnum<FiberType> fiber_type;        // Carbon, Glass, Kevlar ✨
    ConfigOptionFloat fiber_density;                // How much fiber ✨
    ConfigOptionEnum<FiberPattern> fiber_pattern;   // Grid, concentric ✨
    ConfigOptionFloat fiber_angle;                  // 0°, 45°, 90° ✨
    ConfigOptionBool fiber_per_layer;               // Every layer? ✨
    // ... your custom settings
};
```

---

## Processing Flow Comparison

### SLA Flow
```cpp
void SLAPrint::process() {
    // 1. Process objects
    for (auto& obj : m_objects) {
        obj.assemble();
        obj.hollow();
        obj.drill_holes();
        obj.slice();
        obj.generate_supports();
        obj.add_pad();
    }
    
    // 2. Slice supports
    for (auto& obj : m_objects) {
        obj.slice_supports();
    }
    
    // 3. Merge and rasterize
    merge_slices();
    rasterize();
}
```

### Fiber Flow (What You Create)
```cpp
void FiberPrint::process() {
    // 1. Slice model (like FFF)
    for (auto& obj : m_objects) {
        obj.slice();                    ✨
    }
    
    // 2. Analyze where fiber needed
    for (auto& obj : m_objects) {
        obj.analyze_stress();            ✨ (optional)
    }
    
    // 3. Plan fiber paths
    for (auto& obj : m_objects) {
        obj.plan_fiber_paths();          ✨
    }
    
    // 4. Generate G-code
    generate_fiber_gcode();             ✨
}
```

---

## Output Format Comparison

### SLA Output
- **Format**: SL1 archive (ZIP with PNG images)
- **Each layer**: One PNG image
- **Location**: `src/libslic3r/Format/SLAArchiveWriter.hpp`

### Fiber Output (What You Create)
- **Format**: G-code file (with fiber commands)
- **Commands**: 
  - Regular FFF commands (M104, G1, etc.)
  - **NEW**: Fiber-specific commands (M106 fiber start, M107 fiber stop, etc.)
- **Location**: `src/libslic3r/Fiber/FiberGCode.hpp` ✨

---

## Key Files to Copy/Modify

### Study These SLA Files:
1. `src/libslic3r/SLAPrint.hpp` → Create `FiberPrint.hpp`
2. `src/libslic3r/SLAPrint.cpp` → Create `FiberPrint.cpp`
3. `src/libslic3r/SLAPrintSteps.hpp` → Create `FiberPrintSteps.hpp`
4. `src/libslic3r/SLAPrintSteps.cpp` → Create `FiberPrintSteps.cpp`
5. `src/libslic3r/PrintConfig.hpp` → Add `FiberPrintConfig` class

### Create These New Files:
1. `src/libslic3r/Fiber/PathPlanner.hpp` - Your path planning logic
2. `src/libslic3r/Fiber/FiberPlacement.hpp` - Where to place fiber
3. `src/libslic3r/Fiber/ContinuousPath.hpp` - Generate continuous paths
4. `src/libslic3r/Fiber/FiberGCode.hpp` - G-code generation

---

## Integration Points

### Where SLA is Integrated:

1. **GUI**: `src/slic3r/GUI/` - SLA-specific panels
2. **Main App**: Chooses between `Print` (FFF) or `SLAPrint` (SLA)
3. **Config**: `PrintConfig.hpp` - `SLAPrintConfig` class
4. **Export**: Format registry for SL1 files

### Where You'll Integrate Fiber:

1. **GUI**: Add fiber settings panels in `src/slic3r/GUI/` ✨
2. **Main App**: Add option to choose `FiberPrint` ✨
3. **Config**: Add `FiberPrintConfig` to `PrintConfig.hpp` ✨
4. **Export**: Add fiber G-code export ✨

---

## Summary Table

| Aspect | SLA (Reference) | Fiber (Your Task) |
|--------|------------------|-------------------|
| **Main Class** | `SLAPrint` | `FiberPrint` ✨ |
| **Directory** | `SLA/` | `Fiber/` ✨ |
| **Steps File** | `SLAPrintSteps.hpp/cpp` | `FiberPrintSteps.hpp/cpp` ✨ |
| **Config** | `SLAPrintConfig` | `FiberPrintConfig` ✨ |
| **Output** | SL1 images | G-code with fiber commands ✨ |
| **Special Logic** | Supports, hollowing | **Path planning, continuous paths** ✨ |

---

## Action Items

1. ✅ Read `SLAPrint.hpp` - Understand structure
2. ✅ Read `SLAPrintSteps.cpp` - Understand processing
3. ✅ Create `FiberPrint.hpp` - Copy structure from SLA
4. ✅ Create `Fiber/` directory - Put your code here
5. ✅ Integrate your proprietary algorithms - Into the new structure

**The pattern is clear: Just follow what SLA did, but for Fiber!** 🎯

