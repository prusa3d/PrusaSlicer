"""
This creates rhombic dodecahedron infill pattern for 3d printing to maximize
isometric specific strength (most strength in all directions for the lowest
weight). It works by generating each layer based on a the current z-height. And
each point point by point.Each layer is normalized to a global grid to preserve
layer alignment.
"""

import math
from typing import List

from .fill_base import Fill, FillParams, Segment

class FillRhodo(Fill):
    def _fill_surface_single(self, params: FillParams, thickness_layers: int, surface) -> List[Segment]:
        min_spacing    = params.spacing
        hex_side       = min_spacing / params.density;
        hex_width      = hex_side * math.sqrt(3);
        pattern_height = hex_side * 1.5
        z              = (params.z / hex_side) % 3 # normalize it from 1 to 3, relative to hex_side

        permutation = 0 # perm0 is upright triangles, perm1 is upside down triangles
        if z < 1: # phase 1 hex hold
            tri_frac = 0
        elif z < 1.25: # phase 2 transition
            tri_frac = (z - 1) * 4
        elif z < 1.5: # phase 3 perm1 reverse transition
            tri_frac = (1.5 - z) * 4
            permutation = 1
        elif z < 2.5: # phase 4 perm1 hex hold
            tri_frac = 0
            permutation = 1
        elif z < 2.75: # phase 5 perm1 transition
            tri_frac = (z - 2.5) * 4
            permutation = 1
        else: # phase 6 reverse transition
            tri_frac = (3 - z) * 4

        tri_w       = hex_width * tri_frac # width of triangle
        tri_half_w  = tri_w / 2

        xmin, ymin, xmax, ymax = surface.bbox
        xmin = surface.align_to_grid(xmin, hex_width) # rhodo width
        ymin = surface.align_to_grid(ymin, hex_side * 3) # rhodo height
        w = xmax - xmin
        h = ymax - ymin
        num_rows = 2 + math.ceil(h / pattern_height)
        num_cols = 2 + math.ceil(w / hex_width) 
        
        polylines: List[List[Point]] = []
        for i in range(num_rows):
            polyline: List[Point] = []
            if permutation == 0:
                y_offset = ymin + (i-1) * pattern_height

                if i % 2 == 0:
                    for j in range(num_cols):
                        x_offset = xmin + j * hex_width
                        top_left_tri_origin = (x_offset - hex_width / 2, y_offset - hex_side/2) # left hex top center
                        polyline.append((top_left_tri_origin[0] + tri_half_w, top_left_tri_origin[1] + tri_half_w / math.sqrt(3))) # top left tri right
                        polyline.append((x_offset, y_offset)) # hex top left
                        left_tri_origin = (x_offset, y_offset + hex_side) # hex bottom left
                        polyline.append((left_tri_origin[0], left_tri_origin[1] - tri_w * math.sqrt(3) / 3)) # left tri top
                        polyline.append((left_tri_origin[0] - tri_half_w, left_tri_origin[1] + tri_half_w / math.sqrt(3))) # left tri left
                        if j == num_cols - 1: break
                        polyline.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] + tri_half_w / math.sqrt(3))) # left tri right
                        polyline.append((left_tri_origin[0], left_tri_origin[1] - tri_w * math.sqrt(3) / 3)) # left tri top
                        polyline.append((x_offset, y_offset)) # hex top left
                        top_tri_origin = (x_offset + hex_width / 2, y_offset - hex_side/2) # hex top center
                        polyline.append((top_tri_origin[0] - tri_half_w, top_tri_origin[1] + tri_half_w / math.sqrt(3))) # top tri left
                else:
                    for j in range(num_cols - 1, -1, -1):
                        x_offset = xmin + j * hex_width - hex_width / 2
                        polyline.append((x_offset, y_offset)) # hex top right
                        right_tri_origin = (x_offset, y_offset + hex_side) # hex bottom right
                        polyline.append((right_tri_origin[0], right_tri_origin[1] - tri_w * math.sqrt(3) / 3)) # right tri top
                        polyline.append((right_tri_origin[0] + tri_half_w, right_tri_origin[1] + tri_half_w / math.sqrt(3))) # right tri right
                        if j == 0: break
                        polyline.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] + tri_half_w / math.sqrt(3))) # right tri left
                        polyline.append((right_tri_origin[0], right_tri_origin[1] - tri_w * math.sqrt(3) / 3)) # right tri top
                        polyline.append((x_offset, y_offset)) # hex top right
                        top_tri_origin = (x_offset - hex_width / 2, y_offset - hex_side/2) # hex top center
                        polyline.append((top_tri_origin[0] + tri_half_w, top_tri_origin[1] + tri_half_w / math.sqrt(3))) # top tri right
                        polyline.append((top_tri_origin[0] - tri_half_w, top_tri_origin[1] + tri_half_w / math.sqrt(3))) # top tri left
            else:
                y_offset = ymin + (i-1) * pattern_height + hex_side / 2
                if i % 2 == 0:
                    for j in range(num_cols):
                        x_offset = xmin + j * hex_width - hex_width / 2
                        left_tri_origin = (x_offset, y_offset) # hex top left
                        polyline.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] - tri_half_w / math.sqrt(3))) # left tri right
                        polyline.append((left_tri_origin[0], left_tri_origin[1] + tri_w * math.sqrt(3) / 3)) # left tri bot
                        polyline.append((x_offset, y_offset + hex_side)) # hex bottom left
                        bot_tri_origin = (x_offset + hex_width / 2, y_offset+ 3*hex_side/2) # hex bottom center
                        polyline.append((bot_tri_origin[0] - tri_half_w, bot_tri_origin[1] - tri_half_w / math.sqrt(3))) # bot tri left
                        if j == num_cols - 1: break
                        polyline.append((bot_tri_origin[0] + tri_half_w, bot_tri_origin[1] - tri_half_w / math.sqrt(3))) # bot tri right
                        polyline.append((x_offset + hex_width, y_offset + hex_side)) # hex bottom right
                        right_tri_origin = (x_offset + hex_width, y_offset) # hex top right
                        polyline.append((right_tri_origin[0], right_tri_origin[1] + tri_w * math.sqrt(3) / 3)) # right tri bot
                        polyline.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] - tri_half_w / math.sqrt(3))) # right tri left
                else:
                    for j in range(num_cols - 1, -1, -1):
                        x_offset = xmin + j * hex_width
                        right_tri_origin = (x_offset, y_offset) # hex top right
                        polyline.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] - tri_half_w / math.sqrt(3))) # right tri left
                        polyline.append((right_tri_origin[0], right_tri_origin[1] + tri_w * math.sqrt(3) / 3)) # right tri bot
                        polyline.append((x_offset, y_offset + hex_side)) # hex bottom right
                        bot_tri_origin = (x_offset - hex_width / 2, y_offset + 3*hex_side/2) # hex bottom center
                        polyline.append((bot_tri_origin[0] + tri_half_w, bot_tri_origin[1] - tri_half_w / math.sqrt(3))) # bot tri right
                        if j == 0: break
                        polyline.append((bot_tri_origin[0] - tri_half_w, bot_tri_origin[1] - tri_half_w / math.sqrt(3))) # bot tri left
                        polyline.append((x_offset - hex_width, y_offset + hex_side)) # hex bottom left
                        left_tri_origin = (x_offset - hex_width, y_offset) # hex top left
                        polyline.append((left_tri_origin[0], left_tri_origin[1] + tri_w * math.sqrt(3) / 3)) # left tri bot
                        polyline.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] - tri_half_w / math.sqrt(3))) # left tri right
            polylines.append(polyline)
        return polylines