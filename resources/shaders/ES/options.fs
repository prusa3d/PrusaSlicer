#version 300 es

precision highp float;

in vec3 color;

out vec4 out_color;

void main()
{
    out_color = vec4(color, 1.0);
}
