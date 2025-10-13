project(slic3r-arrange-wrapper)
cmake_minimum_required(VERSION 3.13)

add_library(slic3r-arrange-wrapper STATIC
    include/arrange-wrapper/Arrange.hpp
    include/arrange-wrapper/ArrangeSettingsDb_AppCfg.hpp
    include/arrange-wrapper/ArrangeSettingsView.hpp
    include/arrange-wrapper/Items/ArbitraryDataStore.hpp
    include/arrange-wrapper/Items/ArrangeItem.hpp
    include/arrange-wrapper/Items/MutableItemTraits.hpp
    include/arrange-wrapper/Items/SimpleArrangeItem.hpp
    include/arrange-wrapper/Items/TrafoOnlyArrangeItem.hpp
    include/arrange-wrapper/Scene.hpp
    include/arrange-wrapper/SceneBuilder.hpp
    include/arrange-wrapper/SegmentedRectangleBed.hpp
    include/arrange-wrapper/Tasks/ArrangeTask.hpp
    include/arrange-wrapper/Tasks/FillBedTask.hpp
    include/arrange-wrapper/Tasks/MultiplySelectionTask.hpp
    include/arrange-wrapper/ModelArrange.hpp

    src/ArrangeImpl.hpp
    src/ArrangeSettingsDb_AppCfg.cpp
    src/Items/SimpleArrangeItem.cpp
    src/SceneBuilder.cpp
    src/Scene.cpp
    src/Items/ArrangeItem.cpp
    src/ModelArrange.cpp
    src/Tasks/ArrangeTaskImpl.hpp
    src/Tasks/FillBedTaskImpl.hpp
    src/Tasks/MultiplySelectionTaskImpl.hpp
)

target_include_directories(slic3r-arrange-wrapper PRIVATE src)
target_include_directories(slic3r-arrange-wrapper PUBLIC include)
target_link_libraries(slic3r-arrange-wrapper PUBLIC slic3r-arrange)
