// Proxy header - to remove.
#ifndef slic3r_MutablePriorityQueue_hpp_
#define slic3r_MutablePriorityQueue_hpp_

#include "Slic3r/Biz/Algorithms/MutablePriorityQueue.hpp"

namespace Slic3r {
using Slic3r::Biz::Algorithms::MutablePriorityQueue::InvalidQueueID;
using Slic3r::Biz::Algorithms::MutablePriorityQueue::MutablePriorityQueue;
using Slic3r::Biz::Algorithms::MutablePriorityQueue::make_mutable_priority_queue;
using Slic3r::Biz::Algorithms::MutablePriorityQueue::SkipHeapAddressing;
using Slic3r::Biz::Algorithms::MutablePriorityQueue::MutableSkipHeapPriorityQueue;
using Slic3r::Biz::Algorithms::MutablePriorityQueue::make_miniheap_mutable_priority_queue;
} // namespace Slic3r

#endif /* slic3r_MutablePriorityQueue_hpp_ */
