# libnoise - Coherent noise generation library
# Used for advanced fuzzy skin noise patterns (Perlin, Billow, RidgedMulti, Voronoi)
# Source: OrcaSlicer's fork of libnoise 1.0 (LGPL-2.1+)

add_cmake_project(libnoise
    URL "https://github.com/SoftFever/Orca-deps-libnoise/archive/refs/tags/1.0.zip"
    URL_HASH SHA256=96ffd6cc47898dd8147aab53d7d1b1911b507d9dbaecd5613ca2649468afd8b6
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
)
