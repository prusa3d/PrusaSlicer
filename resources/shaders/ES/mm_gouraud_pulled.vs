#version 300 es

precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArray;
precision highp usampler2DArray;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat4 model_matrix;
uniform mat3 view_normal_matrix;

// Clipping plane, x = min z, y = max z. Used to clip the part below the bed.
uniform vec2 z_range;
// Clipping plane - general orientation.
uniform vec4 clipping_plane;

// One texel per triangle slot with the indices of its vertices and state | seed fill << 8 | visible << 9.
uniform usampler2DArray triangle_records_tex;
// One texel per vertex slot, the position in xyz.
uniform sampler2DArray vertex_positions_tex;
// Color of every state, followed by the colors of the triangles selected by seed fill.
uniform sampler2D mm_palette_tex;

const uint TRIANGLE_RECORD_STATE_MASK    = 0xFFu;
const uint TRIANGLE_RECORD_SEED_FILL_BIT = 0x100u;
const uint TRIANGLE_RECORD_VISIBLE_BIT   = 0x200u;

out vec3 clipping_planes_dots;
out vec3 eye_position;
out vec3 triangle_eye_normal;
out vec4 triangle_color;

// Triangle record written by pack_triangle_record() in PaintOnGizmoHelper.cpp.
struct TriangleRecord
{
    uvec3 vertex_indices;
    uint  state;
    bool  selected_by_seed_fill;
    bool  visible;
};

// Texel coordinates in a linear data texture, see LinearDataTexture.
ivec3 texel_coordinates(int texel_idx, ivec3 texture_size)
{
    int row = texel_idx / texture_size.x;
    return ivec3(texel_idx % texture_size.x, row % texture_size.y, row / texture_size.y);
}

vec3 fetch_vertex_position(uint vertex_idx)
{
    ivec3 texture_size = textureSize(vertex_positions_tex, 0);
    return texelFetch(vertex_positions_tex, texel_coordinates(int(vertex_idx), texture_size), 0).xyz;
}

TriangleRecord fetch_triangle_record(int triangle_idx)
{
    ivec3 texture_size = textureSize(triangle_records_tex, 0);
    uvec4 texel        = texelFetch(triangle_records_tex, texel_coordinates(triangle_idx, texture_size), 0);
    return TriangleRecord(
        texel.xyz,
        texel.w & TRIANGLE_RECORD_STATE_MASK,
        (texel.w & TRIANGLE_RECORD_SEED_FILL_BIT) != 0u,
        (texel.w & TRIANGLE_RECORD_VISIBLE_BIT) != 0u
    );
}

// The second half of the palette holds the colors of the triangles selected by seed fill.
vec4 fetch_triangle_color(TriangleRecord record)
{
    int palette_idx = int(record.state) + (record.selected_by_seed_fill ? MM_PALETTE_SIZE : 0);
    return texelFetch(mm_palette_tex, ivec2(palette_idx, 0), 0);
}

void main()
{
    int triangle_idx = gl_VertexID / 3;
    int corner       = gl_VertexID - 3 * triangle_idx;

    TriangleRecord record = fetch_triangle_record(triangle_idx);
    if (!record.visible) {
        // A freed or split triangle, its vertices may be freed too. Placed outside of the view volume.
        gl_Position          = vec4(0.0, 0.0, 2.0, 1.0);
        clipping_planes_dots = vec3(-1.0);
        eye_position         = vec3(0.0);
        triangle_eye_normal  = vec3(0.0, 0.0, 1.0);
        triangle_color       = vec4(0.0);
        return;
    }

    vec3 model_position_0 = fetch_vertex_position(record.vertex_indices.x);
    vec3 model_position_1 = fetch_vertex_position(record.vertex_indices.y);
    vec3 model_position_2 = fetch_vertex_position(record.vertex_indices.z);

    // Computed in every corner the same, so the triangle is flat shaded.
    triangle_eye_normal = view_normal_matrix * cross(model_position_1 - model_position_0, model_position_2 - model_position_0);

    vec3 model_position = (corner == 0) ? model_position_0 : ((corner == 1) ? model_position_1 : model_position_2);
    eye_position        = (view_model_matrix * vec4(model_position, 1.0)).xyz;

    // Fill in the scalars for fragment shader clipping. Fragments with any of these components lower than zero are discarded.
    vec4 world_position  = model_matrix * vec4(model_position, 1.0);
    clipping_planes_dots = vec3(dot(world_position, clipping_plane), world_position.z - z_range.x, z_range.y - world_position.z);

    triangle_color = fetch_triangle_color(record);

    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
