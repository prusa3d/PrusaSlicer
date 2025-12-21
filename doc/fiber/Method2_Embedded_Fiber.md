# Method 2: Embedded Fiber Implementation

## Overview

Method 2 supports fiber that is embedded in the plastic during extrusion, as opposed to Method 1 where fiber is laid on top in separate layers.

## Two Variants

### Method 2a: Co-Extrusion (Placeholder)
- **Status**: Placeholder for future implementation
- **How it works**: Special printhead feeds continuous fiber into plastic during extrusion
- **G-code**: Co-extrusion commands (to be implemented)
- **Use case**: Printers with co-extrusion printheads

### Method 2b: Pre-Embedded Fiber Filament (Implemented)
- **Status**: ✅ Implemented
- **How it works**: Filament spool already contains continuous fiber embedded in it
- **G-code**: Fiber activation commands embedded in plastic extrusion
- **Use case**: Printers using pre-embedded fiber filaments (e.g., some Anisoprint systems)

## Key Differences from Method 1

| Feature | Method 1 (Dual Printhead) | Method 2 (Embedded) |
|---------|---------------------------|---------------------|
| Tool Changes | Yes (T0 ↔ T1) | No |
| Fiber Layer | Separate layer on top | Embedded in plastic extrusion |
| Fiber Orientation | Independent paths | Follows print path direction |
| G-code Structure | Separate plastic/fiber sections | Fiber commands embedded in extrusion |
| Print Sequence | Plastic layer → Fiber layer | Combined (fiber in plastic) |

## Implementation Details

### Configuration

```cpp
struct FiberGCodeConfig {
    FiberPrintMethod method = FiberPrintMethod::PreEmbeddedFilament;
    unsigned int embedded_fiber_extruder_id = 0;  // Extruder with fiber filament
    // ... other settings
};
```

### G-Code Generation

For Method 2b (PreEmbeddedFilament):
- No tool changes needed
- Fiber activation (M106) embedded before extrusion
- Fiber deactivation (M107) embedded after extrusion
- Fiber orientation automatically follows print path direction

Example G-code:
```
; Layer 1 - Plastic with embedded fiber
T0                    ; Use extruder with fiber filament
M106                  ; Activate fiber
G1 X10 Y10 E5         ; Extrude plastic (fiber embedded, follows path direction)
G1 X20 Y20 E10        ; Continue extrusion (fiber continues)
M107                  ; Deactivate fiber
```

### Print Sequence

For Method 2, the print sequence is simplified:
- `PlasticLayer` steps include fiber (if fiber is active for that layer)
- `FiberLayer` steps are informational (fiber paths are used for planning)
- No separate tool changes between plastic and fiber

## Future: Method 2a (Co-Extrusion)

When implemented, Method 2a will:
- Support special co-extrusion printheads
- Generate co-extrusion G-code commands
- Handle fiber feed rate synchronization with plastic extrusion
- Support different fiber feed mechanisms

## Usage

To use Method 2b (PreEmbeddedFilament):

1. Set `FiberGCodeConfig::method = FiberPrintMethod::PreEmbeddedFilament`
2. Set `embedded_fiber_extruder_id` to the extruder with fiber filament
3. Generate print sequence (fiber paths will align with plastic paths)
4. G-code will automatically embed fiber commands in extrusion

## Notes

- Method 2b assumes fiber orientation follows print path direction
- Fiber paths are used for planning/visualization but not separate tool changes
- Integration with FFF G-code generation is needed for full functionality
- Method 2a is a placeholder and will be implemented in a future update

---

**Status**: 
- Method 2a (Co-Extrusion): ⏳ Placeholder
- Method 2b (PreEmbeddedFilament): ✅ Implemented

