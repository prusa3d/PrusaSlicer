#ifndef SUPPORTTREESTRATEGIES_HPP
#define SUPPORTTREESTRATEGIES_HPP

#include <memory>

namespace Slic3r { namespace sla {

enum class SupportTreeType { Default, Branching, Organic };
enum class PillarConnectionMode { zigzag, cross, dynamic };

}} // namespace Slic3r::sla

#endif // SUPPORTTREESTRATEGIES_HPP
