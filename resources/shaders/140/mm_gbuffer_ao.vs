#version 330

uniform mat4 model_matrix;
uniform mat4 projection_view_model_matrix;
uniform mat3 view_normal_matrix;

// Color lookup table (paint-state count x 1 texels): one RGBA texel per paint state, indexed by v_palette_index.
uniform sampler2D mm_palette_tex;

in vec3 v_position;
in vec3 v_normal;
in float v_palette_index;

out vec3 eye_normal;
out vec3 world_position;
out vec4 vertex_color;

void main()
{
    eye_normal = view_normal_matrix * v_normal;
    world_position = (model_matrix * vec4(v_position, 1.0)).xyz;
    vertex_color = texelFetch(mm_palette_tex, ivec2(int(v_palette_index + 0.5), 0), 0);
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
}
