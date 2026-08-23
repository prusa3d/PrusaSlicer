#pragma once

namespace Slic3r {

// Perceptual pigment-style color blending for filament color preview.
// Uses a degree-4 polynomial model trained to approximate Mixbox behavior.
void filament_color_blend(unsigned char r1, unsigned char g1, unsigned char b1,
                          unsigned char r2, unsigned char g2, unsigned char b2,
                          float t,
                          unsigned char *out_r, unsigned char *out_g, unsigned char *out_b);

} // namespace Slic3r
