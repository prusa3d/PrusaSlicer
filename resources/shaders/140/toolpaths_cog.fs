#version 140

const vec3 BLACK = vec3(0.1);
const vec3 WHITE = vec3(0.9);

const float emission_factor = 0.25;

// x = tainted, y = specular;
in vec2 intensity;
in vec3 position;

out vec4 out_color;

void main()
{
    vec3 color = position.x * position.y * position.z > 0.0 ? BLACK : WHITE;
    out_color = vec4(vec3(intensity.y) + color * (intensity.x + emission_factor), 1.0);
}
