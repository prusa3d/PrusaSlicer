///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef BRANCHINGTREESLA_HPP
#define BRANCHINGTREESLA_HPP

#include <boost/log/trivial.hpp>

#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "SupportTreeBuilder.hpp"

namespace Slic3r { namespace sla {
class SupportTreeBuilder;
struct SupportableMesh;

void create_branching_tree(SupportTreeBuilder& builder, const SupportableMesh &sm);

}} // namespace Slic3r::sla

#endif // BRANCHINGTREESLA_HPP
