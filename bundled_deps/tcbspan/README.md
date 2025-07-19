Bundled with PrusaSlicer: 
https://github.com/tcbrindle/span
commit 836dc6a0efd9849cb194e88e4aa2387436bb079b
This is not the full distribution, it only contains README and span.hpp
Original README follows:


[![Standard](https://img.shields.io/badge/c%2B%2B-11/14/17/20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License](https://img.shields.io/badge/license-BSL-blue.svg)](http://www.boost.org/LICENSE_1_0.txt)
[![Build Status](https://travis-ci.org/tcbrindle/span.svg?branch=master)](https://travis-ci.org/tcbrindle/span)
[![Build status](https://ci.appveyor.com/api/projects/status/ow7cj56s108fs439/branch/master?svg=true)](https://ci.appveyor.com/project/tcbrindle/span/branch/master)
[![Try it on godbolt online](https://img.shields.io/badge/on-godbolt-blue.svg)](https://godbolt.org/z/-vlZZR) 

`std::span` implementation for C++11 and later
==============================================

This repository contains a single-header implementation of C++20's `std::span`,
conforming to the C++20 committee draft.
It is compatible with C++11, but will use newer language features if they
are available.

It differs from the implementation in the [Microsoft GSL](https://github.com/Microsoft/GSL/)
in that it is single-header and does not depend on any other GSL facilities. It
also works with C++11, while the GSL version requires C++14.

Usage
-----

The recommended way to use the implementation simply copy the file `span.hpp`
from `include/tcb/` into your own sources and `#include` it like
any other header. By default, it lives in namespace `tcb`, but this can be
customised by setting the macro `TCB_SPAN_NAMESPACE_NAME` to an appropriate string
before `#include`-ing the header -- or simply edit the source code.

The rest of the repository contains testing machinery, and is not required for
use.

Compatibility
-------------

This implementation requires a conforming C++11 (or later) compiler,  and is tested as far
back as GCC 5, Clang 3.5 and MSVC 2015 Update 3. Older compilers may work, but this is not guaranteed.

Documentation
-------------

Documentation for `std::span` is available [on cppreference](https://en.cppreference.com/w/cpp/container/span).

Implementation Notes
--------------------

### Bounds Checking ###

This implementation of `span` includes optional bounds checking, which is handled
either by throwing an exception or by calling `std::terminate()`.

The default behaviour with C++14 and later is to check the macro `NDEBUG`:
if this is set, bounds checking is disabled. Otherwise, `std::terminate()` will
be called if there is a precondition violation (i.e. the same behaviour as
`assert()`). If you wish to terminate on errors even if `NDEBUG` is set, define
the symbol `TCB_SPAN_TERMINATE_ON_CONTRACT_VIOLATION` before `#include`-ing the
header.

Alternatively, if you want to throw on a contract violation, define
`TCB_SPAN_THROW_ON_CONTRACT_VIOLATION`. This will throw an exception of an
implementation-defined type (deriving from `std::logic_error`), allowing
cleanup to happen. Note that defining this symbol will cause the checks to be
run even if `NDEBUG` is set.

Lastly, if you wish to disable contract checking even in debug builds,
`#define TCB_SPAN_NO_CONTRACT_CHECKING`.

Under C++11, due to the restrictions on `constexpr` functions, contract checking
is disabled by default even if `NDEBUG` is not set. You can change this by
defining either of the above symbols, but this will result in most of `span`'s
interface becoming non-`constexpr`.

### `constexpr` ###

This implementation is fully `constexpr` under C++17 and later. Under earlier
versions, it is "as `constexpr` as possible".

Note that even in C++17, it is generally not possible to declare a `span`
as non-default constructed `constexpr` variable, for the same reason that you
cannot form a `constexpr` pointer to a value: it involves taking the address of
a compile-time variable in a way that would be visible at run-time.
You can however use a `span` freely in a `constexpr` function. For example:

```cpp
// Okay, even in C++11
constexpr std::ptrdiff_t get_span_size(span<const int> span)
{
    return span.size();
}

constexpr int arr[] = {1, 2, 3};
constexpr auto size = get_span_size(arr); // Okay
constexpr span<const int> span{arr}; // ERROR -- not a constant expression
constexpr const int* p = arr; // ERROR -- same
```

Constructor deduction guides are provided if the compiler supports them. For
older compilers, a set of `make_span()` functions are provided as an extension
which use the same logic, for example:

   ```cpp
   constexpr int c_array[] = {1, 2, 3};
   std::array<int, 3> std_array{1, 2, 3};
   const std::vector<int> vec{1, 2, 3};

   auto s1 = make_span(c_array);   // returns span<const int, 3>
   auto s2 = make_span(std_array); // returns span<int, 3>
   auto s3 = make_span(vec);       // returns span<const int, dynamic_extent>
   ```

Alternatives
------------

* [Microsoft/GSL](https://github.com/Microsoft/GSL): The original `span` reference
  implementation from which `std::span` was born.
  
* [martinmoene/span_lite](https://github.com/martinmoene/span-lite): An
  alternative implementation which offers C++98 compatibility.

