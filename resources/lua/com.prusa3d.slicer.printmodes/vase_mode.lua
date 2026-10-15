info = {
    id = "vase_mode",
    type = "print_mode",
    title = "Vase mode"
}

function execute(opts)
    return {
        spiral_vase = true,
        perimeters = 1,
        top_solid_layers = 0,
        fill_density = 0,
        support_material = "none",
        support_material_enforce_layers = 0,
        thin_walls = false,
        retract_layer_change = false,
        avoid_crossing_perimeters = false
    }
end
