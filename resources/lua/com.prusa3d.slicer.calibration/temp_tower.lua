info = {
    id = "temp_tower",
    type = "project.plugin",
    title = "Temperature Tower",
    menu = "Calibration/Temperature Tower",
    params = {
        {name = "min_temp", label = "Min Temperature", type = "int", default = 190},
        {name = "max_temp", label = "Max Temperature", type = "int", default = 230},
        {name = "steps", label = "Num Steps", type = "int", default = 5}
    }
}
function execute(opts)
    local base_mesh = api.load_stl("temp_tower-base.stl")
    local base_aabb = base_mesh:bounds()
    local base_height = base_aabb.max_z - base_aabb.min_z
    local z0 = -base_aabb.min_z
    local step_mesh = api.load_stl("temp_tower-step.stl")
    local step_aabb = step_mesh:bounds()
    local step_height = step_aabb.max_z - step_aabb.min_z
    ---@type VolumeDefinition[]
    local other_volumes = {}
    local min_temp = opts.min_temp
    local step_temp = (opts.max_temp - opts.min_temp) / (opts.steps - 1)
    local bed = api.project:current_bed()
    local layer_height = bed:print_presets():value("layer_height")

    api.project:clear_layer_custom_steps(bed)

    -- set print preset to sane values (?)
    bed:print_presets():set("fill_density", "15%")
    bed:print_presets():set("bottom_solid_layers", 3)
    bed:print_presets():set("top_solid_layers", 5)

    local font = api.get_font("Helvetica")

    for i = 1, opts.steps do
        local temp = opts.max_temp - step_temp * (i - 1)
        local z = base_height + step_height * (i - 1) + step_aabb.min_z
        table.insert(other_volumes, {
            mesh = api.load_stl("temp_tower-step.stl"),
            translate = {
                z = z
            }
        })

        local rounded_temp = math.floor(temp + 0.5)
        local gcode = "M104 S" .. rounded_temp
        api.project:insert_layer_custom_gcode(api.project:current_bed(), z + layer_height, gcode)

        ---@type VolumeDefinition
        local vol = {
            mesh = api.emboss_text{
                font=font,
                text="" .. rounded_temp,
                depth=1
            },
            type = VolumeType.Negative,
            rotate = {
                x = 90
            },
            translate = {
                x = -15,
                y = -4.1,
                z = z + 3.15
            },
        }

        table.insert(other_volumes, vol)


    end

    api.project:add_object{
        mesh=api.load_stl("temp_tower-base.stl"),
        other_volumes=other_volumes,
        translate={z=z0},
        object_params={fill_density="0%"}
    }
end


