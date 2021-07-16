# project('qhull',
#     'c', 'cpp',
# )

libqhull_version = '7.2.0'

include_qhull = include_directories('src')

libqhull_headers = [
    # reentrant qhull HEADERS:
    'src/libqhull_r/libqhull_r.h',
    'src/libqhull_r/geom_r.h',
    'src/libqhull_r/io_r.h',
    'src/libqhull_r/mem_r.h',
    'src/libqhull_r/merge_r.h',
    'src/libqhull_r/poly_r.h',
    'src/libqhull_r/qhull_ra.h',
    'src/libqhull_r/qset_r.h',
    'src/libqhull_r/random_r.h',
    'src/libqhull_r/stat_r.h',
    'src/libqhull_r/user_r.h',

    # C++ interface to reentrant Qhull HEADERS:
    'src/libqhullcpp/Coordinates.h',
    'src/libqhullcpp/functionObjects.h',
    'src/libqhullcpp/PointCoordinates.h',
    'src/libqhullcpp/Qhull.h',
    'src/libqhullcpp/QhullError.h',
    'src/libqhullcpp/QhullFacet.h',
    'src/libqhullcpp/QhullFacetList.h',
    'src/libqhullcpp/QhullFacetSet.h',
    'src/libqhullcpp/QhullHyperplane.h',
    'src/libqhullcpp/QhullIterator.h',
    'src/libqhullcpp/QhullLinkedList.h',
    'src/libqhullcpp/QhullPoint.h',
    'src/libqhullcpp/QhullPoints.h',
    'src/libqhullcpp/QhullPointSet.h',
    'src/libqhullcpp/QhullQh.h',
    'src/libqhullcpp/QhullRidge.h',
    'src/libqhullcpp/QhullSet.h',
    'src/libqhullcpp/QhullSets.h',
    'src/libqhullcpp/QhullStat.h',
    'src/libqhullcpp/QhullVertex.h',
    'src/libqhullcpp/QhullVertexSet.h',
    'src/libqhullcpp/RboxPoints.h',
    'src/libqhullcpp/RoadError.h',
    'src/libqhullcpp/RoadLogEvent.h',
    'src/qhulltest/RoadTest.h',
]

libqhull_sources = [
    # reentrant qhull SOURCES:
    'src/libqhull_r/global_r.c',
    'src/libqhull_r/stat_r.c',
    'src/libqhull_r/geom2_r.c',
    'src/libqhull_r/poly2_r.c',
    'src/libqhull_r/merge_r.c',
    'src/libqhull_r/libqhull_r.c',
    'src/libqhull_r/geom_r.c',
    'src/libqhull_r/poly_r.c',
    'src/libqhull_r/qset_r.c',
    'src/libqhull_r/mem_r.c',
    'src/libqhull_r/random_r.c',
    'src/libqhull_r/usermem_r.c',
    'src/libqhull_r/io_r.c',
    'src/libqhull_r/user_r.c',
    'src/libqhull_r/rboxlib_r.c',

    # C++ interface to reentrant Qhull SOURCES:
    'src/libqhullcpp/Coordinates.cpp',
    'src/libqhullcpp/PointCoordinates.cpp',
    'src/libqhullcpp/Qhull.cpp',
    'src/libqhullcpp/QhullFacet.cpp',
    'src/libqhullcpp/QhullFacetList.cpp',
    'src/libqhullcpp/QhullFacetSet.cpp',
    'src/libqhullcpp/QhullHyperplane.cpp',
    'src/libqhullcpp/QhullPoint.cpp',
    'src/libqhullcpp/QhullPointSet.cpp',
    'src/libqhullcpp/QhullPoints.cpp',
    'src/libqhullcpp/QhullQh.cpp',
    'src/libqhullcpp/QhullRidge.cpp',
    'src/libqhullcpp/QhullSet.cpp',
    'src/libqhullcpp/QhullStat.cpp',
    'src/libqhullcpp/QhullVertex.cpp',
    'src/libqhullcpp/QhullVertexSet.cpp',
    'src/libqhullcpp/RboxPoints.cpp',
    'src/libqhullcpp/RoadError.cpp',
    'src/libqhullcpp/RoadLogEvent.cpp',
]

lib_libqhull = static_library('qhull',
    libqhull_sources,
    include_directories: include_qhull,
    dependencies: [ libm, ],
)

libqhull = declare_dependency(
    link_with: lib_libqhull,
    include_directories: include_qhull,
)
