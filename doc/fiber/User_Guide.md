# Fiber 3D Printing - User Guide

## Table of Contents
1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Configuration Guide](#configuration-guide)
4. [Printing Methods](#printing-methods)
5. [Patterns and Settings](#patterns-and-settings)
6. [Troubleshooting](#troubleshooting)
7. [Best Practices](#best-practices)

---

## Introduction

Fiber 3D printing allows you to reinforce plastic prints with continuous fiber strands (carbon, glass, or Kevlar) for enhanced strength. This feature integrates seamlessly with PrusaSlicer's existing FFF printing workflow.

### Key Features

- **Multiple Fiber Types**: Carbon fiber, Glass fiber, Kevlar, and custom materials
- **Pattern Options**: Grid and concentric patterns
- **Multiple Printing Methods**: Dual printhead (Method 1) and embedded filament (Method 2)
- **3D Visualization**: Preview fiber paths before printing
- **Statistics**: View fiber usage, print time, and validation warnings
- **Flexible Configuration**: Control spacing, angle, layer selection, and more

---

## Getting Started

### Enabling Fiber Reinforcement

1. Open your 3D model in PrusaSlicer
2. Go to the **Fiber Reinforcement** settings tab
3. Enable **"Enable Fiber Reinforcement"**
4. Configure your fiber settings (see [Configuration Guide](#configuration-guide))
5. Slice your model as usual

### Quick Start Example

For a simple reinforced print:

1. **Fiber Type**: Select "Carbon fiber" for maximum strength
2. **Pattern**: Choose "Grid" for uniform reinforcement
3. **Spacing**: Set to 5mm for balanced strength and material usage
4. **Angle**: Use 0° (horizontal) or 90° (vertical)
5. **Layer Interval**: Set to 1 (every layer) for maximum strength

---

## Configuration Guide

### Basic Settings

#### Fiber Type
- **Carbon Fiber**: Highest strength, best for structural parts
- **Glass Fiber**: More flexible, good for general reinforcement
- **Kevlar**: Impact resistance, good for protective parts

#### Pattern Type
- **Grid**: Parallel lines at specified angle. Best for flat parts and uniform strength
- **Concentric**: Follows part contours. Best for cylindrical objects and curved surfaces

#### Spacing
- Distance between adjacent fiber strands (in mm)
- **Smaller values** (0.5-2mm): Higher density, more strength, more material
- **Larger values** (5-10mm): Lower density, less material, faster printing
- **Recommended**: 2-5mm for most applications

#### Angle
- Direction of fiber strands relative to X-axis
- **0°**: Horizontal (along X-axis)
- **90°**: Vertical (along Y-axis)
- **45°**: Diagonal
- **Tip**: Use different angles on different layers for multi-directional strength

### Advanced Settings

#### Layer Selection
- **Layer Interval**: Place fiber every N layers (1 = every layer)
- **Start Layer**: First layer to place fiber (0 = from first layer)
- **End Layer**: Last layer to place fiber (leave empty for all layers)

#### Placement Zone
- **Perimeter Only**: Fiber only in outer walls
- **Infill Only**: Fiber only in infill areas
- **Both**: Fiber in both perimeters and infill (recommended for maximum strength)

#### Print Sequence
- **Plastic First**: Print all plastic for a layer, then all fiber
- **Alternating**: Alternate between plastic and fiber segments
- **Fiber on Top**: Print fiber immediately after each plastic segment

### Printing Method Settings

#### Method 1: Dual Printhead
- **Fiber Extruder ID**: Extruder number for fiber (typically 1, T1)
- **Plastic Extruder ID**: Extruder number for plastic (typically 0, T0)
- **Fiber Start Command**: G-code command to start fiber (default: M106)
- **Fiber Stop Command**: G-code command to stop fiber (default: M107)

#### Method 2: Pre-embedded Filament
- **Embedded Fiber Extruder ID**: Extruder with pre-embedded fiber filament
- Fiber is automatically embedded during plastic extrusion

---

## Printing Methods

### Method 1: Dual Printhead

**Best for**: Printers with separate extruders for plastic and fiber

**How it works**:
- Uses tool changes (T0 for plastic, T1 for fiber)
- Plastic and fiber are printed separately
- Allows independent control of each material

**Setup**:
1. Configure your printer with multiple extruders
2. Assign extruder 0 to plastic, extruder 1 to fiber
3. Set "Fiber Print Method" to "Dual Printhead"
4. Configure fiber start/stop commands if needed

### Method 2: Pre-embedded Filament

**Best for**: Filament with continuous fiber already embedded

**How it works**:
- Uses single extruder with special filament
- Fiber orientation follows print path direction
- No tool changes needed

**Setup**:
1. Load pre-embedded fiber filament
2. Set "Fiber Print Method" to "Pre-embedded Filament"
3. Configure embedded fiber extruder ID

---

## Patterns and Settings

### Grid Pattern

**Best for**: Flat parts, uniform strength distribution

**Configuration**:
- **Spacing**: 2-5mm recommended
- **Angle**: 0° or 90° for simple parts, 45° for diagonal reinforcement
- **Layer Interval**: 1 for maximum strength, 2-3 for lighter reinforcement

**Example Use Cases**:
- Flat brackets and plates
- Structural supports
- Parts requiring uniform strength

### Concentric Pattern

**Best for**: Cylindrical objects, curved surfaces, following contours

**Configuration**:
- **Spacing**: 2-5mm recommended
- Works best with layer interval of 1
- Automatically follows part geometry

**Example Use Cases**:
- Cylindrical parts (pipes, tubes)
- Curved surfaces
- Parts where fiber should follow contours

### Multi-Directional Strength

For maximum strength in all directions:

1. **Layer 0**: Angle = 0°
2. **Layer 1**: Angle = 90°
3. **Layer 2**: Angle = 45°
4. **Layer 3**: Angle = 135°
5. Repeat pattern

This creates a cross-ply pattern similar to composite materials.

---

## Troubleshooting

### No Fiber Paths Generated

**Symptoms**: Fiber reinforcement enabled but no paths in preview

**Solutions**:
1. Check layer range settings (start/end layer)
2. Verify layer interval is set correctly
3. Ensure model has printable geometry
4. Check that spacing is not too large for part size

### Fiber Paths Outside Part

**Symptoms**: Fiber paths appear outside the part boundaries

**Solutions**:
1. Enable collision detection in path planning
2. Check that layer geometry is correct
3. Verify placement zone settings

### Tool Changes Not Working

**Symptoms**: Method 1 not switching between extruders

**Solutions**:
1. Verify extruder IDs are correct (0 for plastic, 1 for fiber)
2. Check printer firmware supports tool changes
3. Verify fiber start/stop commands are correct
4. Check G-code for T0/T1 commands

### Poor Fiber Adhesion

**Symptoms**: Fiber not sticking to plastic

**Solutions**:
1. Reduce fiber speed (try 30-40 mm/s)
2. Increase fiber pressure/tension if available
3. Ensure plastic layer is properly cooled before fiber placement
4. Check fiber start/stop timing
5. Verify print temperature compatibility

### Performance Issues

**Symptoms**: Slicing takes too long or uses too much memory

**Solutions**:
1. Increase fiber spacing (fewer paths)
2. Increase layer interval (fewer layers with fiber)
3. Use simpler patterns (grid instead of concentric for large parts)
4. Reduce model complexity if possible

---

## Best Practices

### Material Selection

- **Carbon Fiber**: Use for maximum strength and stiffness
- **Glass Fiber**: Use for flexibility and impact resistance
- **Kevlar**: Use for protective applications

### Pattern Selection

- **Grid**: Best for flat parts and uniform reinforcement
- **Concentric**: Best for curved surfaces and following contours
- **Multi-directional**: Use alternating angles for maximum strength

### Spacing Guidelines

- **High Strength**: 0.5-2mm spacing, every layer
- **Balanced**: 2-5mm spacing, every layer or every 2nd layer
- **Lightweight**: 5-10mm spacing, every 2nd-3rd layer

### Layer Selection

- **Maximum Strength**: Every layer (interval = 1)
- **Balanced**: Every 2nd layer (interval = 2)
- **Lightweight**: Every 3rd-5th layer

### Print Settings

- **Fiber Speed**: 30-50 mm/s for good adhesion
- **Plastic Speed**: Match your normal print speed
- **Cooling**: Ensure adequate cooling between plastic and fiber layers
- **Temperature**: Use compatible temperatures for both materials

### Design Considerations

1. **Part Orientation**: Consider fiber direction when orienting parts
2. **Layer Height**: Standard layer heights (0.2-0.3mm) work well
3. **Wall Thickness**: Ensure sufficient wall thickness for fiber placement
4. **Overhangs**: Fiber may not adhere well to unsupported overhangs

---

## Examples

### Example 1: High-Strength Bracket

**Settings**:
- Fiber Type: Carbon fiber
- Pattern: Grid
- Spacing: 2mm
- Angle: 0° (layer 0), 90° (layer 1), alternating
- Layer Interval: 1
- Placement Zone: Both

### Example 2: Lightweight Reinforcement

**Settings**:
- Fiber Type: Glass fiber
- Pattern: Grid
- Spacing: 5mm
- Angle: 45°
- Layer Interval: 2
- Placement Zone: Infill only

### Example 3: Cylindrical Part

**Settings**:
- Fiber Type: Carbon fiber
- Pattern: Concentric
- Spacing: 3mm
- Layer Interval: 1
- Placement Zone: Both

---

## Additional Resources

- **Developer Guide**: See `Developer_Guide.md` for extending the system
- **Architecture Overview**: See `Fiber_Integration_Architecture.md`
- **Configuration Reference**: See `Configuration_Reference.md`
- **FAQ**: See `FAQ.md` for common questions

---

**Last Updated**: Phase 9 - Documentation & Polish
**Version**: 1.0

