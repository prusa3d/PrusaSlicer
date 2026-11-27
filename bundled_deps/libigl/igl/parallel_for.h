// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PARALLEL_FOR_H
#define IGL_PARALLEL_FOR_H
#include "igl_inline.h"
#include <functional>

//#warning "Defining IGL_PARALLEL_FOR_FORCE_SERIAL"
//#define IGL_PARALLEL_FOR_FORCE_SERIAL

namespace igl
{
  /// Functional implementation of a basic, open-mp style, parallel
  /// for loop. If the inner block of a for-loop can be rewritten/encapsulated in
  /// a single (anonymous/lambda) function call `func` so that the serial code
  /// looks like:
  ///
  /// \code{cpp}
  ///     for(int i = 0;i<loop_size;i++)
  ///     {
  ///       func(i);
  ///     }
  /// \endcode
  ///
  /// then `parallel_for(loop_size,func,min_parallel)` will use as many threads as
  /// available on the current hardware to parallelize this for loop so long as
  /// loop_size<min_parallel, otherwise it will just use a serial for loop.
  ///
  /// Often if your code looks like:
  ///
  /// \code{cpp}
  ///     for(int i = 0;i<loop_size;i++)
  ///     {
  ///       …
  ///     }
  /// \endcode
  ///
  /// Then you can make a minimal two-line change to parallelize it:
  ///
  /// \code{cpp}
  ///     //for(int i = 0;i<loop_size;i++)
  ///     parallel_for(loop_size,[&](int i)
  ///     {
  ///       …
  ///     }
  ///     ,1000);
  /// \endcode
  ///
  /// @param[in] loop_size  number of iterations. I.e. for(int i = 0;i<loop_size;i++) ...
  /// @param[in] func  function handle taking iteration index as only argument to compute
  ///     inner block of for loop I.e. for(int i ...){ func(i); }
  /// @param[in] min_parallel  min size of loop_size such that parallel (non-serial)
  ///     thread pooling should be attempted {0}
  /// @return true iff thread pool was invoked
  template<typename Index, typename FunctionType >
  inline bool parallel_for(
    const Index loop_size,
    const FunctionType & func,
    const size_t min_parallel=0);

  /// Functional implementation of an open-mp style, parallel for loop with
  /// accumulation. For example, serial code separated into n chunks (each to be
  /// parallelized with a thread) might look like:
  ///
  /// \code{cpp}
  ///     Eigen::VectorXd S;
  ///     const auto & prep_func = [&S](int n){ S = Eigen:VectorXd::Zero(n); };
  ///     const auto & func = [&X,&S](int i, int t){ S(t) += X(i); };
  ///     const auto & accum_func = [&S,&sum](int t){ sum += S(t); };
  ///     prep_func(n);
  ///     for(int i = 0;i<loop_size;i++)
  ///     {
  ///       func(i,i%n);
  ///     }
  ///     double sum = 0;
  ///     for(int t = 0;t<n;t++)
  ///     {
  ///       accum_func(t);
  ///     }
  /// \endcode
  ///
  /// @param[in] loop_size  number of iterations. I.e. for(int i = 0;i<loop_size;i++) ...
  /// @param[in] prep_func function handle taking n >= number of threads as only
  ///     argument
  /// @param[in] func  function handle taking iteration index i and thread id t as only
  ///     arguments to compute inner block of for loop I.e.
  ///     for(int i ...){ func(i,t); }
  /// @param[in] accum_func  function handle taking thread index as only argument, to be
  ///     called after all calls of func, e.g., for serial accumulation across
  ///     all n (potential) threads, see n in description of prep_func.
  /// @param[in] min_parallel  min size of loop_size such that parallel (non-serial)
  ///     thread pooling should be attempted {0}
  /// @return true iff thread pool was invoked
  template<
    typename Index,
    typename PrepFunctionType,
    typename FunctionType,
    typename AccumFunctionType
    >
  inline bool parallel_for(
    const Index loop_size,
    const PrepFunctionType & prep_func,
    const FunctionType & func,
    const AccumFunctionType & accum_func,
    const size_t min_parallel=0);
}

// Implementation

#include "default_num_threads.h"

#include <cmath>
#include <cassert>
#include <thread>
#include <vector>
#include <algorithm>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// ---------------------------------------------------------------------------
// Backend selection. Exactly one is active:
//
//   IGL_PARALLEL_FOR_FORCE_SERIAL  always run serially
//   IGL_PARALLEL_FOR_TBB           Intel oneTBB      (EXPERIMENTAL; needs TBB)
//   IGL_PARALLEL_FOR_OPENMP        OpenMP            (EXPERIMENTAL; needs -fopenmp)
//   (none of the above)            internal std::thread pool (default)
//
// The experimental backends' headers are included ONLY when their macro is
// defined, so the default build pulls in no TBB/OpenMP dependency whatsoever.
// ---------------------------------------------------------------------------
#if defined(IGL_PARALLEL_FOR_TBB)
#  include <tbb/parallel_for.h>
#  include <tbb/task_arena.h>
#elif defined(IGL_PARALLEL_FOR_OPENMP)
#  include <omp.h>
#endif

namespace igl {
namespace internal
{

// Thread-local flag: is the current thread already running inside a
// parallel_for region? Nested parallel_for calls run serially so a fixed pool
// (or backend) is never oversubscribed — this is the fix for the thread
// explosion in issue #2412. Scoped so the flag is restored on exit, meaning a
// worker/thread can be reused for an unrelated (non-nested) region afterwards.
inline bool & parallel_for_in_worker()
{
  static thread_local bool flag = false;
  return flag;
}
struct parallel_for_worker_scope
{
  const bool prev;
  parallel_for_worker_scope() : prev(parallel_for_in_worker())
  { parallel_for_in_worker() = true; }
  ~parallel_for_worker_scope() { parallel_for_in_worker() = prev; }
};

#if !defined(IGL_PARALLEL_FOR_FORCE_SERIAL) && \
    !defined(IGL_PARALLEL_FOR_TBB) && \
    !defined(IGL_PARALLEL_FOR_OPENMP)
// A minimal fixed-size worker pool built only on <thread>/<mutex>/etc.
//
// Design notes:
//  - Lazily created on the first *parallel* region (never on include, and never
//    when everything stays under min_parallel / single-threaded), so simply
//    linking libigl costs no background threads.
//  - Intentionally leaked (heap-allocated, never destroyed): process/library
//    teardown must not block joining idle or in-flight workers. Joining threads
//    from a static destructor is a classic source of shutdown hangs (DLL/plugin
//    unload) and is problematic on thread-restricted platforms (WASM, iPadOS —
//    the platform in #2412). The OS reclaims the blocked workers at exit.
//  - Size is taken from igl::default_num_threads(), so IGL_NUM_THREADS (and
//    friends) let an embedder cap or disable pooling.
class parallel_for_pool
{
public:
  // Created once; `nthreads` on later calls is ignored.
  static parallel_for_pool & get(const size_t nthreads)
  {
    static parallel_for_pool * instance = new parallel_for_pool(nthreads);
    return *instance; // never deleted (see class comment)
  }
  size_t size() const { return m_workers.size(); }
  void enqueue(std::function<void()> task)
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
  }
private:
  explicit parallel_for_pool(const size_t nthreads)
  {
    const size_t n = std::max<size_t>(1, nthreads);
    m_workers.reserve(n);
    for(size_t i = 0; i < n; ++i)
    {
      m_workers.emplace_back([this]()
      {
        for(;;)
        {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]{ return !m_tasks.empty(); });
            task = std::move(m_tasks.front());
            m_tasks.pop();
          }
          task();
        }
      });
    }
  }
  std::vector<std::thread> m_workers;
  std::queue<std::function<void()>> m_tasks;
  std::mutex m_mutex;
  std::condition_variable m_cv;
};
#endif

} // namespace internal
} // namespace igl

template<typename Index, typename FunctionType >
inline bool igl::parallel_for(
  const Index loop_size,
  const FunctionType & func,
  const size_t min_parallel)
{
  // no-op preparation/accumulation
  const auto & no_op = [](const size_t /*n_or_t*/){};
  // two-parameter wrapper ignoring thread id
  const auto & wrapper = [&func](Index i, size_t /*t*/){ func(i); };
  return parallel_for(loop_size, no_op, wrapper, no_op, min_parallel);
}


template<
  typename Index,
  typename PreFunctionType,
  typename FunctionType,
  typename AccumFunctionType>
inline bool igl::parallel_for(
  const Index loop_size,
  const PreFunctionType & prep_func,
  const FunctionType & func,
  const AccumFunctionType & accum_func,
  const size_t min_parallel)
{
  assert(loop_size >= 0);
  if(loop_size == 0) return false;

#ifdef IGL_PARALLEL_FOR_FORCE_SERIAL
  // Forced serial: this is the only path compiled, so no backend/pool code
  // (nor any TBB/OpenMP dependency) is referenced.
  prep_func(1);
  for(Index i = 0;i<loop_size;i++){ func(i,0); }
  accum_func(0);
  return false;
#else
  const size_t nthreads = igl::default_num_threads();

  // Serial when: below the threshold, only one thread available, or already
  // inside a parallel_for region (nested → serial keeps the thread count
  // bounded regardless of backend; see #2412).
  if(loop_size < static_cast<Index>(min_parallel) ||
     nthreads <= 1 ||
     igl::internal::parallel_for_in_worker())
  {
    prep_func(1);
    for(Index i = 0;i<loop_size;i++){ func(i,0); }
    accum_func(0);
    return false;
  }

#if defined(IGL_PARALLEL_FOR_TBB)
  // ---- EXPERIMENTAL: Intel TBB / oneTBB backend ----------------------------
  // Partition into `jobs` fixed chunks and run them via TBB over the integer
  // range [0,jobs); chunk index `t` doubles as the thread-slot id. Each slot is
  // written by exactly one task, so func(...,t)/accum(t) need no per-slot sync,
  // and this avoids version-specific APIs like this_task_arena — the integer
  // `parallel_for(first,last,body)` and `task_arena` exist across TBB versions.
  prep_func(nthreads);
  const size_t jobs =
    static_cast<size_t>(std::min<Index>(loop_size, static_cast<Index>(nthreads)));
  const Index base = loop_size / static_cast<Index>(jobs);
  const Index rem  = loop_size % static_cast<Index>(jobs);
  tbb::task_arena arena(static_cast<int>(nthreads));
  arena.execute([&]()
  {
    tbb::parallel_for(size_t(0), jobs, [&](const size_t t)
    {
      igl::internal::parallel_for_worker_scope scope;
      const Index s =
        static_cast<Index>(t)*base + std::min<Index>(static_cast<Index>(t),rem);
      const Index e = s + base + (static_cast<Index>(t) < rem ? 1 : 0);
      for(Index k = s; k < e; k++){ func(k,t); }
    });
  });
  for(size_t t = 0;t<nthreads;t++){ accum_func(t); }
  return true;
#elif defined(IGL_PARALLEL_FOR_OPENMP)
  // ---- EXPERIMENTAL: OpenMP backend ----------------------------------------
  prep_func(nthreads);
  #pragma omp parallel num_threads(static_cast<int>(nthreads))
  {
    igl::internal::parallel_for_worker_scope scope;
    const size_t t = static_cast<size_t>(omp_get_thread_num());
    #pragma omp for schedule(static)
    for(Index i = 0;i<loop_size;i++){ func(i,t); }
  }
  for(size_t t = 0;t<nthreads;t++){ accum_func(t); }
  return true;
#else
  // ---- Default: internal std::thread pool ----------------------------------
  auto & pool = igl::internal::parallel_for_pool::get(nthreads);
  const size_t P = std::max<size_t>(1, pool.size());
  // prep is called with the number of thread-id slots [0,P) that func/accum see.
  prep_func(P);

  // Partition [0,loop_size) into `jobs` contiguous chunks; chunk t is handled by
  // exactly one thread, so func(...,t)/accum(t) need no per-slot synchronization.
  const size_t jobs =
    static_cast<size_t>(std::min<Index>(loop_size, static_cast<Index>(P)));
  const Index base = loop_size / static_cast<Index>(jobs);
  const Index rem  = loop_size % static_cast<Index>(jobs);
  const auto chunk_begin = [&](const size_t t) -> Index
  {
    return static_cast<Index>(t)*base + std::min<Index>(static_cast<Index>(t),rem);
  };
  const auto chunk_end = [&](const size_t t) -> Index
  {
    return chunk_begin(t) + base + (static_cast<Index>(t) < rem ? 1 : 0);
  };

  // Completion is tracked by a counter guarded by `done_mutex` (not a lone
  // atomic): decrementing, testing-for-zero, and notifying all happen under the
  // lock, and the waiter tests the same counter under the lock. This makes the
  // last worker's notify and the caller's wake mutually exclusive, so the caller
  // cannot wake (even spuriously) and destroy these stack-local primitives while
  // a worker is still touching them.
  size_t remaining = jobs;
  std::mutex done_mutex;
  std::condition_variable done_cv;
  const auto run_chunk = [&](const size_t t)
  {
    // Mark this thread as a worker so any parallel_for inside func runs serial.
    igl::internal::parallel_for_worker_scope scope;
    const Index end = chunk_end(t);
    for(Index k = chunk_begin(t); k < end; k++){ func(k,t); }
    std::lock_guard<std::mutex> lock(done_mutex);
    if(--remaining == 0){ done_cv.notify_one(); }
  };

  // Enqueue all-but-one chunk to the pool and run the last one on the calling
  // thread — so the calling core isn't left idle, and (with nested→serial) the
  // total active threads for this call stay ≤ P. The calling thread blocks below
  // until all chunks finish, so capturing the local state by reference is safe.
  for(size_t t = 0; t + 1 < jobs; t++)
  {
    pool.enqueue([&run_chunk,t]{ run_chunk(t); });
  }
  run_chunk(jobs-1);

  {
    std::unique_lock<std::mutex> lock(done_mutex);
    done_cv.wait(lock, [&remaining]{ return remaining == 0; });
  }

  for(size_t t = 0;t<P;t++){ accum_func(t); }
  return true;
#endif // backend selection
#endif // IGL_PARALLEL_FOR_FORCE_SERIAL
}

#endif

