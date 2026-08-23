#version 140

uniform float overlay_alpha;
uniform float u_contour_interval; // mm between contour lines; 0 = disabled
uniform float u_contour_darkness; // 0..1 blend factor for line color

// x = tainted, y = specular;
in vec2 intensity;
in vec3 vertex_color;
in float raw_z_mm;

out vec4 out_color;

void main()
{
    vec3 shaded = vec3(intensity.y) + vertex_color * intensity.x;

    // Iso-Z contour lines. The line profile is:
    //   - core fully opaque for `half_core` pixels either side of the line
    //   - then a smoothstep AA ramp out to `half_core + aa_ramp` pixels
    // Widening the AA beyond 1 pixel (vs. the previous hard 1-pixel line)
    // is what makes the edges read as "smooth" rather than "pixelated".
    //
    // A second guard fades contours out on steep slopes, where multiple
    // lines would otherwise land within a single screen pixel and moire.
    if (u_contour_interval > 0.0) {
        float f        = raw_z_mm / u_contour_interval;
        // Signed distance to the nearest integer (contour), in interval units.
        float dist     = abs(fract(f + 0.5) - 0.5);
        // One screen pixel, expressed in interval units.
        float pixel_w  = max(fwidth(f), 1e-6);
        // Convert dist to screen pixels so line thickness is independent
        // of interval, zoom, and Z-exaggeration.
        float px       = dist / pixel_w;

        // Line profile: ~1.2 px opaque core + ~1.2 px AA fade on each side.
        const float half_core = 0.6;
        const float aa_ramp   = 1.2;
        float line_t = 1.0 - smoothstep(half_core, half_core + aa_ramp, px);

        // Aliasing guard: when iso-lines would stack into fewer than ~2
        // screen pixels apart, fade them so they don't moire on steep
        // slopes. Fully visible below 0.25 px/interval; invisible above
        // 0.6 px/interval (where lines are closer than 2 pixels).
        float resolve = 1.0 - smoothstep(0.25, 0.6, pixel_w);
        line_t *= resolve;

        shaded = mix(shaded, shaded * (1.0 - u_contour_darkness), line_t);
    }

    out_color = vec4(shaded, overlay_alpha);
}
