#ifndef slic3r_SLA_SuppotstIslands_PostProcessNeighbor_hpp_
#define slic3r_SLA_SuppotstIslands_PostProcessNeighbor_hpp_

#include "IStackFunction.hpp"
#include "NodeDataWithResult.hpp"
#include "VoronoiGraph.hpp"
#include "NodeDataWithResult.hpp"

namespace Slic3r::sla {

/// <summary>
/// Decimate data from Ex path to path
/// Done after ONE neighbor is procceessed.
/// Check if node is on circle.
/// Remember ended circle
/// Merge side branches and circle information into result
/// </summary>
class PostProcessNeighbor : public IStackFunction
{
    NodeDataWithResult &data;

public:
    VoronoiGraph::ExPath neighbor_path; // data filled in EvaluateNeighbor
    PostProcessNeighbor(NodeDataWithResult &data) : data(data) {}

    virtual void process([[maybe_unused]] CallStack &call_stack)
    {
        process();
    }

private:
    void process();
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_PostProcessNeighbor_hpp_
