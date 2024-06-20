add_cmake_project(OCCT
    #LMBBS: changed version to 7.6.2
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_2.zip
    URL_HASH SHA256=c696b923593e8c18d059709717dbf155b3e72fdd283c8522047a790ec3a432c5

    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/occt_toolkit.cmake ./adm/cmake/
    CMAKE_ARGS
        -DINSTALL_DIR_LAYOUT=Unix # LMBBS
        -DBUILD_LIBRARY_TYPE=Static
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FREETYPE=OFF
	    -DUSE_FFMPEG=OFF
	    -DUSE_VTK=OFF
	    -DBUILD_MODULE_ApplicationFramework=OFF
        #-DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
		-DBUILD_MODULE_FoundationClasses=OFF
		-DBUILD_MODULE_ModelingAlgorithms=OFF
		-DBUILD_MODULE_ModelingData=OFF
		-DBUILD_MODULE_Visualization=OFF
)
