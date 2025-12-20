# Fiber 3D Printing - Developer Guide

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Adding New Patterns](#adding-new-patterns)
3. [Extending the System](#extending-the-system)
4. [Code Structure](#code-structure)
5. [API Reference](#api-reference)
6. [Testing](#testing)

---

## Architecture Overview

The fiber printing system follows PrusaSlicer's modular architecture, similar to how SLA printing was integrated. The system consists of several key components:

### Core Components

1. **FiberPrint**: High-level FSM for fiber printing (similar to `SLAPrint`)
2. **FiberPrintObject**: Per-object fiber printing data (similar to `SLAPrintObject`)
3. **FiberPlacement**: Pattern generation and placement strategy
4. **PathPlanner**: Continuous path planning and optimization
5. **FiberPlasticCoordinator**: Coordination between plastic and fiber printing
6. **FiberGCodeWriter**: G-code generation for fiber printing
7. **FiberAdvanced**: Advanced features (variable speed, material database)

### Data Flow

```
STL/Model → Print (FFF) → FiberPrint
                ↓              ↓
         PrintObject    FiberPrintObject
                ↓              ↓
            Layer[]      FiberLayer[]
                ↓              ↓
         ExPolygons    FiberPath[]
                ↓              ↓
                    PathPlanner
                         ↓
                  ContinuousPath[]
                         ↓
                  FiberGCodeWriter
                         ↓
                      G-code
```

### Integration Points

- **PrintBaseWithState**: Base class for state management
- **PrintObjectBaseWithState**: Base class for object-level state
- **Multi-Extruder System**: Uses existing T0/T1 tool change system
- **Config System**: Uses PrusaSlicer's configuration framework

---

## Adding New Patterns

### Step 1: Define Pattern Enum

In `src/libslic3r/Fiber/FiberPlacement.hpp`:

```cpp
enum class FiberPattern {
    Grid,
    Concentric,
    Custom,  // Add your pattern here
};
```

### Step 2: Add Pattern Generation Method

In `src/libslic3r/Fiber/FiberPlacement.hpp`:

```cpp
class FiberPlacement {
    // ... existing methods ...
    
private:
    Polylines generate_custom_pattern(
        const ExPolygons& geometry, 
        const FiberPlacementConfig& config
    );
};
```

### Step 3: Implement Pattern Generation

In `src/libslic3r/Fiber/FiberPlacement.cpp`:

```cpp
Polylines FiberPlacement::generate_custom_pattern(
    const ExPolygons& geometry,
    const FiberPlacementConfig& config)
{
    Polylines result;
    
    // Your pattern generation logic here
    // Example: Generate custom paths based on geometry
    
    for (const ExPolygon& expoly : geometry) {
        // Generate paths for this polygon
        // Add to result
    }
    
    return result;
}
```

### Step 4: Add to Switch Statement

In `FiberPlacement::generate_paths_for_layer()`:

```cpp
switch (config.pattern) {
    case FiberPattern::Grid:
        polylines = generate_grid_pattern(layer_geometry, config);
        break;
    case FiberPattern::Concentric:
        polylines = generate_concentric_pattern(layer_geometry, config);
        break;
    case FiberPattern::Custom:
        polylines = generate_custom_pattern(layer_geometry, config);
        break;
    default:
        // ...
}
```

### Step 5: Add to Configuration

In `src/libslic3r/PrintConfig.cpp`:

```cpp
// Add to enum static map
{ "custom", int(FiberPatternType::fptCustom) },

// Add config option
def = this->add("fiber_pattern", coEnum, ...);
def->enum_values_map = &ConfigOptionEnum<FiberPatternType>::get_enum_values();
```

### Step 6: Add to GUI

In `src/slic3r/GUI/Tab.cpp` (TabFiber::build()):

```cpp
// Add to pattern selector
append_combo(pg, "fiber_pattern", L("Pattern"));
```

---

## Extending the System

### Adding New Fiber Types

1. **Update Enum** (`FiberAdvanced.hpp`):
```cpp
enum class FiberType {
    Carbon,
    Glass,
    Kevlar,
    Basalt,
    Custom,
    YourNewType,  // Add here
};
```

2. **Add Material Properties** (`FiberAdvanced.cpp`):
```cpp
FiberMaterialDatabase::FiberMaterialDatabase() {
    // ... existing materials ...
    
    FiberMaterialProperties your_type;
    your_type.type = FiberType::YourNewType;
    your_type.name = "Your New Fiber";
    your_type.diameter_mm = 0.1;
    your_type.density_g_per_cm3 = 1.5;
    // ... set other properties ...
    
    m_materials[FiberType::YourNewType] = your_type;
}
```

3. **Update Config** (`PrintConfig.cpp`):
```cpp
{ "your_new_type", int(FiberTypeEnum::fteYourNewType) },
```

### Adding New Print Methods

1. **Update Enum** (`FiberGCode.hpp`):
```cpp
enum class FiberPrintMethod {
    DualPrinthead,
    CoExtrusion,
    PreEmbeddedFilament,
    YourNewMethod,  // Add here
};
```

2. **Add Generation Method** (`FiberGCode.cpp`):
```cpp
std::string FiberGCodeWriter::generate_sequence_gcode_your_method(
    const PrintSequence& sequence,
    FiberPrintObject* fiber_object,
    GCodeWriter& writer,
    const FiberGCodeConfig& config)
{
    // Your method implementation
}
```

3. **Add to Switch** (`FiberGCode.cpp`):
```cpp
switch (config.method) {
    case FiberPrintMethod::DualPrinthead:
        return generate_sequence_gcode_dual_printhead(...);
    case FiberPrintMethod::YourNewMethod:
        return generate_sequence_gcode_your_method(...);
    // ...
}
```

### Adding New Path Planning Algorithms

1. **Extend PathPlanner** (`PathPlanner.hpp`):
```cpp
class PathPlanner {
    // ... existing methods ...
    
    ContinuousPaths plan_paths_your_algorithm(
        const FiberPaths& fiber_paths,
        const ExPolygons& layer_geometry,
        const PathPlanningConfig& config
    );
};
```

2. **Implement Algorithm** (`PathPlanner.cpp`):
```cpp
ContinuousPaths PathPlanner::plan_paths_your_algorithm(...) {
    // Your algorithm implementation
}
```

---

## Code Structure

### Directory Structure

```
src/libslic3r/
├── FiberPrint.hpp/cpp          # Main print class
├── FiberPrintConfig.hpp         # Configuration
├── FiberPrintSteps.hpp/cpp     # Step definitions
└── Fiber/
    ├── FiberLayer.hpp/cpp       # Layer data structures
    ├── FiberPrintObject.hpp/cpp # Print object
    ├── FiberPlacement.hpp/cpp  # Pattern generation
    ├── PathPlanner.hpp/cpp     # Path planning
    ├── FiberPlasticCoordinator.hpp/cpp  # Coordination
    ├── FiberGCode.hpp/cpp      # G-code generation
    ├── FiberAdvanced.hpp/cpp   # Advanced features
    └── FiberStatistics.hpp/cpp # Statistics

src/slic3r/GUI/
├── Tab.cpp                      # Settings tab (TabFiber)
├── GUI_Preview.cpp              # 3D visualization
├── GLCanvas3D.cpp               # OpenGL rendering
└── MainFrame.cpp                # Help menu

tests/fiber_print/
├── test_path_planning.cpp
├── test_pattern_generation.cpp
├── test_integration_*.cpp
└── test_gcode_*.cpp
```

### Key Classes

#### FiberPrint
- Main entry point for fiber printing
- Manages `FiberPrintObject` instances
- Handles configuration and processing

#### FiberPrintObject
- Per-object fiber printing data
- Links to `PrintObject` for FFF layer data
- Manages `FiberLayer` instances

#### FiberPlacement
- Pattern generation (grid, concentric, custom)
- Layer selection logic
- Placement zone filtering

#### PathPlanner
- Connects path segments into continuous paths
- Collision detection
- Path optimization and smoothing

#### FiberGCodeWriter
- G-code generation for fiber printing
- Tool change handling
- Method-specific G-code generation

---

## API Reference

### FiberPrint

```cpp
class FiberPrint : public PrintBaseWithState<FiberPrintStep, fiberpsCount> {
    // Apply model and configuration
    ApplyStatus apply(const Model &model, DynamicPrintConfig config, 
                     std::vector<std::string> *warnings = nullptr);
    
    // Process fiber printing
    void process();
    
    // Get print objects
    const PrintObjects& objects() const;
    
    // Get statistics
    FiberStatistics calculate_statistics(const DynamicPrintConfig* config = nullptr) const;
};
```

### FiberPrintObject

```cpp
class FiberPrintObject : public _FiberPrintObjectBase {
    // Create fiber layers from FFF layers
    void create_fiber_layers_from_fff_layers();
    
    // Generate fiber paths
    void generate_fiber_paths(const FiberPlacementConfig& config);
    
    // Plan continuous paths
    void plan_continuous_paths(const PathPlanningConfig& path_config);
    
    // Generate G-code
    std::string generate_gcode(const FiberGCodeConfig& config);
    
    // Get fiber layers
    const std::vector<FiberLayer>& fiber_layers() const;
};
```

### FiberPlacement

```cpp
class FiberPlacement {
    // Generate paths for a layer
    bool generate_paths_for_layer(FiberLayer& layer, 
                                   const ExPolygons& layer_geometry,
                                   const FiberPlacementConfig& config);
    
    // Check if layer should have fiber
    static bool should_place_fiber_on_layer(size_t layer_id, 
                                            const FiberPlacementConfig& config);
    
    // Get angle for layer
    static double get_angle_for_layer(size_t layer_id, 
                                       const FiberPlacementConfig& config);
};
```

### PathPlanner

```cpp
class PathPlanner {
    // Plan continuous paths
    ContinuousPaths plan_continuous_paths(
        const FiberPaths& fiber_paths,
        const ExPolygons& layer_geometry,
        const PathPlanningConfig& config
    );
    
    // Detect collisions
    bool detect_collisions(
        const ContinuousPath& path,
        const ExPolygons& layer_geometry,
        const ContinuousPaths& other_paths,
        const PathPlanningConfig& config,
        std::vector<std::string>& warnings
    ) const;
};
```

---

## Testing

### Running Tests

```bash
# Build tests
cd build
cmake ..
make fiber_print_tests

# Run tests
./tests/fiber_print/fiber_print_tests
```

### Test Structure

- **Unit Tests**: Test individual components
- **Integration Tests**: Test full pipeline
- **G-code Validation**: Test generated G-code

### Adding Tests

1. Create test file in `tests/fiber_print/`
2. Add to `CMakeLists.txt`
3. Use Catch2 framework:

```cpp
TEST_CASE("Your test", "[Tag]") {
    // Setup
    // ...
    
    // Test
    REQUIRE(condition);
}
```

---

## Best Practices

### Code Style

- Follow PrusaSlicer coding conventions
- Use meaningful variable names
- Add comments for complex logic
- Keep functions focused and small

### Error Handling

- Use `BOOST_LOG_TRIVIAL` for logging
- Return error codes or throw exceptions appropriately
- Validate inputs early
- Provide meaningful error messages

### Performance

- Avoid unnecessary copies (use references)
- Pre-allocate containers when size is known
- Use efficient algorithms (O(n log n) or better)
- Profile before optimizing

### Documentation

- Document public APIs
- Explain complex algorithms
- Add examples where helpful
- Keep documentation up to date

---

## Additional Resources

- **Architecture**: See `Fiber_Integration_Architecture.md`
- **Roadmap**: See `Fiber_Implementation_Roadmap.md`
- **Phase Summaries**: See `Phase*_Summary.md` files
- **PrusaSlicer Docs**: See main PrusaSlicer documentation

---

**Last Updated**: Phase 9 - Documentation & Polish
**Version**: 1.0

