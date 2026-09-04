add_cmake_project(
        Sol2
        URL https://github.com/ThePhD/sol2/archive/refs/tags/v3.5.0.zip
        URL_HASH SHA256=b43e539415956960055f62a9d328fec3fd1ad4f272d6206631b9f022b0b12678
        CMAKE_ARGS
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -DSOL2_BUILD_LUA=OFF
)

set(DEP_Sol2_DEPENDS Lua)