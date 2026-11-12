#include "Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.hpp"

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"
#include "Slic3r/Biz/Algorithms/MeshSplitImpl.hpp"

#include <optional>
#include <stack>
#include <vector>
#include <memory>

using Slic3r::Biz::CGAL::Algorithms::MeshBoolean::cgal::CGALMeshPtr;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Transform3f;
using Slic3r::Domain::TriangleMesh;

namespace Slic3r::Biz::CGAL::Algorithms {

namespace {

/**
 * @brief csg::CSGType
 */
enum class CSGType
{
    Union,
    Difference
};

/**
 * @brief csg::CSGStackOp
 */
enum class CSGStackOp
{
    Push,
    Continue,
    Pop
};

/**
 * @brief csg::CSGPart
 */
struct CSGPart
{
    std::shared_ptr<const indexed_triangle_set> its_ptr;
    Transform3f trafo;
    CSGType operation;
    CSGStackOp stack_operation;

    CSGPart(
        std::shared_ptr<const indexed_triangle_set> ptr = {},
        CSGType op                                      = CSGType::Union,
        const Transform3f& tr                           = Transform3f::Identity()
    ) :
        its_ptr{std::move(ptr)},
        trafo{tr},
        operation{op},
        stack_operation{CSGStackOp::Continue}
    {}
};

/**
 * @brief csg::is_all_positive()
 */
bool is_all_positive(const std::vector<CSGPart>& csgmesh)
{
    return std::ranges::all_of(
        csgmesh,
        [](const CSGPart& part) { return part.operation == CSGType::Union; }
    );
}

/**
 * @brief csg::csgmesh_merge_positive_parts()
 */
indexed_triangle_set csgmesh_merge_positive_parts(const std::vector<CSGPart>& csgmesh)
{
    indexed_triangle_set m;
    for (const CSGPart& csgpart : csgmesh) {
        const CSGType op                  = csgpart.operation;
        const indexed_triangle_set* pmesh = csgpart.its_ptr.get();
        if (pmesh && op == CSGType::Union) {
            indexed_triangle_set mcpy = *pmesh;
            its_transform(mcpy, csgpart.trafo, true);
            Domain::its_merge(m, mcpy);
        }
    }

    return m;
}

/**
 * @brief csg::model_to_csgmesh()
 */
std::vector<CSGPart> model_to_csgmesh(const ModelObject& mo)
{
    std::vector<CSGPart> parts;
    parts.reserve(2 * mo.volumes.size());

    for (const ModelVolume* vol : mo.volumes) {
        if (vol == nullptr
            || !vol->mesh_ptr()
            || (!vol->is_model_part() && !vol->is_negative_volume()))
        {
            continue;
        }

        bool split_failed        = false;
        const bool attempt_split = its_is_splittable(vol->mesh().its);

        if (attempt_split) {
            // Collect partial meshes and keep outward and inward facing meshes separately.
            std::vector<indexed_triangle_set> meshes_union;
            std::vector<indexed_triangle_set> meshes_difference;
            its_split(
                vol->mesh().its,
                SplitOutputFn{
                    [&meshes_union, &meshes_difference, &split_failed](indexed_triangle_set&& its)
                    {
                        if (its.empty()) {
                            return;
                        }
                        const double volume = Domain::its_volume(its);
                        if (std::abs(volume) > 1.) {
                            if (volume > 0.) {
                                meshes_union.emplace_back(std::move(its));
                            } else if (volume < 0.) {
                                Domain::its_flip_triangles(its);
                                meshes_difference.emplace_back(std::move(its));
                            }
                        } else {
                            // Little mesh like that may be some degenerate artifact.
                            // Things like that might throw further processing off track (SPE-2661).
                            // Better do not split the mesh and work with it as we got it.
                            split_failed = true;
                        }
                    }
                }
            );

            if (!split_failed) {
                CSGPart part_begin{{}, vol->is_model_part() ? CSGType::Union : CSGType::Difference};
                part_begin.stack_operation = CSGStackOp::Push;
                parts.push_back(std::move(part_begin));

                // Add the operation for each of the partial mesh (outward-facing normals go first).
                for (std::vector<indexed_triangle_set>* meshes :
                     {&meshes_union, &meshes_difference})
                {
                    for (indexed_triangle_set& its : *meshes) {
                        CSGPart part{
                            std::make_shared<const indexed_triangle_set>(std::move(its)),
                            meshes == &meshes_union ? CSGType::Union : CSGType::Difference,
                            vol->get_matrix().cast<float>()
                        };
                        parts.push_back(std::move(part));
                    }
                }

                CSGPart part_end{{}};
                part_end.stack_operation = CSGStackOp::Pop;
                parts.push_back(std::move(part_end));
            }
        }

        if (!attempt_split || split_failed) {
            CSGPart part{
                {vol->mesh_ptr(), &vol->mesh().its},
                vol->is_model_part() ? CSGType::Union : CSGType::Difference,
                vol->get_matrix().cast<float>()
            };

            parts.push_back(std::move(part));
        }
    }

    return parts;
}

/**
 * @brief csg::check_csgmesh_booleans()
 */
std::optional<std::vector<CGALMeshPtr>> check_csgmesh_booleans(const std::vector<CSGPart>& csgrange)
{
    std::vector<CGALMeshPtr> cgalmeshes(csgrange.size());
    auto check_part = [&csgrange, &cgalmeshes](size_t i)
    {
        const CSGPart& csgpart = csgrange[i];

        // mesh can be nullptr if this is a stack push or pull
        if (!csgpart.its_ptr) {
            if (csgpart.stack_operation != CSGStackOp::Continue) {
                cgalmeshes[i] = MeshBoolean::cgal::triangle_mesh_to_cgal(indexed_triangle_set{});
            }
            return;
        }

        indexed_triangle_set m = *csgpart.its_ptr;
        its_transform(m, csgpart.trafo, true);

        try {
            CGALMeshPtr ret = MeshBoolean::cgal::triangle_mesh_to_cgal(m);

            if (!ret || MeshBoolean::cgal::empty(*ret)) {
                return;
            }

            if (MeshBoolean::cgal::does_self_intersect(*ret)) {
                return;
            }

            if (!MeshBoolean::cgal::does_bound_a_volume(*ret)) {
                return;
            }

            cgalmeshes[i] = std::move(ret);
        } catch (...) {
            return;
        }
    };

    Biz::Algorithms::Execution::for_each(
        Biz::Algorithms::Execution::ex_tbb,
        static_cast<size_t>(0),
        csgrange.size(),
        check_part
    );

    for (const CGALMeshPtr& cgalmesh : cgalmeshes) {
        if (!cgalmesh) {
            return std::nullopt;
        }
    }

    return std::move(cgalmeshes);
}

/**
 * @brief csg::detail_cgal::perform_csg()
 */
void perform_csg(CSGType op, CGALMeshPtr& dst, CGALMeshPtr& src)
{
    if (!dst && op == CSGType::Union && src) {
        dst = std::move(src);
        return;
    }

    if (!dst || !src) {
        return;
    }

    switch (op) {
    case CSGType::Union:
        MeshBoolean::cgal::plus(*dst, *src);
        break;
    case CSGType::Difference:
        MeshBoolean::cgal::minus(*dst, *src);
        break;
    }
}

/**
 * @brief csg::perform_csgmesh_booleans()
 */
CGALMeshPtr
perform_csgmesh_booleans(const std::vector<CSGPart>& csgrange, std::vector<CGALMeshPtr>& cgalmeshes)
{
    struct Frame
    {
        CSGType op;
        CGALMeshPtr cgalptr;

        explicit Frame(CSGType csgop = CSGType::Union) :
            op{csgop},
            cgalptr{MeshBoolean::cgal::triangle_mesh_to_cgal(indexed_triangle_set{})}
        {}
    };

    std::stack opstack{std::vector<Frame>{}};

    opstack.emplace();

    size_t csgidx = 0;
    for (const CSGPart& csgpart : csgrange) {
        const CSGType op     = csgpart.operation;
        CGALMeshPtr& cgalptr = cgalmeshes[csgidx++];

        if (csgpart.stack_operation == CSGStackOp::Push) {
            opstack.emplace(op);
        }

        Frame* top = &opstack.top();

        perform_csg(csgpart.operation, top->cgalptr, cgalptr);

        if (csgpart.stack_operation == CSGStackOp::Pop) {
            CGALMeshPtr src     = std::move(top->cgalptr);
            const CSGType popop = opstack.top().op;
            opstack.pop();
            CGALMeshPtr& dst = opstack.top().cgalptr;
            perform_csg(popop, dst, src);
        }
    }

    return std::move(opstack.top().cgalptr);
}

} // namespace

std::optional<TriangleMesh> merge_object_volumes(const ModelObject& model_object)
{
    const std::vector<CSGPart> parts = model_to_csgmesh(model_object);

    if (is_all_positive(parts)) {
        return TriangleMesh{csgmesh_merge_positive_parts(parts)};
    }

    try {
        std::optional<std::vector<CGALMeshPtr>> cgalmeshes = check_csgmesh_booleans(parts);
        if (!cgalmeshes.has_value()) {
            return std::nullopt;
        }

        const CGALMeshPtr merged_mesh = perform_csgmesh_booleans(parts, *cgalmeshes);
        if (merged_mesh == nullptr) {
            return std::nullopt;
        }

        return MeshBoolean::cgal::cgal_to_triangle_mesh(*merged_mesh);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace Slic3r::Biz::CGAL::Algorithms
