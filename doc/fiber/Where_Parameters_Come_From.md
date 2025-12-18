# Where PrusaSlicer Gets Its Parameters From

## Overview

PrusaSlicer gets parameters from **multiple sources**, in a specific priority order. Yes, there are default files, but also much more!

---

## 📋 Parameter Sources (Priority Order)

### 1. **Hardcoded Defaults** (Lowest Priority - Base Values)
**Location**: `src/libslic3r/PrintConfig.cpp`

```cpp
FullPrintConfig::defaults()  // Returns default values for all settings
```

**What it is:**
- Hardcoded default values in C++ code
- Used when no other source provides a value
- Example: `layer_height = 0.2` (default)

**Code Location:**
- `src/libslic3r/PrintConfig.cpp` - `FullPrintConfig::defaults()`
- `src/libslic3r/PrintConfig.hpp` - Default value definitions

---

### 2. **System Presets** (Built-in Profiles)
**Location**: `resources/profiles/*.ini`

**Files:**
- `resources/profiles/PrusaResearch.ini` - Prusa printer profiles
- `resources/profiles/Creality.ini` - Creality printer profiles
- `resources/profiles/Voron.ini` - Voron printer profiles
- `resources/profiles/Templates.ini` - Material profiles
- And many more...

**What they contain:**
- Printer profiles (bed size, nozzle, speeds, etc.)
- Print profiles (layer height, infill, perimeters, etc.)
- Filament profiles (temperature, flow, etc.)

**Example from `PrusaResearch.ini`:**
```ini
[printer:MK4S]
name = Original Prusa MK4S
bed_shape = 250x210
nozzle_diameter = 0.4
max_layer_height = 0.3
min_layer_height = 0.05
```

**How it's loaded:**
```cpp
// src/libslic3r/PresetBundle.cpp
PresetBundle::load_presets()
{
    // Loads from resources/profiles/
    this->load_system_presets();
    // Then loads user presets
    this->prints.load_presets(dir_user_presets, "print");
}
```

---

### 3. **User Presets** (User Customizations)
**Location**: User data directory

**Linux**: `~/.config/PrusaSlicer/` or `~/.Slic3r/`
**Windows**: `%APPDATA%\PrusaSlicer\`
**macOS**: `~/Library/Application Support/PrusaSlicer/`

**Files:**
- `presets/print/*.ini` - User print profiles
- `presets/filament/*.ini` - User filament profiles
- `presets/printer/*.ini` - User printer profiles

**What they do:**
- Override system presets
- User's custom settings
- Saved from GUI

---

### 4. **3MF Project Files** (Embedded Config)
**Location**: Inside `.3mf` files

**What they contain:**
- Model geometry
- **Print settings** (embedded in the file)
- **Printer settings**
- **Filament settings**

**How it works:**
```cpp
// src/libslic3r/FileReader.cpp
FileReader::load_model_with_config(file, &config, ...)
{
    // Loads model AND config from 3MF file
    load_3mf(file, config, &model);
}
```

**Priority:** Higher than presets (overrides them)

---

### 5. **Command Line Arguments** (Highest Priority)
**Location**: CLI arguments

**Examples:**
```bash
prusa-slicer model.stl \
  --layer-height 0.3 \      # Overrides preset
  --infill 30 \             # Overrides preset
  --load my_config.ini      # Loads custom config file
```

**How it works:**
```cpp
// src/CLI/Setup.cpp
CLI::read(argc, argv)
{
    // Parses --layer-height, --infill, etc.
    // Stores in overrides_config
}
```

**Priority:** Highest - overrides everything!

---

### 6. **Config Files via CLI** (`--load`)
**Location**: User-specified `.ini` files

**Example:**
```bash
prusa-slicer model.stl --load my_printer.ini --load my_filament.ini
```

**What they contain:**
- Full or partial configuration
- Can override presets

**Priority:** Very high (almost highest)

---

## 🔄 Parameter Loading Flow

```
1. Start with hardcoded defaults
   ↓
2. Load system presets (resources/profiles/*.ini)
   ↓
3. Load user presets (~/.config/PrusaSlicer/presets/)
   ↓
4. If loading 3MF: Apply embedded config
   ↓
5. Apply CLI arguments (--layer-height, etc.)
   ↓
6. Apply --load config files
   ↓
Final Configuration ✅
```

---

## 📁 File Structure

### System Presets (Built-in):
```
resources/
└── profiles/
    ├── PrusaResearch.ini      ← Prusa printers
    ├── Creality.ini            ← Creality printers
    ├── Voron.ini              ← Voron printers
    ├── Templates.ini          ← Material profiles
    └── ... (many more)
```

### User Presets (User Data):
```
~/.config/PrusaSlicer/
├── presets/
│   ├── print/
│   │   ├── My Custom Print.ini
│   │   └── ...
│   ├── filament/
│   │   ├── My PLA.ini
│   │   └── ...
│   └── printer/
│       ├── My Printer.ini
│       └── ...
└── config.ini                 ← Current selections
```

---

## 🎯 How It Works in Code

### 1. **Default Values**
```cpp
// src/libslic3r/PrintConfig.cpp
FullPrintConfig::defaults()
{
    // Returns default values
    layer_height = 0.2;
    infill = 20;
    perimeters = 3;
    // ... etc
}
```

### 2. **Loading Presets**
```cpp
// src/libslic3r/PresetBundle.cpp
PresetBundle::load_presets()
{
    // 1. Load system presets
    load_system_presets();  // From resources/profiles/
    
    // 2. Load user presets
    prints.load_presets(dir_user_presets, "print");
    filaments.load_presets(dir_user_presets, "filament");
    printers.load_presets(dir_user_presets, "printer");
}
```

### 3. **Applying Config**
```cpp
// src/CLI/LoadPrintData.cpp
load_print_data()
{
    // Start with defaults
    DynamicPrintConfig config = FullPrintConfig::defaults();
    
    // Apply preset
    config.apply(preset.config);
    
    // Apply CLI overrides
    config.apply(cli.overrides_config);
    
    // Final config ready!
}
```

---

## 📝 Example: What Happens When You Slice

### Scenario: User slices with CLI

```bash
prusa-slicer model.stl --layer-height 0.3 --infill 30
```

**What happens:**

1. **Load defaults:**
   ```cpp
   layer_height = 0.2  // from defaults
   infill = 20         // from defaults
   ```

2. **Load preset** (if `--load` not specified, uses default preset):
   ```cpp
   layer_height = 0.2  // from preset (same as default)
   infill = 20         // from preset
   perimeters = 3      // from preset
   ```

3. **Apply CLI arguments:**
   ```cpp
   layer_height = 0.3  // CLI override ✅
   infill = 30         // CLI override ✅
   perimeters = 3      // unchanged
   ```

**Final config:** `layer_height=0.3`, `infill=30`, `perimeters=3`

---

## 🎨 GUI vs CLI

### GUI:
1. Loads presets from files
2. User selects preset from dropdown
3. User can modify settings
4. Settings saved to user preset

### CLI:
1. Loads presets from files
2. Uses default preset (or `--load`)
3. CLI arguments override preset
4. No saving (just generates G-code)

---

## 🔍 Finding Default Values

### Method 1: Check Code
```cpp
// src/libslic3r/PrintConfig.cpp
FullPrintConfig::defaults()
{
    // All default values here
}
```

### Method 2: Check Preset Files
```ini
# resources/profiles/PrusaResearch.ini
[print:*common*]
layer_height = 0.2
infill = 20
perimeters = 3
```

### Method 3: Use CLI
```bash
prusa-slicer --help
# Shows all available options and defaults
```

---

## 🎯 For Your Fiber Implementation

When you add fiber printing, you'll need to:

### 1. **Add Default Values**
```cpp
// src/libslic3r/PrintConfig.cpp
FiberPrintConfig::defaults()
{
    fiber_type = "carbon";
    fiber_density = 20;
    fiber_pattern = "grid";
    // ... your defaults
}
```

### 2. **Create Preset Files**
```ini
# resources/profiles/FiberPrinting.ini
[fiber_print:*common*]
fiber_type = carbon
fiber_density = 20
fiber_pattern = grid
fiber_angle = 0
```

### 3. **Support CLI Arguments**
```cpp
// src/CLI/Setup.cpp
// Add --fiber-type, --fiber-density, etc.
```

### 4. **Load from Presets**
```cpp
// src/libslic3r/PresetBundle.cpp
// Add fiber_print presets loading
```

---

## ✅ Summary

**Parameters come from (in priority order):**

1. ✅ **Hardcoded defaults** - Base values in C++ code
2. ✅ **System presets** - `resources/profiles/*.ini` files
3. ✅ **User presets** - User's custom `.ini` files
4. ✅ **3MF embedded config** - Settings in project file
5. ✅ **CLI arguments** - `--layer-height`, etc. (highest priority)
6. ✅ **Config files** - `--load my_config.ini`

**Yes, there are default files!** They're in `resources/profiles/` directory. But parameters can also come from user presets, CLI arguments, and embedded configs.

The system is **hierarchical** - later sources override earlier ones! 🎯

