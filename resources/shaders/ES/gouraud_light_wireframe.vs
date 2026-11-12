#version 100

#define MAX_LIGHTS 4

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

attribute vec3 v_position;
attribute vec3 v_normal;

// x = tainted, y = specular;
varying vec2 vertex_intensity;

vec3 light_direction(Light light)
{
    // return light direction in eye coordinates
    return (light.system == 0) ? (view_matrix * vec4(-light.direction, 0.0)).xyz : -light.direction;
}

void main()
{
    // First transform the normal into camera space and normalize the result.
    vec3 eye_normal = normalize(view_normal_matrix * v_normal);
    vec4 eye_position = view_model_matrix * vec4(v_position, 1.0);
    vertex_intensity = vec2(0.0);
    vec3 v = -normalize(eye_position.xyz);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= num_lights)
            break;
        vec3 dir = light_direction(lights[i]);
        vertex_intensity.x += lights[i].ambient + max(dot(eye_normal, dir), 0.0) * lights[i].diffuse;
        vertex_intensity.y += lights[i].specular * pow(max(dot(v, reflect(-dir, eye_normal)), 0.0), lights[i].shininess);    
    }

    gl_Position = projection_matrix * eye_position;
}
