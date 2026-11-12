#version 300 es

precision highp float;

const vec3 BLACK = vec3(0.05);
const vec3 WHITE = vec3(0.95);

uniform vec3 world_origin;

in float intensity;
in vec3 world_position;

out vec4 out_color;

void main()
{
    vec3 delta = world_position - world_origin;
    vec3 color = delta.x * delta.y * delta.z > 0.0 ? BLACK : WHITE;
    out_color = intensity * vec4(color, 1.0);
}
