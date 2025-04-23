add_cmake_project(
    CGAL
    # GIT_REPOSITORY https://github.com/CGAL/cgal.git
    # GIT_TAG        bec70a6d52d8aacb0b3d82a7b4edc3caa899184b # releases/CGAL-5.0
    # For whatever reason, this keeps downloading forever (repeats downloads if finished)
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v6.0.1.zip
    URL_HASH SHA256=6aa3837ebcefc39a53a7e6ac8ac08d7695d942e2eaab3709dc43da118cd10bc4
)

include(GNUInstallDirs)

set(DEP_CGAL_DEPENDS Boost GMP MPFR)
