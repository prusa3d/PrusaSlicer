# Portable Strawberry Perl for building OpenSSL on Windows. Only ever included from
# OpenSSL.cmake, on WIN32.
#
# OpenSSL's Configure needs a native Windows Perl. Do not search PATH for one: the Perl
# bundled with Git for Windows gets picked up on many machines and it is a stripped MSYS
# build that lacks core modules and treats paths the POSIX way, which breaks VC-* targets.
# Fetch a pinned portable Strawberry Perl as a dependency instead, so the build is
# self-contained and reproducible for anyone building the deps.
#
# Not a "+Perl" package directory by design: it is a build tool, not a library, and must
# not show up in the flatpak manifest, which is generated from the +<name> directories.
ExternalProject_Add(dep_Perl
    EXCLUDE_FROM_ALL ON
    URL "https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54051_64bit/strawberry-perl-5.40.5.1-64bit-portable.zip"
    URL_HASH SHA256=6619fe7eeef921ccddb4aac3972fb602a0c690a3074205b863ade998d7bc79a6
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/Perl
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)
ExternalProject_Get_Property(dep_Perl SOURCE_DIR)
set(_perl_exe "${SOURCE_DIR}/perl/bin/perl.exe")
