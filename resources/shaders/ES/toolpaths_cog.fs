#version 100

precision highp float;

const vec3 BLACK = vec3(0.1);
const vec3 WHITE = vec3(0.9);

const float emission_factor = 0.25;

// x = tainted, y = specular;
varying vec2 intensity;
varying vec3 position;

void main()
{
    vec3 color = position.x * position.y * position.z > 0.0 ? BLACK : WHITE;
    gl_FragColor = vec4(vec3(intensity.y) + color * (intensity.x + emission_factor), 1.0);
}
