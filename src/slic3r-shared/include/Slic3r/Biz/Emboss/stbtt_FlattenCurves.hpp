#pragma once
#include "imgui/imstb_truetype.h" // stbtt_fontinfo

// create public interface for private function from imstb_truetype.h
// Implementation with dependencies is copied
namespace Slic3r::Biz::Emboss {
struct stbtt__point
{
    float x, y;
};

stbtt__point* stbtt_FlattenCurves(
    stbtt_vertex* vertices,
    int num_verts,
    float objspace_flatness,
    int** contour_lengths,
    int* num_contours,
    void* userdata
);
} // namespace Slic3r::Biz::Emboss
