#version 300 es

precision lowp usampler2D;

const float scaling_factor = 1.5;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 light_matrix;

uniform sampler2D position_tex;
uniform sampler2D height_width_angle_tex;
uniform sampler2D color_tex;
uniform usampler2D segment_index_tex;

in vec3 v_position;
in vec3 v_normal;

out vec3 eye_position;
out vec3 eye_normal;
out vec4 light_position;
out vec4 var_color;

vec3 decode_color(float color)
{
    int c = int(round(color));
    int r = (c >> 16) & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = (c >> 0) & 0xFF;
    return vec3(r, g, b) / 255.0;
}

ivec2 tex_coord(sampler2D sampler, int id)
{
    ivec2 tex_size = textureSize(sampler, 0);
    return (tex_size.y == 1) ? ivec2(id, 0) : ivec2(id % tex_size.x, id / tex_size.x);
}

ivec2 tex_coord_u(usampler2D sampler, int id)
{
    ivec2 tex_size = textureSize(sampler, 0);
    return (tex_size.y == 1) ? ivec2(id, 0) : ivec2(id % tex_size.x, id / tex_size.x);
}

void main()
{
    int id = int(texelFetch(segment_index_tex, tex_coord_u(segment_index_tex, gl_InstanceID), 0).r);
    vec2 height_width = texelFetch(height_width_angle_tex, tex_coord(height_width_angle_tex, id), 0).xy;
    vec3 offset = texelFetch(position_tex, tex_coord(position_tex, id), 0).xyz;
    offset.z -= 0.5 * height_width.x;
    height_width *= scaling_factor;
    eye_normal = view_normal_matrix * v_normal;
    vec3 final_pos = v_position * vec3(height_width.y, height_width.y, height_width.x) + offset;
    eye_position = (view_model_matrix * vec4(final_pos, 1.0)).xyz;
    light_position = light_matrix * vec4(final_pos, 1.0);
    var_color = vec4(decode_color(texelFetch(color_tex, tex_coord(color_tex, id), 0).r), 1.0);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
