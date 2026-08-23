# Virtual Filaments Tutorial

Virtual filaments create mixed colors by alternating physical filaments layer-by-layer. A region assigned "Virtual T1+T2" prints with filament 1 on even layers and filament 2 on odd layers, producing a blended color appearance. 3+ filament mixes are supported for custom colors.

## Setup

### 1. Printer with multiple extruders

Select a printer profile with 2 or more extruders (e.g., Prusa CORE One with MMU3, or any multi-extruder printer).

### 2. Load filaments

Assign filament presets to each extruder slot in the sidebar. Choose colors with good contrast for visible blending:

| Slot | Example     | Color   |
|------|-------------|---------|
| T1   | PLA Cyan    | #21FFFF |
| T2   | PLA Magenta | #FB02FF |
| T3   | PLA Yellow  | #FFFF0A |
| T4   | PLA Black   | #000000 |

### 3. Enable virtual filaments

1. Go to **Print Settings** tab
2. Select **Multiple Extruders** in the left tree
3. Find the **Virtual Filaments** section
4. Check **Enable virtual filaments**

The sidebar will show a **Virtual Filaments** panel listing all auto-generated pairs with blended color previews:

```
T1 + T2 (50%)   [teal/purple blend]
T1 + T3 (50%)   [green blend]
T1 + T4 (50%)   [dark cyan blend]
T2 + T3 (50%)   [red/orange blend]
T2 + T4 (50%)   [dark magenta blend]
T3 + T4 (50%)   [olive/dark yellow blend]
```

Use the checkboxes to enable or disable individual virtual filaments. The panel refreshes automatically when you change filament presets, toggle the enable flag on the Print tab, or edit virtual filament definitions.

## Creating a custom virtual filament from a target color

You're not limited to the auto-generated 50/50 pairs — you can ask the slicer to approximate any color using the physical filaments you have loaded.

### 1. Open the Create dialog

In the sidebar **Virtual Filaments** panel, click **+ Add custom…** in the header.

### 2. Enter a target color

Type a color in any of these forms, or click the color-picker button:

- Hex: `#CC7733`, `#ffb`, `#4b0082`
- CSS named: `tomato`, `indigo`, `seagreen`, `gold`

The dialog shows:

- **Target** swatch — the color you asked for
- **Achieved** swatch — the closest blend the solver found
- **Ratio bars** — proportional contribution of each physical filament (e.g. `T1 × 3, T2 × 5, T3 × 4` in a 12-slot pattern)
- **ΔRGB** status — how far the achieved color is from the target

### 3. Name it (optional)

At the top of the dialog there's a **Name** field. Give the virtual filament a friendly label (e.g. `Teal`, `Brand Orange`, `Skin Tone #2`) and it will appear in the sidebar and gizmo ahead of the ratio text. Leave it blank to fall back to the ratio-only label.

### 4. Accept

Click **Create**. The dialog adds a new custom virtual filament to the sidebar panel using the solved pattern. It behaves identically to auto-generated ones — you can enable/disable it, paint with it, or assign objects to it. 3+ component mixes are displayed in the panel as e.g. `T2 + T3 + T4 (33/58/8%)` with one mini swatch per component; named entries appear as `Teal — T1 + T2 (50%)`.

## Editing a virtual filament

Every row in the sidebar has a small **✏ edit** button next to its enable checkbox. Clicking it opens the same dialog in **Edit** mode, pre-populated with the row's current color and name. Change the name, change the target color, or both, then click **Save**.

Editing an auto-generated row converts it to a custom row — the 50/50 auto pair is replaced with the newly-solved definition. Auto pairs for component combinations you haven't edited are still auto-generated around it.

The solver works over **enabled, loaded physical filaments only** and picks the integer ratio (summing to 12 by default) whose perceptual blend is closest to the target.

## Assignment methods

Virtual filaments can be applied three ways:

### A. Whole object / whole volume

In the objects list (left panel), right-click an object or one of its modifier volumes and pick a virtual filament from the **Change extruder** / filament submenu. Every face inherits that filament — no painting needed. This is the fastest way to give a whole part a blended color.

### B. Painting faces (MMU gizmo)

1. Click the paint brush icon in the left toolbar, or press **N**.
2. In the **First color** dropdown, scroll past the physical extruders to find virtual filament entries:
   - Extruder 1 (physical)
   - Extruder 2 (physical)
   - ...
   - Virtual T1+T2
   - Virtual T1+T3
   - Virtual T2+T3
   - *(your custom entries appear here too)*
3. Paint:
   - **Left mouse button** — paint with first color
   - **Right mouse button** — paint with second color
   - **Shift + Left mouse button** — remove painted color
   - **Alt + Mouse wheel** — change brush size
4. Use **Smart fill** or **Bucket fill** for faster coverage.

### C. Slice and preview

Click **Slice now**. In the G-code preview:

- Each layer shows the physical extruder color that the virtual filament resolves to on that layer
- Scrub through layers with the right-side slider to see the alternation
- The legend shows physical extruder colors (the actual filaments being used)

## Settings

Found in **Print Settings > Multiple Extruders > Virtual Filaments**:

| Setting | Description |
|---------|-------------|
| **Enable virtual filaments** | Master toggle. When off, virtual filaments are hidden and ignored during slicing. |
| **Advanced dithering** | Uses ordered dithering instead of contiguous layer runs for more even color distribution. Only applies to custom virtual filaments. |
| **Collapse same-color regions** | Merges adjacent regions that resolve to the same physical filament on a given layer, reducing unnecessary tool changes. |

## How it works

### Layer alternation

A virtual filament with a 1:1 ratio (50% blend) alternates every layer:

```
Layer 0: ████ Filament A (cyan)
Layer 1: ████ Filament B (magenta)
Layer 2: ████ Filament A (cyan)
Layer 3: ████ Filament B (magenta)
```

From a distance, the eye blends these into a mixed color (purple).

Multi-component patterns cycle through their tokens. For example a pattern `112223333` on a 9-layer window prints 2 layers of T1, 3 of T2, 4 of T3, then repeats.

### Color blending preview

The sidebar, Create dialog, and painting gizmo all show perceptual pigment-style blended colors. This model produces realistic mixing predictions:

- Blue + Yellow = **Green** (not gray like RGB averaging)
- Red + Yellow = **Orange**
- Red + Blue = **Purple**
- Cyan + Magenta = **Blue**

### Target-color solver

When you enter a target hex, the solver enumerates integer ratios over the loaded physical filaments whose components sum to a fixed denominator (default 12), blends each candidate with the perceptual model, and picks the one minimizing RGB distance to the target. This is why the "achieved" color can differ slightly from the "target" — the solver is constrained to ratios your physical filaments can actually produce.

### Wipe tower

Virtual filament regions create additional tool changes compared to single-material printing. The wipe tower accommodates these automatically. Each layer-to-layer alternation is a physical tool change that requires purging.

## Tips

- **Start with two filaments** to understand the behavior before using more complex combinations.
- **Use high-contrast component colors** (e.g., cyan + magenta) for clearly visible blending results.
- **Whole-object assignment is fastest** if you want an entire part to be a mixed color — no painting required.
- **Check the G-code preview layer-by-layer** using the slider on the right side. You should see the painted or assigned regions alternate between component colors.
- **Thin walls and small features** may not show the blending effect well since they have fewer layers.
- **Print speed**: Virtual filaments don't change print speed, but the additional tool changes add time. Check the estimated print time before committing to a long print.

## Limitations

- Advanced features from OrcaSlicer-FullSpectrum that are not yet implemented: height-weighted cadence, per-perimeter patterns, same-layer pointillisme, surface-bias offsets.
- Maximum of 15 total colors (physical + virtual) due to the triangle selector's state encoding limit.
- The target-color solver uses a fixed denominator (12) — very small fractions that would need a larger pattern window are rounded to the nearest representable ratio.
