add_cmake_project(OCCT
	# Versions newer than 7.6.1 contain a bug that causes chamfers to be triangulated incorrectly.
	# So, before any updating, it is necessary to check whether SPE-2257 is still happening.
	# In version 7.9.3, this bug has still not been fixed.
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_1.zip
	URL_HASH SHA256=b7cf65430d6f099adc9df1749473235de7941120b5b5dd356067d12d0909b1d3

    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/occt_toolkit.cmake ./adm/cmake/
    CMAKE_ARGS
        -DINSTALL_DIR_LAYOUT=Unix
        -DBUILD_LIBRARY_TYPE=Static

        # Disable all external dependencies not needed for STEP meshing
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FREETYPE=OFF
        -DUSE_FREEIMAGE=OFF
        -DUSE_FFMPEG=OFF
        -DUSE_OPENVR=OFF
        -DUSE_RAPIDJSON=OFF
        -DUSE_DRACO=OFF
        -DUSE_EIGEN=OFF
        -DUSE_VTK=OFF
        -DUSE_GL2PS=OFF
        -DUSE_D3D=OFF
        -DUSE_TCL=OFF
        -DUSE_GLES2=OFF

        # Disable unnecessary modules (DataExchange auto-enables required dependencies)
        -DBUILD_MODULE_ApplicationFramework=OFF
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_Visualization=OFF
        -DBUILD_MODULE_FoundationClasses=OFF
        -DBUILD_MODULE_ModelingAlgorithms=OFF
        -DBUILD_MODULE_ModelingData=OFF

        # Enable only DataExchange (includes STEP, dependencies auto-enabled)
        # Commented out means it's ON by default
        #-DBUILD_MODULE_DataExchange=OFF

        # Disable samples, tests, and documentation
        -DBUILD_DOC_Overview=OFF
        -DBUILD_SAMPLES_MFC=OFF
        -DBUILD_SAMPLES_QT=OFF
        -DBUILD_Inspector=OFF
        -DINSTALL_SAMPLES=OFF
        -DINSTALL_TEST_CASES=OFF
        -DINSTALL_DOC_Overview=OFF

        # Build optimizations
        -DBUILD_USE_PCH=OFF
        -DBUILD_RESOURCES=OFF

        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
