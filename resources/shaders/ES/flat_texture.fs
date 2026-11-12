#version 100

precision highp float;

uniform sampler2D in_texture;

varying vec2 tex_coord;

void main()
{
    gl_FragColor = texture2D(in_texture, tex_coord);
}
