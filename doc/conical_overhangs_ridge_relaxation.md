# Conical overhangs — ridge melting

## Problem

"Conical overhangs" bakes a printable support cone under steep overhangs by
walking the sliced layers top-down and, per layer, eroding the outline above by
`layer_height · tan(θ)` and unioning it with the current layer (see
`ConicalOverhangs.cpp`). Its underside is a *straight-skeleton roof*, so wherever
cones from different edges collide it leaves sharp **convex ridges** (knife
edges). Those ridges are the thinnest, least-anchored extrusions in the cone and
tend to **curl up when hot**, degrading the surface and sometimes the print.

## Goal

Melt those cone ridges **away completely** as they descend — a sharp square
cross-section should slowly round into a circle — while:

- never touching the model's own edges (only cone-generated ridges), and
- never letting a melted wall get steeper than a user-bounded extra angle.

## Approach: curve-shortening flow

Melting a closed curve so it rounds and eventually becomes a circle is exactly
**curve-shortening (curvature) flow** (Grayson's theorem: any embedded closed
curve becomes convex, then a round point). Its discrete form is **Laplacian
smoothing**: move each vertex towards the midpoint of its two neighbours.

- High-curvature convex ridges move most → they melt first.
- Straight (collinear) runs are **fixed points** of the flow → the flat cone
  faces are untouched, so the cone keeps tapering (no blob).
- **The layer stack is the flow's time integration.** One smoothing step per
  layer, carried down, melts ridges progressively — no per-layer iteration knob
  is needed.

### Subdivision is required

Curve-shortening can only move the vertices it is given, and it converges an
`N`-vertex polygon to a regular `N`-gon (max turning angle floors at `2π/N`). A
bare 4-vertex square therefore just *shrinks*, staying square; subdivided once it
stalls at an octagon. To actually reach a circle the vertex **density** must be
maintained as the shape rounds and shrinks. So each layer the eroded outline is
`densify`-d to a maximum edge length (`conical_overhangs_subdivision`) before
melting.

### Bounding the extra overhang

Curve-shortening is **subtractive** at convex corners (it cuts inward), which
locally steepens the overhang. To keep that in check, each vertex's per-layer
move is clamped to

    max_travel = layer_height · ( tan(θ + extra) − tan(θ) )

where `θ` is the overhang angle and `extra` is `conical_overhangs_melt_angle`.
Because the added horizontal recession per layer is then ≤ `max_travel`, the
local wall angle satisfies `tan(φ) ≤ tan(θ + extra)`, i.e. **no melted wall is
ever steeper than `θ + extra`** (`θ + extra` is clamped below 90°). This is a
per-layer bound, so it holds cumulatively down the stack. It is conservative
(clamping the displacement magnitude may melt slightly slower than the full
budget, never faster) and produces a smooth rounded bevel, not a flat `θ+extra`
facet.

## Algorithm

Per layer, top-down (melting is applied to the eroded cone **before** the model
is unioned in, so the model's edges are never touched):

```
grown_above = top layer outline
for each layer i, top-1 → 0:
    step    = layer_height · tan(θ)
    eroded  = offset(grown_above, -step)          // shrink cone
    if extra > 0:
        densify(eroded, subdivision_length)        // supply points to round onto
        max_travel = step_dz · (tan(θ+extra) − tan(θ))
        eroded  = curve_shorten(eroded, max_travel) // one Laplacian step, clamped
    grown   = union(eroded, model[i])              // model added AFTER, never melted
    cleanup / keep-holes-open / merge into layer
    grown_above = grown                             // carry melted outline (compounds)
```

`curve_shorten` moves each vertex `0.5 · (midpoint_of_neighbours − vertex)`,
clamped to `max_travel`, then re-unions to heal any self-intersection. When
melting is on, the carry-down simplify uses a much finer tolerance so the melted
arcs and subdivision points are not stripped before they compound.

## Parameters

- `conical_overhangs_melt_angle` (float, °, default 0): extra overhang angle
  allowed at ridges in exchange for melting them. Melted walls never exceed
  `overhang angle + this`. Larger = faster, more complete melting. 0 disables
  (exact original sharp behaviour).
- `conical_overhangs_subdivision` (float, mm, default 0.5): maximum edge length
  the cone outline is subdivided to before melting. Smaller = finer, rounder
  ridges at some slicing cost.

## Properties

- **Melts to a circle.** Repeated curve-shortening rounds a subdivided square
  monotonically towards a circle, then a point — sharp ridges vanish entirely.
- **Bounded steepness.** No melted wall exceeds `θ + extra` (per-layer clamp,
  holds cumulatively).
- **Taper preserved.** Straight faces are fixed points of the flow and melting is
  subtractive, so the cone can only keep or tighten its taper — never balloon.
- **Provenance-correct.** The model is unioned in after melting, so its own edges
  are never touched.

## Known limitations

- The bound is on wall *steepness*, giving a smooth rounded bevel, not a planar
  `θ+extra` facet.
- Subdivision length trades roundness against slicing cost/vertex count; it is a
  density control, not a "number of splits".
- Melting is subtractive — it removes a little support material at ridge tips
  (which supported nothing below them) in exchange for smooth, non-curling
  ridges.

## Testing

`tests/libslic3r/test_conical_overhangs.cpp` covers: turning-angle sign, zero =
identity, one-step rounding, square→circle convergence, subtractive-at-convex,
the displacement clamp (nothing moves past `max_travel`), taper preservation, and
acute-tip boundedness.
