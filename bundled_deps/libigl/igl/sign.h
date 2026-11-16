// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SIGN_H
#define IGL_SIGN_H
namespace igl
{
  /// Returns the sign of a number. https://en.wikipedia.org/wiki/Sign_function
  ///
  /// @param[in] x The number to compute the sign of.
  /// @return 1 if x > 0, -1 if x < 0, and 0 if x == 0.
  ///
  /// https://stackoverflow.com/questions/1903954/is-there-a-standard-sign-function-signum-sgn-in-c-c
  template <typename T> inline T sign(const T & x){return (x>T(0)) - (x<T(0));}
}
#endif
