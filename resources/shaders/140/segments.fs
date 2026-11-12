#version 150

in vec3 color;
out vec4 fragment_color;

void main()
{
    fragment_color = vec4(color, 1.0);
}
