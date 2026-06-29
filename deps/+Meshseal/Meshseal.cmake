# Meshseal — NOT currently used by the deps/ flow.
#
# The deps/ infrastructure (add_cmake_project + ExternalProject_Add)
# requires that each dep installs into destdir/usr/local via CMake
# install() rules. meshseal v0.1.x does not yet ship install rules
# (planned for v0.1.2 — exporting the meshseal target via
# install(TARGETS ... EXPORT meshsealTargets) + meshsealConfig.cmake).
#
# Until then, meshseal is fetched as an in-tree subproject from the
# top-level PrusaSlicer CMakeLists.txt — see SLIC3R_MESHSEAL there.
# This file is a placeholder so the future move into deps/ is one
# concentrated change rather than scattered edits.

# When meshseal v0.1.2 ships with install() rules, replace the above
# with the real recipe:
#
# add_cmake_project(Meshseal
#     URL      https://github.com/jkavalik/meshseal/archive/refs/tags/v0.1.2.tar.gz
#     URL_HASH SHA256=...
#     CMAKE_ARGS
#         -DMESHSEAL_BUILD_SHARED=ON
#         -DMESHSEAL_BUILD_TESTS=OFF
#         -DMESHSEAL_BUILD_CLI=OFF
# )
