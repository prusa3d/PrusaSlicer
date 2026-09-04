#ifndef SLIC3R_TEST_UTILS
#define SLIC3R_TEST_UTILS

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Assert.hpp"

#include <random>

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

inline Slic3r::Domain::TriangleMesh load_model(const std::string &obj_filename)
{
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    auto mesh = Slic3r::Biz::load_obj(fpath.c_str());
    ASSERT(mesh);
    return mesh.value();
}

template<class T>
std::enable_if_t<std::is_floating_point<T>::value, T> random_value(T minv, T maxv)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<T> dist(minv, maxv);

    return dist(rng);
}

template<class T>
std::enable_if_t<std::is_integral<T>::value, T> random_value(T minv, T maxv)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<T> dist(minv, maxv);

    return dist(rng);
}

#endif // SLIC3R_TEST_UTILS
