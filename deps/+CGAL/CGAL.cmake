add_cmake_project(
    CGAL
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v6.2.1.zip
    URL_HASH SHA256=e1c1ae04f851ccc613c4531ff6677a53837afdae19d97917b6d97660955da4d5
    CMAKE_ARGS -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

include(GNUInstallDirs)

set(DEP_CGAL_DEPENDS Boost GMP MPFR)
