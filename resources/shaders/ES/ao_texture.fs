#version 100

uniform float intensity;
uniform float radius;
uniform float bias;
uniform float z_threshold;
uniform int kernel_size;
uniform vec2 viewport_size;
uniform mat4 projection_matrix;
uniform mat4 inverse_projection_matrix;
uniform vec3 kernel[256];

uniform sampler2D g_depth;
uniform sampler2D g_eye_normal;
uniform sampler2D tex_noise;

varying vec2 tex_coord;

vec3 eye_position_from_depth(vec2 uv, float depth)
{
    vec4 eye = inverse_projection_matrix * vec4(vec3(uv, depth) * 2.0 - vec3(1.0), 1.0);
    return eye.xyz / eye.w;
}

void main()
{
    // get input for SSAO algorithm
    vec3 eye_position = eye_position_from_depth(tex_coord, texture(g_depth, tex_coord).r);
    vec3 eye_normal = normalize(texture(g_eye_normal, tex_coord).xyz);
    vec2 tex_noise_size = textureSize(tex_noise, 0);
    vec2 noise_scale = viewport_size / tex_noise_size; 
    vec3 random = normalize(texture(tex_noise, tex_coord * noise_scale).xyz);

    // create TBN change-of-basis matrix: from tangent-space to view-space
    vec3 tangent = normalize(random - eye_normal * dot(random, eye_normal));
    vec3 bitangent = cross(eye_normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, eye_normal);

    // iterate over the sample kernel and calculate occlusion factor
    float occlusion = 0.0;
    for (int i = 0; i < kernel_size; ++i) {
        // get sample position
        vec3 sample_ref_pos = eye_position + TBN * kernel[i] * radius; // from tangent to view-space

        // project sample position (to sample texture) (to get position on screen/texture)
        vec4 offset = projection_matrix * vec4(sample_ref_pos, 1.0); // from view to clip-space
        offset.xyz /= offset.w; // perspective divide
        offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0

        // get sample depth
        float sample_depth = texture(g_depth, offset.xy).r; // get depth value of kernel sample
        vec3 sample_eye_position = eye_position_from_depth(offset.xy, sample_depth);        

        // range check & accumulate
        float range_check = smoothstep(0.0, 1.0, z_threshold / abs(eye_position.z - sample_eye_position.z));
        occlusion += (sample_eye_position.z >= sample_ref_pos.z + bias ? 1.0 : 0.0) * range_check;           
    }
    gl_FragColor = pow(1.0 - (occlusion / kernel_size), intensity);
}