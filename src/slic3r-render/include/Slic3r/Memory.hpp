#pragma once

#include <functional>
#include <tuple>

namespace Slic3r {

/**
 * Unique Pointer deleter implementation wrapping pure function.
 * @tparam T Type of objet to be deleted (first template param of std::unique_ptr)
 * @tparam ArgsT Other argument types of additional function args.
 *
 * Usage:
 * @code
 * FuncDeleter<T> nsvg_image_deleter(nsvgDelete);
 * std::unique_ptr<NSVGimage, FuncDeleter<T>> image(img, nsvg_image_deleter);
 * @endcode
 */
template<typename T, typename ... ArgsT>
struct FuncDeleter
{
    using Func = std::function<void(T*, ArgsT...)>;
    using Args = std::tuple<ArgsT...>;
    Func m_func;
    Args m_args;

    FuncDeleter(Func func, ArgsT... args): m_func(func),  m_args(args...){}
    void operator()(T* obj)
    {
        m_func(obj, std::get<std::index_sequence_for<ArgsT>>(m_args)...);
    }
};

/**
* Unique Pointer deleter implementation wrapping pure function.
* @tparam T Type of objet to be deleted (first template param of std::unique_ptr)
* @tparam ArgsT Other argument types of additional function args.
*
* Usage:
* @code
* FuncDeleter<T> nsvg_image_deleter(nsvgDelete);
* std::unique_ptr<NSVGimage, FuncDeleter<T>> image(img, nsvg_image_deleter);
* @endcode
 */
template<typename T>
struct FuncDeleter<T>
{
    using Func = std::function<void(T*)>;
    Func m_func;

    FuncDeleter(Func func): m_func(func){}
    void operator()(T* obj)
    {
        m_func(obj);
    }
};

}