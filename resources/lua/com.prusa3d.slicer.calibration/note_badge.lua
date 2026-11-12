local M = {}

function M.note_badge(opts)
    local note = opts.note or "Hello"
    local font = opts.font or api.get_default_font()
    local depth = opts.depth or 1.0
    local padding_x = opts.padding_x or 1.0
    local padding_y = opts.padding_y or 1.0
    local anchor_x = opts.anchor_x or 0.5
    local anchor_y = opts.anchor_y or 1.0
    local pos_x = opts.pos_x or 0.0
    local pos_y = opts.pos_y or 0.0

    local text_mesh = api.emboss_text {
        font=font,
        text="" .. note,
        depth=depth
    }
    local text_bounds = text_mesh:bounds()
    local width = text_bounds.max_x - text_bounds.min_x + padding_x * 2
    local height = text_bounds.max_y - text_bounds.min_y + padding_y * 2
    local background_mesh = api.make_cube(width, height, depth)

    pos_x = pos_x - anchor_x * width
    pos_y = pos_y - anchor_y * height

    return {
        {mesh=text_mesh, translate={x=-(text_bounds.min_x - padding_x) + pos_x, y=-(text_bounds.min_y - padding_y) + pos_y, z=depth}},
        {mesh=background_mesh, translate={x=pos_x, y=pos_y, z=0}}
    }
end

return M
