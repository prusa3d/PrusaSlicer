#ifndef BRANCHINGTREESLA_HPP
#define BRANCHINGTREESLA_HPP

#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "SupportTreeBuilder.hpp"

namespace Slic3r { namespace sla {
class SupportTreeBuilder;
struct SupportableMesh;

void create_branching_tree(SupportTreeBuilder& builder, const SupportableMesh &sm);

}} // namespace Slic3r::sla

#endif // BRANCHINGTREESLA_HPP
