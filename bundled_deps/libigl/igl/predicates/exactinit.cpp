// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "exactinit.h"
#include <predicates.h>

IGL_INLINE void igl::predicates::exactinit() {
  // Thread-safe initialization using Meyers' singleton
  class MySingleton {
  public:
    static MySingleton& instance() {
      static MySingleton instance;
      return instance;
    }
  private:
    MySingleton() { ::exactinit(); }
  };
  MySingleton::instance();
}
