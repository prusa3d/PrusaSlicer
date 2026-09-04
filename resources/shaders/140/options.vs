#version 150

#define MAX_LIGHTS 4

const float scaling_factor = 1.5;

struct Light
{
    int system;
    vec3 direction;
    float ambient;
    float diffuse;
    float specular;
    float shininess;
};

uniform mat4 view_matrix;
uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform int num_lights;
uniform Light lights[MAX_LIGHTS];
uniform samplerBuffer position_tex;
uniform samplerBuffer height_width_angle_tex;
uniform samplerBuffer color_tex;
uniform usamplerBuffer segment_index_tex;

in vec3 v_position;
in vec3 v_normal;

out vec3 color;

vec3 decode_color(float color)
{
    int c = int(round(color));
    int r = (c >> 16) & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = (c >> 0) & 0xFF;
    return vec3(r, g, b) / 255.0;
}

vec3 light_direction(Light light)
{
    // return light direction in eye coordinates
    return (light.system == 0) ? (view_matrix * vec4(-light.direction, 0.0)).xyz : -light.direction;
}

float lighting(vec3 eye_position, vec3 eye_normal)
{
    float ambient = 0.0;
    float diffuse = 0.0;
    float specular = 0.0;
    vec3 v = -normalize(eye_position);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= num_lights)
            break;
        vec3 dir = light_direction(lights[i]);
        ambient += lights[i].ambient;
        diffuse += lights[i].diffuse * max(dot(eye_normal, dir), 0.0);
        specular += lights[i].specular * pow(max(dot(v, reflect(-dir, eye_normal)), 0.0), lights[i].shininess);
    }
    return ambient + diffuse + specular;
}

void main()
{
    int id = int(texelFetch(segment_index_tex, gl_InstanceID).r);
    vec2 height_width = texelFetch(height_width_angle_tex, id).xy;
    vec3 offset = texelFetch(position_tex, id).xyz;
    offset.z -= 0.5 * height_width.x;
    height_width *= scaling_factor;
    vec3 eye_position = (view_model_matrix * vec4(v_position * vec3(height_width.y, height_width.y, height_width.x) + offset, 1.0)).xyz;
    vec3 eye_normal = normalize(view_normal_matrix * v_normal);
    color = decode_color(texelFetch(color_tex, id).r) * lighting(eye_position, eye_normal);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
