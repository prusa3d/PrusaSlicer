#pragma once

#include "Slic3r/Biz/Algorithms/IStackFunction.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/NodeDataWithResult.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/VoronoiGraph.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

/// <summary>
/// Decimate data from Ex path to path
/// Done after ONE neighbor is procceessed.
/// Check if node is on circle.
/// Remember ended circle
/// Merge side branches and circle information into result
/// </summary>
class PostProcessNeighbor : public Biz::Algorithms::IStackFunction
{
    NodeDataWithResult &data;

public:
    VoronoiGraph::ExPath neighbor_path; // data filled in EvaluateNeighbor
    PostProcessNeighbor(NodeDataWithResult &data) : data(data) {}

    virtual void process([[maybe_unused]] Biz::Algorithms::CallStack &call_stack) override
    {
        process();
    }

private:
    void process();
};

}
