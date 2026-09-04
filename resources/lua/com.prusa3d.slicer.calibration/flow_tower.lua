info = {
    id = "flow_tower",
    type = "project.plugin",
    title = "Flow Tower",
    menu = "Calibration/Flow Tower",
    params = {
        {name = "min_flow_rate", label = "Min Vol. Flow Rate  [mm^3/s]", type = "float", default = 1},
        {name = "max_flow_rate", label = "Max Vol. Flow Rate  [mm^3/s]", type = "float", default = 25},
        {name = "steps", label = "Num Steps", type = "int", default = 15},
        {name = "step_depth", label = "Step Depth  [mm]", type = "float", default = 3},
        {name = "enable_ticks", label = "Enable Ticks", type = "bool", default = true},
        {name = "enable_brim", label = "Enable Brim", type = "bool", default = true},
        {name = "note", label = "Note", type = "string", default = ""}
    }
}

function flow_rate_to_speed(flow_rate, extrusion_width, layer_height)
    local w = extrusion_width
    local h = layer_height
    local a = (w - h) * h + math.pi * (h/2)^2
    return flow_rate / a
end

function execute(opts)
    local note_badge = require('note_badge').note_badge

    local bed = api.project:current_bed()

    local nozzle_dia = bed:printer_config().tools[1]:nozzle_diameter() or 0.4
    local layer_height = nozzle_dia * 0.75
    local extrusion_width = nozzle_dia * 1.75

    bed:print_presets():set("first_layer_height", layer_height)
    bed:print_presets():set("layer_height", layer_height)
    bed:material_presets(0):set("filament_max_volumetric_speed", 0)

    local rounded_step_depth = math.floor(opts.step_depth / layer_height) * layer_height
    local depth = rounded_step_depth * opts.steps
    local mod_depth = rounded_step_depth
    local other_volumes = {}

    local base_mesh = api.emboss_svg("hreben.svg", depth)
    local bb = base_mesh:bounds()

    print("base mesh z min: " .. bb.min_z .. " / z max: " .. bb.max_z)

    bed:print_presets():set("brim_type", opts.enable_brim and "outer_only" or "none")
    bed:print_presets():set("brim_width", opts.enable_brim and 5 or 0)

    local min_speed = flow_rate_to_speed(opts.min_flow_rate, extrusion_width, layer_height)
    local max_speed = flow_rate_to_speed(opts.max_flow_rate, extrusion_width, layer_height)

    for i = 1, opts.steps do
        local f = (i - 1) / (opts.steps - 1)
        local speed = min_speed * (1 - f) + max_speed * f
        table.insert(other_volumes, {
            mesh = api.make_cube(bb.max_x - bb.min_x, bb.max_y - bb.min_y,  mod_depth),
            translate = {
                x = bb.min_x,
                y = bb.min_y,
                z = mod_depth * (i - 1)
            },
            params = {
                perimeter_speed = speed,
                external_perimeter_speed = speed
            }
        })
        if opts.enable_ticks and i > 1 then
            table.insert(other_volumes, {
                mesh = api.make_cube(
                    bb.max_x - bb.min_x,
                    bb.max_y - bb.min_y,
                    layer_height
                ),
                translate = {
                    x = bb.min_x,
                    y = bb.min_y,
                    z = mod_depth * (i - 1)
                },
                type = VolumeType.Modifier,
                params = {
                    external_perimeter_extrusion_width = extrusion_width *  1.5
                }
            })
--             for k, v in pairs(other_volumes[#other_volumes].translate) do
--                 print(k,v)
--             end
--             print(i, tx, other_volumes[#other_volumes].translate.x)
        end
    end

    if #opts.note > 0 then
        local pos_x = (bb.max_x - bb.min_x) * 0.5 + bb.min_x
        local pos_y = bb.min_y
        local badge_vols = note_badge {
            note = opts.note,
            pos_x = pos_x,
            pos_y = pos_y,
            depth = layer_height * 2
        }
        for _, v in ipairs(badge_vols) do
            table.insert(other_volumes, v)
        end
    end

    api.project:add_object{
        mesh=base_mesh,
        other_volumes=other_volumes,
        translate={
            z = -bb.min_z
        },
        params={
            fill_density="0%",
            top_solid_layers=0,
            bottom_solid_layers=0,
            perimeters=1,
            external_perimeter_extrusion_width=extrusion_width
        }
    }
end

