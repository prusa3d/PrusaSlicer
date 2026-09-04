project(slic3r-biz-lua)

add_library(slic3r-biz-lua STATIC
        include/Slic3r/Biz/Lua/LuaEngine.hpp
        src/Slic3r/Biz/Lua/LuaEngine.cpp
        include/Slic3r/Biz/Lua/LuaException.hpp
        src/Slic3r/Biz/Lua/LuaException.cpp
)
target_include_directories(slic3r-biz-lua PUBLIC include)
target_include_directories(slic3r-biz-lua PRIVATE src)

find_package(Lua REQUIRED)

if(Lua_FOUND AND NOT TARGET Lua::Lua)
    add_library(Lua::Lua INTERFACE IMPORTED)
    set_target_properties(Lua::Lua PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}"
    )
endif()

find_package(sol2 REQUIRED)
target_link_libraries(slic3r-biz-lua PUBLIC
    Lua::Lua
    sol2::sol2
    boost_headeronly
)

