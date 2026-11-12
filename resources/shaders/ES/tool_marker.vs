#version 300 es

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
uniform vec4 uniform_color;
uniform int num_lights;
uniform Light lights[MAX_LIGHTS];

in vec3 v_position;
in vec3 v_normal;

out vec4 color;

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

void main() {
    vec3 eye_position = (view_model_matrix * vec4(v_position, 1.0)).xyz;
    vec3 eye_normal = normalize(view_normal_matrix * v_normal);
    color = vec4(uniform_color.rgb * lighting(eye_position, eye_normal), uniform_color.a);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
