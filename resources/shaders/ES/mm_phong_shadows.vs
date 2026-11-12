#version 100

uniform mat4 model_matrix;
uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 light_matrix;

// Color lookup table (MM_PALETTE_SIZE x 1 texels): one RGBA texel per paint state, indexed by v_palette_index.
uniform sampler2D mm_palette_tex;

attribute vec3 v_position;
attribute vec3 v_normal;
attribute float v_palette_index;

varying vec3 eye_normal;
varying vec3 eye_position;
varying vec4 light_position;
varying vec3 world_position;
varying vec4 vertex_color;

void main()
{
    eye_normal = view_normal_matrix * v_normal;
    eye_position = (view_model_matrix * vec4(v_position, 1.0)).xyz;
    light_position = light_matrix * vec4(v_position, 1.0);
    world_position = (model_matrix * vec4(v_position, 1.0)).xyz;
    vertex_color = texture2DLod(mm_palette_tex, vec2((v_palette_index + 0.5) / float(MM_PALETTE_SIZE), 0.5), 0.0);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
