"""
This creates an animation of generating a rhombic dodecahedron point by point. 
It works by generating each layer based on a the current z-height, and the
target hexagon edge length. This is a precursor to the g-code infill version of
the same geometry, used for 3d printing to maximize isometric specific strength
(most strength in all directions for the lowest weight)
"""

import math
import time
import pygame

DEG_TO_RAD = math.pi / 180
SQRT3 = 3**0.5

size = 720

def main():
    z = 0
    pygame.init()
    screen = pygame.display.set_mode([size, size])
    running = True
    while running:
        #time.sleep(0.2)
        screen.fill((255, 255, 255))

        layer_points = get_layer_points(z, 64)
        for i in range(1, len(layer_points)):
            if pygame.QUIT in (event.type for event in pygame.event.get()):
                running = False
                break
            
            pygame.draw.line(screen, pygame.Color("black"), layer_points[i-1], layer_points[i])
            time.sleep(0.001)
            pygame.display.flip()
        
        z += 1
    pygame.quit()

# raw_z: raw z height
# l: hexagon edge length
def get_layer_points(raw_z: float, l: float):
    z = (raw_z / l) % 3 # normalize it from 1 to 3, which is relative to l
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

    w = l * SQRT3 # width of hexagon
    tri_w = w * tri_frac # width of triangle
    tri_half_w = tri_w / 2
    layer_points = []

    num_rows = 2 + math.ceil(size / w)
    num_cols = 2 + math.ceil(size / w) 
    for i in range(num_rows):
        if permutation == 0:
            row_offset = (i-1) * (l + l / 2)

            if i % 2 == 0:
                for j in range(num_cols):
                    col_offset = j * w
                    top_left_tri_origin = (col_offset - w / 2, row_offset - l/2) # left hex top center
                    layer_points.append((top_left_tri_origin[0] + tri_half_w, top_left_tri_origin[1] + tri_half_w / SQRT3)) # top left tri right
                    layer_points.append((col_offset, row_offset)) # hex top left
                    left_tri_origin = (col_offset, row_offset + l) # hex bottom left
                    layer_points.append((left_tri_origin[0], left_tri_origin[1] - tri_w * SQRT3 / 3)) # left tri top
                    layer_points.append((left_tri_origin[0] - tri_half_w, left_tri_origin[1] + tri_half_w / SQRT3)) # left tri left
                    if j == num_cols - 1: break
                    layer_points.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] + tri_half_w / SQRT3)) # left tri right
                    layer_points.append((left_tri_origin[0], left_tri_origin[1] - tri_w * SQRT3 / 3)) # left tri top
                    layer_points.append((col_offset, row_offset)) # hex top left
                    top_tri_origin = (col_offset + w / 2, row_offset - l/2) # hex top center
                    layer_points.append((top_tri_origin[0] - tri_half_w, top_tri_origin[1] + tri_half_w / SQRT3)) # top tri left
            else:
                for j in range(num_cols - 1, -1, -1):
                    col_offset = j * w - w / 2
                    layer_points.append((col_offset, row_offset)) # hex top right
                    right_tri_origin = (col_offset, row_offset + l) # hex bottom right
                    layer_points.append((right_tri_origin[0], right_tri_origin[1] - tri_w * SQRT3 / 3)) # right tri top
                    layer_points.append((right_tri_origin[0] + tri_half_w, right_tri_origin[1] + tri_half_w / SQRT3)) # right tri right
                    if j == 0: break
                    layer_points.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] + tri_half_w / SQRT3)) # right tri left
                    layer_points.append((right_tri_origin[0], right_tri_origin[1] - tri_w * SQRT3 / 3)) # right tri top
                    layer_points.append((col_offset, row_offset)) # hex top right
                    top_tri_origin = (col_offset - w / 2, row_offset - l/2) # hex top center
                    layer_points.append((top_tri_origin[0] + tri_half_w, top_tri_origin[1] + tri_half_w / SQRT3)) # top tri right
                    layer_points.append((top_tri_origin[0] - tri_half_w, top_tri_origin[1] + tri_half_w / SQRT3)) # top tri left
        else:
            row_offset = (i-1) * (l + l / 2) + l / 2
            if i % 2 == 0:
                for j in range(num_cols):
                    col_offset = j * w - w / 2
                    left_tri_origin = (col_offset, row_offset) # hex top left
                    layer_points.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] - tri_half_w / SQRT3)) # left tri right
                    layer_points.append((left_tri_origin[0], left_tri_origin[1] + tri_w * SQRT3 / 3)) # left tri bot
                    layer_points.append((col_offset, row_offset + l)) # hex bottom left
                    bot_tri_origin = (col_offset + w / 2, row_offset+ 3*l/2) # hex bottom center
                    layer_points.append((bot_tri_origin[0] - tri_half_w, bot_tri_origin[1] - tri_half_w / SQRT3)) # bot tri left
                    if j == num_cols - 1: break
                    layer_points.append((bot_tri_origin[0] + tri_half_w, bot_tri_origin[1] - tri_half_w / SQRT3)) # bot tri right
                    layer_points.append((col_offset + w, row_offset + l)) # hex bottom right
                    right_tri_origin = (col_offset + w, row_offset) # hex top right
                    layer_points.append((right_tri_origin[0], right_tri_origin[1] + tri_w * SQRT3 / 3)) # right tri bot
                    layer_points.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] - tri_half_w / SQRT3)) # right tri left
            else:
                for j in range(num_cols - 1, -1, -1):
                    col_offset = j * w
                    right_tri_origin = (col_offset, row_offset) # hex top right
                    layer_points.append((right_tri_origin[0] - tri_half_w, right_tri_origin[1] - tri_half_w / SQRT3)) # right tri left
                    layer_points.append((right_tri_origin[0], right_tri_origin[1] + tri_w * SQRT3 / 3)) # right tri bot
                    layer_points.append((col_offset, row_offset + l)) # hex bottom right
                    bot_tri_origin = (col_offset - w / 2, row_offset + 3*l/2) # hex bottom center
                    layer_points.append((bot_tri_origin[0] + tri_half_w, bot_tri_origin[1] - tri_half_w / SQRT3)) # bot tri right
                    if j == 0: break
                    layer_points.append((bot_tri_origin[0] - tri_half_w, bot_tri_origin[1] - tri_half_w / SQRT3)) # bot tri left
                    layer_points.append((col_offset - w, row_offset + l)) # hex bottom left
                    left_tri_origin = (col_offset - w, row_offset) # hex top left
                    layer_points.append((left_tri_origin[0], left_tri_origin[1] + tri_w * SQRT3 / 3)) # left tri bot
                    layer_points.append((left_tri_origin[0] + tri_half_w, left_tri_origin[1] - tri_half_w / SQRT3)) # left tri right

    return layer_points

main()
