///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Roman Beránek @zavorka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifdef WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
#endif

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <istream>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/bind/bind.hpp>
#include <boost/config.hpp>
#include <boost/config/warning_disable.hpp>
#include <boost/container/small_vector.hpp>
#ifdef _WIN32
// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
// https://github.com/microsoft/STL/issues/147#issuecomment-1090148740
// Thus it is recommended to use boost::container::deque instead.
#include <boost/container/deque.hpp>
#endif // _WIN32
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_array.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/iostream.hpp>

// boost/property_tree/json_parser/detail/parser.hpp includes boost/bind.hpp, which is deprecated.
// Suppress the following boost message:
// The practice of declaring the Bind placeholders (_1, _2, ...) in the global namespace is deprecated.
#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#undef BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <boost/thread.hpp>
#include <boost/version.hpp>

#include <tbb/parallel_for.h>
#include <tbb/scalable_allocator.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <cereal/access.hpp>
#include <cereal/types/base_class.hpp>

#include <clipper/clipper_z.hpp>
#include "libslic3r/clipper.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/enum_bitmask.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/SVG.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r_version.h"

#include <admesh/stl.h>
