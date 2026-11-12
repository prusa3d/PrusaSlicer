#include "Slic3r/Biz/Emboss/stbtt_FlattenCurves.hpp"

#include <math.h>
#define STBTT_sqrt(x) sqrt(x)

#include <stdlib.h>
#define STBTT_malloc(x, u) ((void) (u), malloc(x))
#define STBTT_free(x, u) ((void) (u), free(x))

// Code is copied(see .hpp file)
// clang-format off

namespace Slic3r::Biz::Emboss {
namespace {
// next 146 lines are copied from line 3561 to line 3707
void stbtt__add_point(stbtt__point* points, int n, float x, float y)
{
    if (!points)
        return; // during first pass, it's unallocated
    points[n].x = x;
    points[n].y = y;
}

// tessellate until threshold p is happy... @TODO warped to compensate for non-linear stretching
int stbtt__tesselate_curve(
    stbtt__point* points,
    int* num_points,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float objspace_flatness_squared,
    int n
)
{
    // midpoint
    float mx = (x0 + 2 * x1 + x2) / 4;
    float my = (y0 + 2 * y1 + y2) / 4;
    // versus directly drawn line
    float dx = (x0 + x2) / 2 - mx;
    float dy = (y0 + y2) / 2 - my;
    if (n > 16) // 65536 segments on one curve better be enough!
        return 1;
    if (dx * dx + dy * dy >
        objspace_flatness_squared) { // half-pixel error allowed... need to be smaller if AA
        stbtt__tesselate_curve(
            points, num_points, x0, y0, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, mx, my,
            objspace_flatness_squared, n + 1
        );
        stbtt__tesselate_curve(
            points, num_points, mx, my, (x1 + x2) / 2.0f, (y1 + y2) / 2.0f, x2, y2,
            objspace_flatness_squared, n + 1
        );
    } else {
        stbtt__add_point(points, *num_points, x2, y2);
        *num_points = *num_points + 1;
    }
    return 1;
}

void stbtt__tesselate_cubic(
    stbtt__point* points,
    int* num_points,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    float objspace_flatness_squared,
    int n
)
{
    // @TODO this "flatness" calculation is just made-up nonsense that seems to work well enough
    float dx0 = x1 - x0;
    float dy0 = y1 - y0;
    float dx1 = x2 - x1;
    float dy1 = y2 - y1;
    float dx2 = x3 - x2;
    float dy2 = y3 - y2;
    float dx = x3 - x0;
    float dy = y3 - y0;
    float longlen = (float) (STBTT_sqrt(dx0 * dx0 + dy0 * dy0) + STBTT_sqrt(dx1 * dx1 + dy1 * dy1) +
                             STBTT_sqrt(dx2 * dx2 + dy2 * dy2));
    float shortlen = (float) STBTT_sqrt(dx * dx + dy * dy);
    float flatness_squared = longlen * longlen - shortlen * shortlen;

    if (n > 16) // 65536 segments on one curve better be enough!
        return;

    if (flatness_squared > objspace_flatness_squared) {
        float x01 = (x0 + x1) / 2;
        float y01 = (y0 + y1) / 2;
        float x12 = (x1 + x2) / 2;
        float y12 = (y1 + y2) / 2;
        float x23 = (x2 + x3) / 2;
        float y23 = (y2 + y3) / 2;

        float xa = (x01 + x12) / 2;
        float ya = (y01 + y12) / 2;
        float xb = (x12 + x23) / 2;
        float yb = (y12 + y23) / 2;

        float mx = (xa + xb) / 2;
        float my = (ya + yb) / 2;

        stbtt__tesselate_cubic(
            points, num_points, x0, y0, x01, y01, xa, ya, mx, my, objspace_flatness_squared, n + 1
        );
        stbtt__tesselate_cubic(
            points, num_points, mx, my, xb, yb, x23, y23, x3, y3, objspace_flatness_squared, n + 1
        );
    } else {
        stbtt__add_point(points, *num_points, x3, y3);
        *num_points = *num_points + 1;
    }
}
} // namespace

// returns number of contours
stbtt__point *stbtt_FlattenCurves(stbtt_vertex *vertices, int num_verts, float objspace_flatness, int **contour_lengths, int *num_contours, void *userdata)
{
   stbtt__point *points=0;
   int num_points=0;

   float objspace_flatness_squared = objspace_flatness * objspace_flatness;
   int i,n=0,start=0, pass;

   // count how many "moves" there are to get the contour count
   for (i=0; i < num_verts; ++i)
      if (vertices[i].type == STBTT_vmove)
         ++n;

   *num_contours = n;
   if (n == 0) return 0;

   *contour_lengths = (int *) STBTT_malloc(sizeof(**contour_lengths) * n, userdata);

   if (*contour_lengths == 0) {
      *num_contours = 0;
      return 0;
   }

   // make two passes through the points so we don't need to realloc
   for (pass=0; pass < 2; ++pass) {
      float x=0,y=0;
      if (pass == 1) {
         points = (stbtt__point *) STBTT_malloc(num_points * sizeof(points[0]), userdata);
         if (points == NULL) goto error;
      }
      num_points = 0;
      n= -1;
      for (i=0; i < num_verts; ++i) {
         switch (vertices[i].type) {
            case STBTT_vmove:
               // start the next contour
               if (n >= 0)
                  (*contour_lengths)[n] = num_points - start;
               ++n;
               start = num_points;

               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x,y);
               break;
            case STBTT_vline:
               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x, y);
               break;
            case STBTT_vcurve:
               stbtt__tesselate_curve(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
            case STBTT_vcubic:
               stbtt__tesselate_cubic(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].cx1, vertices[i].cy1,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
         }
      }
      (*contour_lengths)[n] = num_points - start;
   }

   return points;
error:
   STBTT_free(points, userdata);
   STBTT_free(*contour_lengths, userdata);
   *contour_lengths = 0;
   *num_contours = 0;
   return NULL;
}
} // namespace Slic3r::Biz::Emboss

// clang-format on
