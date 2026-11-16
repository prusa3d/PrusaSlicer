#ifndef IGL_PREDICATES_ASSERT_SCALAR_H
#include <type_traits>
#ifdef LIBIGL_PREDICATES_USE_FLOAT
#define IGL_PREDICATES_ASSERT_SCALAR(Vector)                        \
  static_assert(                                         \
    std::is_same<typename Vector::Scalar, float>::value, \
    "Shewchuk's exact predicates only support float")
#else
#define IGL_PREDICATES_ASSERT_SCALAR(Vector)                           \
  static_assert(                                            \
    std::is_same<typename Vector::Scalar, double>::value || \
    std::is_same<typename Vector::Scalar, float>::value,    \
    "Shewchuk's exact predicates only support float and double")
#endif
#endif
