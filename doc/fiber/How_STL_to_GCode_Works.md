# How PrusaSlicer Converts STL to G-Code

## Overview

Yes, your understanding is correct! Here's the complete workflow:

```
1. Build the code → Creates executable
2. Launch GUI (or use CLI) → Load STL file
3. Configure settings → Slice → Export G-code
```

But there are **two ways** to use it: **GUI** and **Command Line**. Let me explain both!

---

## 🖥️ Method 1: GUI (Graphical User Interface)

### Workflow:

```
Build → Launch GUI → Load STL → Configure → Slice → Export G-code
```

### Step-by-Step:

1. **Build the Code**
   ```bash
   cd PrusaSlicer
   mkdir build && cd build
   cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DCMAKE_PREFIX_PATH=../deps/build/destdir/usr/local
   make -j4
   ```

2. **Launch GUI**
   ```bash
   cd src
   ./prusa-slicer
   ```
   Or on Windows: `PrusaSlicer.exe`

3. **Use the GUI**
   - **Load STL**: File → Open → Select your `.stl` file
   - **Configure**: Adjust print settings (layer height, infill, etc.)
   - **Slice**: Click "Slice Now" button
   - **Export**: Click "Export G-code" → Save `.gcode` file

### What Happens Behind the Scenes:

```
User loads STL
    ↓
GUI calls: FileReader::load_model("file.stl")
    ↓
STL file parsed → Model object created
    ↓
User clicks "Slice"
    ↓
GUI calls: Print::process()
    ↓
Print::process() does:
    1. Print::slice() - Slices model into layers
    2. Print::make_perimeters() - Generates perimeters
    3. Print::make_infill() - Generates infill
    4. Print::make_support() - Generates supports
    ↓
User clicks "Export G-code"
    ↓
GUI calls: Print::export_gcode("output.gcode")
    ↓
GCodeGenerator::do_export() writes G-code file
    ↓
G-code file saved! ✅
```

---

## 💻 Method 2: Command Line Interface (CLI)

### Workflow:

```
Build → Run CLI command → G-code generated automatically
```

### Step-by-Step:

1. **Build the Code** (same as GUI)

2. **Run CLI Command**
   ```bash
   ./prusa-slicer model.stl --slice --export-gcode -o output.gcode
   ```

   Or with more options:
   ```bash
   ./prusa-slicer model.stl \
     --load config.ini \
     --layer-height 0.2 \
     --infill 20 \
     --slice \
     --export-gcode \
     -o output.gcode
   ```

### What Happens Behind the Scenes:

```
CLI parses arguments
    ↓
CLI::run() called
    ↓
load_print_data() loads STL file
    ↓
FileReader::load_model("model.stl") → Model created
    ↓
process_actions() sees --slice flag
    ↓
Creates Print object
    ↓
print->process() - Slices model
    ↓
print->export_gcode() - Generates G-code
    ↓
G-code file written! ✅
```

---

## 📋 Detailed Process Flow

### 1. **Loading STL File**

**Code Location**: `src/libslic3r/Format/STL.cpp`

```cpp
bool load_stl(const char *path, Model *model)
{
    TriangleMesh mesh;
    mesh.ReadSTLFile(path);  // Reads STL file
    model->add_object(name, path, std::move(mesh));  // Adds to model
    return true;
}
```

**What it does:**
- Reads STL file (binary or ASCII)
- Parses triangles
- Creates `TriangleMesh` object
- Adds to `Model` object

---

### 2. **Slicing Process**

**Code Location**: `src/libslic3r/PrintObjectSlice.cpp`

```cpp
void PrintObject::slice()
{
    // 1. Generate layer heights
    std::vector<coordf_t> layer_height_profile;
    update_layer_height_profile(...);
    
    // 2. Create layers
    m_layers = new_layers(this, generate_object_layers(...));
    
    // 3. Slice volumes
    slice_volumes();
    
    // 4. Process modifiers
    // 5. Apply size compensation
    // 6. Fix bad slices
}
```

**What it does:**
- Takes 3D model (STL)
- Slices it into 2D layers at different Z heights
- Each layer contains polygons (contours and holes)
- Result: `Layer` objects with `ExPolygons`

---

### 3. **Path Generation**

**Code Location**: `src/libslic3r/Print.cpp`

```cpp
void Print::make_perimeters()
{
    // Generate perimeter paths
    for (auto& layer : layers) {
        layer->make_perimeters();  // Outer walls
        layer->make_fills();       // Infill
    }
}
```

**What it does:**
- Generates perimeter paths (outer walls)
- Generates infill paths (internal structure)
- Generates support paths (if needed)
- Creates `ExtrusionPath` objects

---

### 4. **G-Code Generation**

**Code Location**: `src/libslic3r/GCode.cpp`

```cpp
void GCodeGenerator::do_export(Print* print, const char* path)
{
    // Open file
    GCodeOutputStream file(fopen(path, "wb"));
    
    // Write G-code
    _do_export(*print, file);
    
    // Write header
    // Write start G-code
    // Write each layer:
    //   - Travel moves
    //   - Extrusion moves
    //   - Perimeters
    //   - Infill
    // Write end G-code
}
```

**What it does:**
- Takes sliced layers with paths
- Converts paths to G-code commands:
  - `G1 X10 Y20 E5` - Move and extrude
  - `G0 X10 Y20` - Travel move
  - `M104 S200` - Set temperature
- Writes to `.gcode` file

---

## 🔍 Code Flow Diagram

```
STL File
    ↓
[FileReader::load_model()]
    ↓
Model (3D mesh)
    ↓
[Print::process()]
    ↓
[PrintObject::slice()]
    ↓
Layers (2D polygons)
    ↓
[Print::make_perimeters()]
    ↓
ExtrusionPaths (toolpaths)
    ↓
[Print::export_gcode()]
    ↓
[GCodeGenerator::do_export()]
    ↓
G-code File ✅
```

---

## 📁 Key Files Involved

### Loading:
- `src/libslic3r/Format/STL.cpp` - STL file reader
- `src/libslic3r/FileReader.cpp` - File loading interface

### Slicing:
- `src/libslic3r/PrintObjectSlice.cpp` - Main slicing logic
- `src/libslic3r/TriangleMeshSlicer.cpp` - Mesh slicing algorithm
- `src/libslic3r/Slicing.hpp` - Slicing parameters

### Path Generation:
- `src/libslic3r/Print.cpp` - Print processing
- `src/libslic3r/LayerRegion.cpp` - Layer region processing
- `src/libslic3r/Fill/` - Infill pattern generators

### G-Code:
- `src/libslic3r/GCode.cpp` - G-code generation
- `src/libslic3r/GCode/GCodeWriter.cpp` - G-code writing
- `src/libslic3r/GCode/` - G-code utilities

### CLI:
- `src/CLI/Run.cpp` - CLI entry point
- `src/CLI/ProcessActions.cpp` - Action processing
- `src/CLI/LoadPrintData.cpp` - Data loading

### GUI:
- `src/slic3r/GUI/Plater.cpp` - Main GUI window
- `src/slic3r/GUI/BackgroundSlicingProcess.cpp` - Background slicing

---

## 🎯 For Your Fiber Implementation

When you add fiber printing, you'll follow the same pattern:

```
STL File
    ↓
Load Model (existing)
    ↓
FiberPrint::process()  ← YOUR NEW CODE
    ↓
Slice (reuse existing)
    ↓
Plan Fiber Paths  ← YOUR NEW CODE
    ↓
FiberPrint::export_gcode()  ← YOUR NEW CODE
    ↓
G-code with fiber commands ✅
```

### Integration Points:

1. **Loading**: Reuse existing `FileReader::load_model()`

2. **Slicing**: Reuse existing `PrintObject::slice()` or create `FiberPrintObject::slice()`

3. **Path Planning**: **NEW** - Your `FiberPathPlanner` class

4. **G-Code**: **NEW** - Your `FiberGCodeWriter` class that adds fiber commands

---

## 📝 Example CLI Usage

### Basic:
```bash
prusa-slicer model.stl --slice --export-gcode -o output.gcode
```

### With Config:
```bash
prusa-slicer model.stl \
  --load printer.ini \
  --load filament.ini \
  --load print.ini \
  --slice \
  --export-gcode \
  -o output.gcode
```

### With Options:
```bash
prusa-slicer model.stl \
  --layer-height 0.2 \
  --infill 20 \
  --perimeters 3 \
  --slice \
  --export-gcode \
  -o output.gcode
```

---

## ✅ Summary

**Your understanding is correct!**

1. ✅ **Build code** → Creates `prusa-slicer` executable
2. ✅ **Launch GUI** → `./prusa-slicer` (or use CLI)
3. ✅ **Load STL** → File → Open
4. ✅ **Slice** → Click "Slice Now" (or `--slice` in CLI)
5. ✅ **Export** → Click "Export G-code" (or `--export-gcode` in CLI)

**For Fiber Printing:**
- Same workflow, but your `FiberPrint` class handles the fiber-specific logic
- Your code integrates into the existing pipeline
- Users can use GUI or CLI just like regular printing!

---

## 🚀 Next Steps for Fiber

1. **Study the existing flow** - Understand how `Print::process()` works
2. **Create `FiberPrint`** - Follow the same pattern as `Print`
3. **Integrate path planning** - Your algorithms go here
4. **Add G-code generation** - Your fiber commands go here
5. **Add GUI support** - Users can select "Fiber Print" mode

The architecture is already set up for this! 🎯

