#version 100

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

uniform mat4 viewport_matrix;

// x = tainted, y = specular;
varying vec2 vertex_intensity[];

varying vec2 intensity;
noperspective varying vec3 edge_distance;

void main()
{
    vec4 p;

    p = gl_in[0].gl_Position;
    vec2 p0 = vec2(viewport_matrix * (p / p.w));

    p = gl_in[1].gl_Position;
    vec2 p1 = vec2(viewport_matrix * (p / p.w));

    p = gl_in[2].gl_Position;
    vec2 p2 = vec2(viewport_matrix * (p / p.w));

    float a = length(p1 - p2);
    float b = length(p2 - p0);
    float c = length(p1 - p0);

    float alpha = acos((b * b + c * c - a * a) / (2.0 * b * c));
    float beta  = acos((a * a + c * c - b * b) / (2.0 * a * c));

    float ha = abs(c * sin(beta));
    float hb = abs(c * sin(alpha));
    float hc = abs(b * sin(alpha));

    gl_Position = gl_in[0].gl_Position;
    edge_distance = vec3(ha, 0.0, 0.0);
    intensity = vertex_intensity[0];
    EmitVertex();

    gl_Position = gl_in[1].gl_Position;
    edge_distance = vec3(0.0, hb, 0.0);
    intensity = vertex_intensity[1];
    EmitVertex();

    gl_Position = gl_in[2].gl_Position;
    edge_distance = vec3(0.0, 0.0, hc);
    intensity = vertex_intensity[2];
    EmitVertex();

    EndPrimitive();
}