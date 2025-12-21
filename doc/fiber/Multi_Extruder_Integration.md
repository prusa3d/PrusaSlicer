# Multi-Extruder Integration for Fiber Printing

## Overview

Fiber printing is integrated with PrusaSlicer's existing multi-extruder system. The fiber head is treated as a regular extruder (e.g., T1), allowing seamless integration with existing tool change logic.

## Architecture

### Extruder Assignment

- **Plastic Extruder**: Typically T0 (Extruder 0)
- **Fiber Extruder**: Typically T1 (Extruder 1)
- **Configuration**: Users can assign which extruder is plastic vs fiber

### Integration with PrusaSlicer Multi-Extruder System

The implementation leverages PrusaSlicer's existing multi-extruder infrastructure:

1. **Tool Changes**: Uses `GCodeWriter::set_extruder()` for tool changes
2. **Extruder Management**: Uses existing extruder state tracking
3. **Retraction/Wipe**: Automatic retraction and wipe handled by existing system
4. **Temperature Control**: Existing temperature management applies

## Configuration

### FiberGCodeConfig

```cpp
struct FiberGCodeConfig {
    unsigned int fiber_extruder_id = 1;    // T1 (default)
    unsigned int plastic_extruder_id = 0;   // T0 (default)
    // ... other settings
};
```

### Future: Multi-Fiber Support

The architecture is designed to be extensible for multiple fiber types:

```cpp
// Future feature (not yet implemented):
// std::map<FiberType, unsigned int> fiber_type_to_extruder;
// Example:
//   Carbon Fiber -> T1
//   Glass Fiber  -> T2
//   Kevlar       -> T3
```

## Tool Change Sequence

### Plastic → Fiber

1. **Automatic Retraction**: PrusaSlicer handles plastic retraction
2. **Tool Change**: `T1` command (via `GCodeWriter::set_extruder()`)
3. **Fiber Activation**: Optional fiber-specific commands (M106, etc.)
4. **Ready**: Fiber extruder is active

### Fiber → Plastic

1. **Fiber Deactivation**: Optional fiber-specific commands (M107, etc.)
2. **Tool Change**: `T0` command (via `GCodeWriter::set_extruder()`)
3. **Automatic Unretraction**: PrusaSlicer handles plastic unretraction
4. **Ready**: Plastic extruder is active

## G-Code Generation

### Example Sequence

```
; Layer 1: Plastic
T0                    ; Switch to plastic extruder
G1 X10 Y10 E5         ; Print plastic layer
...

; Layer 1: Fiber
T1                    ; Switch to fiber extruder (handled by GCodeWriter)
M106                  ; Activate fiber head (optional, configurable)
G1 X10 Y10            ; Lay fiber path
G1 X20 Y20            ; Continue fiber path
M107                  ; Deactivate fiber head (optional)
T0                    ; Switch back to plastic extruder
```

## Benefits of This Approach

1. **Reuses Existing Code**: Leverages PrusaSlicer's robust multi-extruder system
2. **Consistent Behavior**: Tool changes work the same way as multi-material printing
3. **Automatic Features**: Retraction, wipe, temperature control handled automatically
4. **Extensible**: Easy to add support for multiple fiber types in the future
5. **Compatible**: Works with existing printer configurations

## Implementation Details

### Key Methods

- `generate_toolchange_to_fiber()`: Uses `GCodeWriter::set_extruder(fiber_extruder_id)`
- `generate_toolchange_to_plastic()`: Uses `GCodeWriter::set_extruder(plastic_extruder_id)`
- Both methods integrate with existing retraction/wipe logic

### Print Sequence Integration

The `PrintSequence` from Phase 4 automatically handles tool changes:

- `PlasticLayer` step → Ensures plastic extruder is active
- `FiberLayer` step → Switches to fiber extruder, prints fiber, switches back
- Tool changes are inserted automatically based on sequence

## Future Enhancements

1. **Multiple Fiber Types**: Support T1=carbon, T2=glass, T3=kevlar
2. **Smart Tool Changes**: Optimize to avoid unnecessary tool changes
3. **Custom Tool Change G-code**: Allow user-defined tool change sequences
4. **Extruder-Specific Settings**: Different speeds/temperatures per fiber type

## Notes

- The fiber extruder is treated as a regular extruder in PrusaSlicer's system
- No special handling needed beyond configuration
- Works seamlessly with existing multi-extruder features (wipe tower, etc.)
- Compatible with single-extruder multi-material setups (future)

---

**Status**: ✅ Implemented and integrated with PrusaSlicer's multi-extruder system

