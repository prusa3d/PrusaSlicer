info = {
    id = "breakaway_interface_supports",
    type = "print.print_mode",
    title = "Breakaway interface supports",
    condition = "printer.tool_count > 1 or feeder.base_model =~ /.*MMU.*/",
    params = {
        {name = "support_extruder", label = "Support interface extruder", type = "int", default = 2},
        {name = "model_extruder", label = "Model extruder", type = "int", default = 1}
    }
}

function execute(opts)
    local config = api.config
    local interface = opts.support_extruder
    local model = opts.model_extruder

    return {
        -- Everything that is not support follows the model extruder: the model itself,
        -- the bed temperature and the wipe tower perimeters (one material, present on every layer).
        perimeter_extruder = model,
        infill_extruder = model,
        solid_infill_extruder = model,
        bed_temperature_extruder = model,
        wipe_tower_extruder = config:value("wipe_tower") and model or nil,
        support_material_extruder = model,
        support_material_interface_extruder = interface,

        -- The support body is the model material with zero gap: without interface layers it fuses to the model
        support_material_interface_layers = math.max(config:value("support_material_interface_layers"), 2),

        -- Per slot keys: a table changes only the interface extruder, the model keeps its speed
        solid_infill_acceleration = {[interface] = config:value("solid_infill_acceleration")[interface] * 0.5},
        solid_infill_speed = {[interface] = config:value("solid_infill_speed")[interface] * 0.5},
        support_material_interface_speed = config:value("support_material_interface_speed") * 0.8,
        support_material_speed = config:value("support_material_speed") * 0.8,
        support_material = "everywhere",
        support_material_threshold = math.max(config:value("support_material_threshold"), 50),
        support_material_bottom_interface_layers = math.max(config:value("support_material_bottom_interface_layers"), 2),
        support_material_contact_distance = 0,
        support_material_bottom_contact_distance = 0,
        support_material_interface_spacing = 0,
        support_material_synchronize_layers = true,
        support_material_with_sheath = false,
        support_material_xy_spacing = config:value("support_material_xy_spacing") * 0.5,
        wipe_tower_bridging = config:value("wipe_tower_bridging") * 0.75,
        support_material_extrusion_width = config:value("support_material_extrusion_width") * 0.875,
        support_material_interface_pattern = "rectilinear",
        filament_soluble = {[interface] = true}
    }
end
