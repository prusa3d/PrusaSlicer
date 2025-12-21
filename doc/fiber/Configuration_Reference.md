# Fiber 3D Printing - Configuration Reference

## Complete List of Configuration Options

This document provides a complete reference for all fiber printing configuration options.

---

## Basic Settings

### enable_fiber_reinforcement
- **Type**: Boolean
- **Default**: `false`
- **Description**: Enable continuous fiber reinforcement for enhanced part strength
- **Example**: `enable_fiber_reinforcement = true`

### fiber_type
- **Type**: Enum
- **Default**: `Carbon`
- **Options**: `Carbon`, `Glass`, `Kevlar`, `Basalt`, `Custom`
- **Description**: Type of fiber material to use for reinforcement
- **Example**: `fiber_type = Glass`

### fiber_pattern
- **Type**: Enum
- **Default**: `Grid`
- **Options**: `Grid`, `Concentric`, `Custom`
- **Description**: Pattern used to place fiber strands within the print
- **Example**: `fiber_pattern = Concentric`

### fiber_spacing
- **Type**: Float (mm)
- **Default**: `5.0`
- **Range**: `0.1` - `50.0`
- **Description**: Spacing between adjacent fiber strands in millimeters
- **Example**: `fiber_spacing = 2.5`

### fiber_angle
- **Type**: Float (degrees)
- **Default**: `0.0`
- **Range**: `0.0` - `360.0`
- **Description**: Angle of fiber strands relative to the X-axis
- **Example**: `fiber_angle = 45.0`

---

## Layer Selection

### fiber_layer_interval
- **Type**: Integer
- **Default**: `1`
- **Range**: `1` - `100`
- **Description**: Place fiber reinforcement every N layers
- **Example**: `fiber_layer_interval = 2` (every 2nd layer)

### fiber_start_layer
- **Type**: Integer
- **Default**: `0`
- **Range**: `0` - `10000`
- **Description**: First layer to place fiber (0 = from first layer)
- **Example**: `fiber_start_layer = 5` (start from layer 5)

### fiber_end_layer
- **Type**: Integer
- **Default**: `999999` (all layers)
- **Range**: `0` - `10000`
- **Description**: Last layer to place fiber
- **Example**: `fiber_end_layer = 50` (stop at layer 50)

---

## Placement Zone

### fiber_placement_zone
- **Type**: Enum
- **Default**: `Both`
- **Options**: `Perimeter`, `Infill`, `Both`
- **Description**: Where to place fiber strands within the print
- **Example**: `fiber_placement_zone = Perimeter`

---

## Printing Method

### fiber_print_method
- **Type**: Enum
- **Default**: `DualPrinthead`
- **Options**: `DualPrinthead`, `CoExtrusion`, `PreEmbeddedFilament`
- **Description**: Method for printing fiber
- **Example**: `fiber_print_method = PreEmbeddedFilament`

### fiber_extruder_id
- **Type**: Integer
- **Default**: `1`
- **Range**: `0` - `15`
- **Description**: Extruder ID for fiber (Method 1 only)
- **Example**: `fiber_extruder_id = 1` (T1)

### plastic_extruder_id
- **Type**: Integer
- **Default**: `0`
- **Range**: `0` - `15`
- **Description**: Extruder ID for plastic (Method 1 only)
- **Example**: `plastic_extruder_id = 0` (T0)

### embedded_fiber_extruder_id
- **Type**: Integer
- **Default**: `0`
- **Range**: `0` - `15`
- **Description**: Extruder ID with embedded fiber (Method 2 only)
- **Example**: `embedded_fiber_extruder_id = 0`

---

## Print Sequence

### fiber_print_sequence
- **Type**: Enum
- **Default**: `PlasticFirst`
- **Options**: `PlasticFirst`, `Alternating`, `FiberOnTop`
- **Description**: Sequence strategy for printing plastic and fiber layers
- **Example**: `fiber_print_sequence = Alternating`

### fiber_delay_after_plastic
- **Type**: Float (seconds)
- **Default**: `0.0`
- **Range**: `0.0` - `60.0`
- **Description**: Delay after printing plastic before fiber placement
- **Example**: `fiber_delay_after_plastic = 2.0`

### fiber_cooling_time
- **Type**: Float (seconds)
- **Default**: `0.0`
- **Range**: `0.0` - `300.0`
- **Description**: Cooling time for plastic before fiber placement
- **Example**: `fiber_cooling_time = 5.0`

### fiber_wait_for_cooling
- **Type**: Boolean
- **Default**: `false`
- **Description**: Wait for plastic to cool before placing fiber
- **Example**: `fiber_wait_for_cooling = true`

---

## Speed and Pressure

### fiber_speed
- **Type**: Float (mm/s)
- **Default**: `50.0`
- **Range**: `1.0` - `500.0`
- **Description**: Printing speed for fiber placement
- **Example**: `fiber_speed = 30.0`

### fiber_pressure
- **Type**: Float
- **Default**: `0.0`
- **Range**: `0.0` - `100.0`
- **Description**: Pressure/tension for fiber (if applicable)
- **Example**: `fiber_pressure = 10.0`

---

## G-code Commands

### fiber_start_command
- **Type**: String
- **Default**: `"M106"`
- **Description**: G-code command to start fiber deposition
- **Example**: `fiber_start_command = "M800"`

### fiber_stop_command
- **Type**: String
- **Default**: `"M107"`
- **Description**: G-code command to stop fiber deposition
- **Example**: `fiber_stop_command = "M801"`

### fiber_speed_command
- **Type**: String
- **Default**: `"M108"`
- **Description**: G-code command for speed control
- **Example**: `fiber_speed_command = "M108"`

### fiber_enable_comments
- **Type**: Boolean
- **Default**: `true`
- **Description**: Enable comments in generated G-code
- **Example**: `fiber_enable_comments = false`

---

## Visualization

### fiber_path_color
- **Type**: String (hex color)
- **Default**: `"#FF0000"` (red)
- **Description**: Color for fiber paths in 3D preview
- **Example**: `fiber_path_color = "#00FF00"` (green)

### fiber_arrow_color
- **Type**: String (hex color)
- **Default**: `"#0000FF"` (blue)
- **Description**: Color for direction arrows in 3D preview
- **Example**: `fiber_arrow_color = "#FFFF00"` (yellow)

### fiber_arrow_density
- **Type**: Float
- **Default**: `10.0`
- **Range**: `1.0` - `100.0`
- **Description**: Density of direction arrows (arrows per 10mm)
- **Example**: `fiber_arrow_density = 5.0`

---

## Configuration Examples

### High-Strength Configuration

```ini
enable_fiber_reinforcement = true
fiber_type = Carbon
fiber_pattern = Grid
fiber_spacing = 2.0
fiber_angle = 0.0
fiber_layer_interval = 1
fiber_placement_zone = Both
fiber_speed = 40.0
```

### Lightweight Configuration

```ini
enable_fiber_reinforcement = true
fiber_type = Glass
fiber_pattern = Grid
fiber_spacing = 10.0
fiber_angle = 45.0
fiber_layer_interval = 3
fiber_placement_zone = Infill
fiber_speed = 50.0
```

### Method 1 (Dual Printhead) Configuration

```ini
enable_fiber_reinforcement = true
fiber_print_method = DualPrinthead
fiber_extruder_id = 1
plastic_extruder_id = 0
fiber_start_command = "M106"
fiber_stop_command = "M107"
fiber_print_sequence = PlasticFirst
```

### Method 2 (Pre-embedded) Configuration

```ini
enable_fiber_reinforcement = true
fiber_print_method = PreEmbeddedFilament
embedded_fiber_extruder_id = 0
fiber_speed = 50.0
```

---

## Configuration File Location

Fiber settings are stored in:
- **Print Settings**: `print/fiber_*.ini`
- **Printer Settings**: `printer/*.ini`
- **Material Settings**: `filament/*.ini` (for embedded fiber)

---

## Validation Rules

### Spacing Validation
- Must be positive: `fiber_spacing > 0`
- Recommended range: `0.5` - `10.0` mm

### Angle Validation
- Wrapped to 0-360 range
- Negative angles are converted: `-45°` → `315°`

### Layer Range Validation
- `fiber_start_layer < fiber_end_layer`
- `fiber_start_layer >= 0`
- `fiber_end_layer <= total_layers`

### Extruder ID Validation
- Must be valid extruder: `0 <= extruder_id < num_extruders`
- Cannot use same extruder for plastic and fiber (Method 1)

---

## Default Values Summary

| Setting | Default Value |
|---------|--------------|
| `enable_fiber_reinforcement` | `false` |
| `fiber_type` | `Carbon` |
| `fiber_pattern` | `Grid` |
| `fiber_spacing` | `5.0` mm |
| `fiber_angle` | `0.0`° |
| `fiber_layer_interval` | `1` |
| `fiber_start_layer` | `0` |
| `fiber_end_layer` | `999999` |
| `fiber_placement_zone` | `Both` |
| `fiber_print_method` | `DualPrinthead` |
| `fiber_extruder_id` | `1` |
| `plastic_extruder_id` | `0` |
| `fiber_speed` | `50.0` mm/s |
| `fiber_path_color` | `#FF0000` |
| `fiber_arrow_color` | `#0000FF` |
| `fiber_arrow_density` | `10.0` |

---

**Last Updated**: Phase 9 - Documentation & Polish
**Version**: 1.0

