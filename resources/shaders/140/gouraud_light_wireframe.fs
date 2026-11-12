#version 150

uniform vec4 uniform_color;
uniform vec4 wireframe_color;
uniform float wireframe_width;

// x = tainted, y = specular;
in vec2 intensity;

noperspective in vec3 edge_distance;

out vec4 out_color;

void main()
{
    float d = min(edge_distance.x, min(edge_distance.y, edge_distance.z));
    float mix_val = 0.0;
    if (d < wireframe_width - 1.0)
        mix_val = 1.0;
    else if (d > wireframe_width + 1.0)
        mix_val = 0.0;
    else {
        float x = d - (wireframe_width - 1.0);
        mix_val = exp2(-2.0 * x * x);
    }
    vec4 color = mix(uniform_color, wireframe_color, mix_val);
    out_color = vec4(vec3(intensity.y) + color.rgb * intensity.x, color.a);
}
