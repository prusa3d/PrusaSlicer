info = {
    id = "soluble_supports",
    type = "print_mode",
    title = "Soluble supports",
    params = {
        {name = "extruder", label = "Support extruder", type = "int", default = 2}
    }
}

function execute(opts)
    local config = api.config
    return {
        solid_infill_acceleration = config:value("solid_infill_acceleration")[opts.extruder] * 0.5,
        solid_infill_speed = config:value("solid_infill_speed")[opts.extruder] * 0.5,
        support_material_interface_speed = config:value("support_material_interface_speed") * 0.8,
        support_material_speed = config:value("support_material_speed") * 0.8,
        support_material = "everywhere",
        support_material_threshold = math.min(config:value("support_material_threshold") + 10, 90),
        support_material_bottom_interface_layers = math.max(config:value("support_material_bottom_interface_layers"), 2),
        support_material_contact_distance = 0,
        support_material_interface_spacing = config:value("support_material_interface_spacing") * 0.5,
        support_material_synchronize_layers = true,
        support_material_with_sheath = true,
        support_material_xy_spacing = config:value("support_material_xy_spacing") * 0.5,
        support_material_extruder = opts.extruder,
        support_material_interface_extruder = opts.extruder,
        wipe_tower_bridging = config:value("wipe_tower_bridging") * 0.75,
        support_material_extrusion_width = config:value("support_material_extrusion_width") * 0.875
    }
end
