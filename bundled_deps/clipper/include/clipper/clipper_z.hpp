#ifndef clipper_hpp_z
#define clipper_hpp_z

#ifdef CLIPPERLIB_USE_XYZ
    #define CLIPPERLIB_USE_XYZ_WAS_DEFINED 1
#else
    #define CLIPPERLIB_USE_XYZ_WAS_DEFINED 0
#endif
#define CLIPPERLIB_USE_XYZ 1

#ifdef clipper_hpp
    #undef clipper_hpp
    #define CLIPPER_HPP_WAS_DEFINED 1
#else
    #define CLIPPER_HPP_WAS_DEFINED 0
#endif

#include "clipper.hpp"

#if CLIPPER_HPP_WAS_DEFINED
    #define clipper_hpp
#else
    #undef clipper_hpp
#endif

#if CLIPPERLIB_USE_XYZ_WAS_DEFINED
    #define CLIPPERLIB_USE_XYZ 1
#else
    #undef CLIPPERLIB_USE_XYZ
#endif

#undef CLIPPER_HPP_WAS_DEFINED
#undef CLIPPERLIB_USE_XYZ_WAS_DEFINED

#endif // clipper_hpp_z
