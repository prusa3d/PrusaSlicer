add_cmake_project(OCCT
	# Versions newer than 7.6.1 contain a bug that causes chamfers to be triangulated incorrectly.
	# So, before any updating, it is necessary to check whether SPE-2257 is still happening.
	# In version 7.8.1, this bug has still not been fixed.
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_1.zip
	URL_HASH SHA256=b7cf65430d6f099adc9df1749473235de7941120b5b5dd356067d12d0909b1d3

    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/occt_toolkit.cmake ./adm/cmake/
    CMAKE_ARGS
        -DINSTALL_DIR_LAYOUT=Unix # LMBBS
        -DBUILD_LIBRARY_TYPE=Static
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FREETYPE=OFF
	    -DUSE_FFMPEG=OFF
	    -DUSE_VTK=OFF
	    -DUSE_FREETYPE=OFF
	    -DBUILD_MODULE_ApplicationFramework=OFF
        #-DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
		-DBUILD_MODULE_FoundationClasses=OFF
		-DBUILD_MODULE_ModelingAlgorithms=OFF
		-DBUILD_MODULE_ModelingData=OFF
		-DBUILD_MODULE_Visualization=OFF
)
