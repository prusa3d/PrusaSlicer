# Fiber Printing Integration Architecture

## Overview

This document shows how fiber printing integrates with PrusaSlicer's existing architecture, following the same pattern as SLA printing.

---

## High-Level Architecture Flow

```mermaid
flowchart TD
    A[STL/OBJ/3MF File] --> B[FileReader::load_model]
    B --> C[Model Object Created]
    C --> D{Print Type?}
    
    D -->|FFF| E[Print::process]
    D -->|SLA| F[SLAPrint::process]
    D -->|Fiber| G[FiberPrint::process]
    
    E --> H[G-code Output]
    F --> I[SL1 Images]
    G --> J[G-code with Fiber Commands]
    
    style G fill:#90EE90
    style J fill:#90EE90
```

---

## Detailed Fiber Integration Flow

```mermaid
flowchart TD
    Start([User Loads STL File]) --> Load[FileReader::load_model]
    Load --> Model[Model Object<br/>3D Mesh Data]
    
    Model --> Select{User Selects<br/>Print Type}
    
    Select -->|Fiber Print| FiberInit[FiberPrint Created]
    Select -->|Regular FFF| FFFInit[Print Created]
    Select -->|SLA| SLAInit[SLAPrint Created]
    
    FiberInit --> FiberConfig[Load FiberPrintConfig<br/>- Fiber type<br/>- Pattern<br/>- Density<br/>- Angle]
    
    FiberConfig --> FiberProcess[FiberPrint::process]
    
    FiberProcess --> Slice[Slice Model into Layers<br/>Reuse PrintObject::slice]
    
    Slice --> Layers[Layer Objects<br/>2D Polygons per Z-height]
    
    Layers --> FiberPlacement[FiberPlacement::plan<br/>Decide where fiber goes]
    
    FiberPlacement --> Pattern{Pattern Type?}
    Pattern -->|Grid| Grid[GridPatternGenerator]
    Pattern -->|Concentric| Concentric[ConcentricPatternGenerator]
    Pattern -->|Custom| Custom[CustomPatternGenerator]
    
    Grid --> PathPlan[PathPlanner::generate_paths]
    Concentric --> PathPlan
    Custom --> PathPlan
    
    PathPlan --> Continuous[Generate Continuous Paths<br/>- No retractions<br/>- Collision-free<br/>- Optimized]
    
    Continuous --> FiberPaths[FiberPath Objects<br/>Per Layer]
    
    FiberPaths --> Coordinate[FiberPlasticCoordinator<br/>Sequence plastic + fiber]
    
    Coordinate --> Sequence{Print Sequence?}
    Sequence -->|Plastic First| PF[Print all plastic layers<br/>Then all fiber layers]
    Sequence -->|Alternating| Alt[Plastic layer → Fiber layer<br/>Repeat]
    Sequence -->|Fiber on Top| FOT[Plastic layer → Fiber immediately<br/>Repeat]
    
    PF --> GCodeGen[FiberGCodeWriter::export]
    Alt --> GCodeGen
    FOT --> GCodeGen
    
    GCodeGen --> GCode[G-code File<br/>- Standard FFF commands<br/>- Fiber commands M106/M107<br/>- Coordinated sequence]
    
    GCode --> End([G-code Ready for Printer])
    
    style FiberInit fill:#90EE90
    style FiberConfig fill:#90EE90
    style FiberProcess fill:#90EE90
    style FiberPlacement fill:#90EE90
    style PathPlan fill:#90EE90
    style Continuous fill:#90EE90
    style FiberPaths fill:#90EE90
    style Coordinate fill:#90EE90
    style GCodeGen fill:#90EE90
    style GCode fill:#90EE90
```

---

## Class Hierarchy Integration

```mermaid
classDiagram
    class PrintBase {
        <<abstract>>
        +process()
        +export_gcode()
    }
    
    class Print {
        +process()
        +export_gcode()
        -make_perimeters()
        -make_infill()
    }
    
    class SLAPrint {
        +process()
        +export_print()
        -slice_model()
        -generate_supports()
    }
    
    class FiberPrint {
        +process()
        +export_gcode()
        -plan_fiber_paths()
        -coordinate_plastic_fiber()
    }
    
    class FiberPlacement {
        +plan_placement()
        +select_pattern()
        +calculate_density()
    }
    
    class PathPlanner {
        +generate_paths()
        +check_collisions()
        +optimize_paths()
    }
    
    class FiberGCodeWriter {
        +export_gcode()
        +write_fiber_commands()
    }
    
    PrintBase <|-- Print
    PrintBase <|-- SLAPrint
    PrintBase <|-- FiberPrint
    
    FiberPrint --> FiberPlacement
    FiberPrint --> PathPlanner
    FiberPrint --> FiberGCodeWriter
    
    note for FiberPrint "Follows same pattern\nas SLAPrint"
```

---

## Data Flow: STL to G-code (FFF, SLA, Fiber in Parallel)

```mermaid
flowchart TD
    subgraph Input["Input"]
        STL[STL/OBJ/3MF File]
    end
    
    subgraph Loading["Loading Phase"]
        FR[FileReader::load_model]
        M[Model<br/>TriangleMesh]
    end
    
    STL --> FR
    FR --> M
    
    M --> Type{Print Type?}
    
    subgraph FFF["FFF Print Flow"]
        FFFSlice[PrintObject::slice]
        FFFLayers[Layers<br/>ExPolygons]
        FFFPerim[Make Perimeters]
        FFFInfill[Make Infill]
        FFFGC[GCodeGenerator]
        FFFOut[G-code File]
        
        FFFSlice --> FFFLayers
        FFFLayers --> FFFPerim
        FFFPerim --> FFFInfill
        FFFInfill --> FFFGC
        FFFGC --> FFFOut
    end
    
    subgraph SLA["SLA Print Flow"]
        SLAAssemble[SLAPrint::assemble]
        SLASlice[SLAPrint::slice]
        SLALayers[Layers<br/>ExPolygons]
        SLASupports[Generate Supports]
        SLATree[Support Tree]
        SLARaster[Rasterize]
        SLAOut[SL1 Images]
        
        SLAAssemble --> SLASlice
        SLASlice --> SLALayers
        SLALayers --> SLASupports
        SLASupports --> SLATree
        SLATree --> SLARaster
        SLARaster --> SLAOut
    end
    
    subgraph Fiber["Fiber Print Flow"]
        FiberSlice[PrintObject::slice<br/>Reuse FFF slicing]
        FiberLayers[Layers<br/>ExPolygons]
        FiberPlace[FiberPlacement<br/>Decide where fiber goes]
        FiberPattern[Pattern Generator<br/>Grid/Concentric/Custom]
        FiberPath[PathPlanner<br/>Generate continuous paths]
        FiberPaths[FiberPath Objects]
        FiberCoord[FiberPlasticCoordinator<br/>Sequence plastic + fiber]
        FiberGC[FiberGCodeWriter]
        FiberOut[G-code with Fiber Commands]
        
        FiberSlice --> FiberLayers
        FiberLayers --> FiberPlace
        FiberPlace --> FiberPattern
        FiberPattern --> FiberPath
        FiberPath --> FiberPaths
        FiberPaths --> FiberCoord
        FiberCoord --> FiberGC
        FiberGC --> FiberOut
    end
    
    Type -->|FFF| FFFSlice
    Type -->|SLA| SLAAssemble
    Type -->|Fiber| FiberSlice
    
    style FiberPlace fill:#90EE90
    style FiberPattern fill:#90EE90
    style FiberPath fill:#90EE90
    style FiberPaths fill:#90EE90
    style FiberCoord fill:#90EE90
    style FiberGC fill:#90EE90
    style FiberOut fill:#90EE90
```

---

## Integration Points with Existing Code

```mermaid
flowchart TD
    subgraph Existing["Existing PrusaSlicer Code"]
        A1[FileReader]
        A2[PrintObject::slice]
        A3[GCodeGenerator]
        A4[PrintConfig]
    end
    
    subgraph New["New Fiber Code"]
        B1[FiberPrint]
        B2[FiberPlacement]
        B3[PathPlanner]
        B4[FiberGCodeWriter]
        B5[FiberPrintConfig]
    end
    
    A1 -->|Reuse| B1
    A2 -->|Reuse| B1
    A4 -->|Extend| B5
    B1 -->|Uses| B2
    B1 -->|Uses| B3
    B1 -->|Uses| B4
    B4 -->|Extends| A3
    
    style B1 fill:#90EE90
    style B2 fill:#90EE90
    style B3 fill:#90EE90
    style B4 fill:#90EE90
    style B5 fill:#90EE90
```

---

## Processing Steps Comparison

```mermaid
flowchart TD
    subgraph FFF["FFF Print Flow"]
        F1[Load Model]
        F2[Slice]
        F3[Make Perimeters]
        F4[Make Infill]
        F5[Export G-code]
        F1 --> F2 --> F3 --> F4 --> F5
    end
    
    subgraph SLA["SLA Print Flow"]
        S1[Load Model]
        S2[Assemble]
        S3[Slice]
        S4[Generate Supports]
        S5[Export SL1]
        S1 --> S2 --> S3 --> S4 --> S5
    end
    
    subgraph Fiber["Fiber Print Flow"]
        FB1[Load Model]
        FB2[Slice]
        FB3[Plan Fiber Placement]
        FB4[Generate Fiber Paths]
        FB5[Coordinate Plastic+Fiber]
        FB6[Export G-code with Fiber]
        FB1 --> FB2 --> FB3 --> FB4 --> FB5 --> FB6
    end
    
    style FB3 fill:#90EE90
    style FB4 fill:#90EE90
    style FB5 fill:#90EE90
    style FB6 fill:#90EE90
```

---

## File Structure Integration

```mermaid
graph TD
    Root[PrusaSlicer Root] --> Src[src/]
    Root --> Docs[doc/]
    
    Src --> LibSlic3r[libslic3r/]
    Src --> Slic3r[slic3r/GUI/]
    
    LibSlic3r --> Print[Print.hpp/cpp<br/>FFF Printing]
    LibSlic3r --> SLAPrint[SLAPrint.hpp/cpp<br/>SLA Printing]
    LibSlic3r --> FiberPrint[FiberPrint.hpp/cpp<br/>Fiber Printing]
    LibSlic3r --> PrintBase[PrintBase.hpp<br/>Common Interface]
    
    LibSlic3r --> SLA[SLA/<br/>SLA-specific code]
    LibSlic3r --> Fiber[Fiber/<br/>Fiber-specific code]
    
    Fiber --> FP[FiberPlacement.hpp]
    Fiber --> PP[PathPlanner.hpp]
    Fiber --> FC[FiberGCode.hpp]
    
    Slic3r --> GUI[GUI Components]
    GUI --> FiberGUI[Fiber Settings Panels]
    
    style FiberPrint fill:#90EE90
    style Fiber fill:#90EE90
    style FP fill:#90EE90
    style PP fill:#90EE90
    style FC fill:#90EE90
    style FiberGUI fill:#90EE90
```

---

## Configuration Flow

```mermaid
flowchart TD
    Start([User Configures]) --> GUI{GUI or CLI?}
    
    GUI -->|GUI| GUISettings[GUI Settings Panels]
    GUI -->|CLI| CLIArgs[CLI Arguments<br/>--fiber-type, --fiber-density]
    
    GUISettings --> Preset[Select/Create Preset]
    CLIArgs --> Preset
    
    Preset --> Load[Load Preset from<br/>resources/profiles/ or<br/>user presets/]
    
    Load --> Config[FiberPrintConfig Object]
    
    Config --> FiberPrint[FiberPrint::process<br/>Uses config values]
    
    FiberPrint --> Slice[Slice with config]
    FiberPrint --> Plan[Plan fiber with config]
    FiberPrint --> Generate[Generate G-code with config]
    
    style Config fill:#90EE90
    style FiberPrint fill:#90EE90
```

---

## G-code Generation Flow

```mermaid
flowchart TD
    Start([FiberPrint Ready]) --> Init[FiberGCodeWriter::export]
    
    Init --> Header[Write G-code Header<br/>- Printer setup<br/>- Temperature<br/>- Start G-code]
    
    Header --> Loop{For Each Layer}
    
    Loop --> PlasticLayer[Write Plastic Layer<br/>- Standard FFF G-code<br/>- G1 moves<br/>- Extrusion]
    
    PlasticLayer --> FiberCheck{Fiber in this layer?}
    
    FiberCheck -->|Yes| FiberStart[Write Fiber Start<br/>M106 or custom command]
    
    FiberStart --> FiberLayer[Write Fiber Layer<br/>- Continuous paths<br/>- G1 moves<br/>- Fiber commands]
    
    FiberLayer --> FiberStop[Write Fiber Stop<br/>M107 or custom command]
    
    FiberStop --> NextLayer[Next Layer]
    FiberCheck -->|No| NextLayer
    
    NextLayer --> MoreLayers{More Layers?}
    MoreLayers -->|Yes| Loop
    MoreLayers -->|No| Footer[Write G-code Footer<br/>- End G-code<br/>- Printer shutdown]
    
    Footer --> End([G-code File Complete])
    
    style FiberStart fill:#90EE90
    style FiberLayer fill:#90EE90
    style FiberStop fill:#90EE90
```

---

## Key Integration Points

### 1. **Extends PrintBase** (Like SLA)
- `FiberPrint` extends `PrintBase`
- Implements required virtual methods
- Follows same interface pattern

### 2. **Reuses Existing Slicing**
- Uses `PrintObject::slice()` or similar
- Reuses layer generation logic
- Leverages existing mesh processing

### 3. **New Fiber-Specific Logic**
- `FiberPlacement` - Decides where fiber goes
- `PathPlanner` - Generates continuous paths
- `FiberGCodeWriter` - Outputs fiber commands

### 4. **Configuration System**
- Extends `PrintConfig` with `FiberPrintConfig`
- Uses preset system (INI files)
- Supports CLI arguments

### 5. **GUI Integration**
- New settings panels for fiber
- 3D visualization of fiber paths
- Preset management

---

## Summary

The fiber printing integration follows the **exact same pattern as SLA**:

1. ✅ **Separate Module**: `FiberPrint` class (like `SLAPrint`)
2. ✅ **Dedicated Directory**: `src/libslic3r/Fiber/` (like `SLA/`)
3. ✅ **Processing Steps**: `FiberPrintSteps` (like `SLAPrintSteps`)
4. ✅ **Configuration**: `FiberPrintConfig` (like `SLAPrintConfig`)
5. ✅ **G-code Output**: Custom G-code generation (like SLA's SL1 export)

The integration is **modular and non-intrusive** - it doesn't modify existing FFF code, just adds a new parallel system! 🎯

