#pragma once
#include <tl/expected.hpp>

namespace Slic3r {

template <typename T, typename E>
using expected = tl::expected<T, E>;

template <typename E>
using unexpected = tl::unexpected<E>;

// using unexpected = tl::unexpected;


}
