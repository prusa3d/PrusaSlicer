#version 300 es

precision lowp usampler2D;

const vec3 UP = vec3(0.0, 0.0, 1.0);

//|     /1-------6\     |
//|    / |       | \    |
//|   2--0-------5--7   |
//|    \ |       | /    |
//|      3-------4      |
const int VERTICES_PER_SEGMENT = 24;
const int INDEX_MAP[VERTICES_PER_SEGMENT] = int[](
    0, 1, 2, // front spike
    0, 2, 3, // front spike
    0, 3, 4, // right/bottom body
    0, 4, 5, // right/bottom body
    0, 5, 6, // left/top body
    0, 6, 1, // left/top body
    5, 4, 7, // back spike
    5, 7, 6  // back spike
);

const vec2 HORIZONTAL_VERTICAL_VIEW_SIGNS_ARRAY[16] = vec2[](
    //horizontal view (from right)
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 0.0),
    vec2(0.0, -1.0),
    vec2(0.0, -1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 0.0),
    // vertical view (from top)
    vec2(0.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(0.0, 0.0)
);

uniform mat4 projection_view_model_matrix;
uniform mat3 view_normal_matrix;
uniform vec3 camera_position;
uniform int total_vertex_count;
uniform bool camera_looking_down;

uniform sampler2D position_tex;
uniform sampler2D height_width_angle_tex;
uniform sampler2D color_tex;
uniform usampler2D segment_index_tex;

out vec3 eye_normal;
out vec4 color;

vec3 decode_color(float color)
{
    int c = int(round(color));
    int r = (c >> 16) & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = (c >> 0) & 0xFF;
    return vec3(r, g, b)/ 255.0;
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
    int vertex_id = camera_looking_down
        ? (total_vertex_count - 1 - gl_VertexID)
        : gl_VertexID;

    int instance_id = vertex_id / VERTICES_PER_SEGMENT;
    int local_vertex_index = INDEX_MAP[vertex_id % VERTICES_PER_SEGMENT];

    int id_a = int(texelFetch(segment_index_tex, tex_coord_u(segment_index_tex, instance_id), 0).r);
    int id_b = id_a + 1;
    vec3 pos_a = texelFetch(position_tex, tex_coord(position_tex, id_a), 0).xyz;
    vec3 pos_b = texelFetch(position_tex, tex_coord(position_tex, id_b), 0).xyz;
    vec3 line = pos_b - pos_a;
    // directions of the line box in world space
    float line_len = length(line);
    vec3 line_dir;
    if (line_len < 1e-4)
        line_dir = vec3(1.0, 0.0, 0.0);
    else
        line_dir = line / line_len;
    vec3 line_right_dir;
    if (abs(dot(line_dir, UP)) > 0.9) {
        // For vertical lines, the width and height should be same, there is no concept of up and down.
        // For simplicity, the code will expand width in the x axis, and height in the y axis
        line_right_dir = normalize(cross(vec3(1, 0, 0), line_dir));
    }
    else
        line_right_dir = normalize(cross(line_dir, UP));
    vec3 line_up_dir = normalize(cross(line_right_dir, line_dir));
    int id = local_vertex_index < 4 ? id_a : id_b;
    vec3 endpoint_pos = local_vertex_index < 4 ? pos_a : pos_b;
    vec3 height_width_angle = texelFetch(height_width_angle_tex, tex_coord(height_width_angle_tex, id), 0).xyz;
    int closer_id = (dot(camera_position - pos_a, camera_position - pos_a) < dot(camera_position - pos_b, camera_position - pos_b)) ? id_a : id_b;
    vec3 closer_pos = (closer_id == id_a) ? pos_a : pos_b;
    vec3 camera_view_dir = normalize(closer_pos - camera_position);
    vec3 closer_height_width_angle = texelFetch(height_width_angle_tex, tex_coord(height_width_angle_tex, closer_id), 0).xyz;
    vec3 diagonal_dir_border = normalize(closer_height_width_angle.x * line_up_dir + closer_height_width_angle.y * line_right_dir);
    bool is_vertical_view = abs(dot(camera_view_dir, line_up_dir)) / abs(dot(diagonal_dir_border, line_up_dir)) >
        abs(dot(camera_view_dir, line_right_dir)) / abs(dot(diagonal_dir_border, line_right_dir));
    vec2 signs = HORIZONTAL_VERTICAL_VIEW_SIGNS_ARRAY[local_vertex_index + 8 * int(is_vertical_view)];
    float view_right_sign = sign(dot(-camera_view_dir, line_right_dir));
    float view_top_sign = sign(dot(-camera_view_dir, line_up_dir));
    float half_height = 0.5 * height_width_angle.x;
    float half_width = 0.5 * height_width_angle.y;
    vec3 horizontal_dir = half_width * line_right_dir;
    vec3 vertical_dir = half_height * line_up_dir;
    float horizontal_sign = signs.x * view_right_sign;
    float vertical_sign = signs.y * view_top_sign;
    vec3 pos = endpoint_pos + horizontal_sign * horizontal_dir + vertical_sign * vertical_dir;
    if (local_vertex_index == 2 || local_vertex_index == 7) {
        float line_dir_sign = (local_vertex_index == 2) ? -1.0 : 1.0;
        if (height_width_angle.z == 0) {
            // There I add a cap to lines that do not have a following line
            // (or they have one, but perfectly aligned, so the cap is hidden inside the next line).
            pos += line_dir_sign * line_dir * half_width;
        }
        else {
            pos += line_dir_sign * line_dir * half_width * sin(abs(height_width_angle.z) * 0.5);
            pos += sign(height_width_angle.z) * horizontal_dir * cos(abs(height_width_angle.z) * 0.5);
        }
    }
    eye_normal = view_normal_matrix * (pos - endpoint_pos);
    color = vec4(decode_color(texelFetch(color_tex, tex_coord(color_tex, id), 0).r), 1.0);
    gl_Position = projection_view_model_matrix * vec4(pos, 1.0);
}
