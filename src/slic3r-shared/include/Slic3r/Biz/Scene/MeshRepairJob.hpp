#pragma once

#include <utility>
#include <vector>

#include <jthread/JThread.hpp>

#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Slic3r::Biz::Scene {

using RefMesh = std::pair<Domain::ElementRef, Domain::TriangleMesh>;
using RefMeshes = std::vector<RefMesh>;

/**
 * @brief Repairs meshes via the Windows 10+ WinRT mesh repair service (Windows.Graphics.Printing3D).
 *
 * Each mesh is converted to a WinRT Printing3DMesh, repaired in-process via
 * IPrinting3DModel::RepairAsync, and converted back. Meant to be run via
 * JobManager::create_job, on its own dedicated thread (WinRT is initialized
 * for the duration of the call and torn down again on return).
 *
 * Only built when SLIC3R_ENABLE_WIN10_MESH_REPAIR is set; see repair_meshes()
 * in the #else branch for the stub used otherwise.
 *
 * @param stop_token Cooperative cancellation; checked between meshes and while
 *   polling each repair operation. On cancellation, the whole batch is
 *   discarded and the original @p meshes are returned unchanged.
 * @param progress Reports overall batch progress (0..1) after each mesh.
 * @param meshes Meshes to repair, each tagged with the ElementRef it belongs to.
 * @return The repaired meshes (same ElementRefs, new geometry), or the
 *   original @p meshes unchanged if cancelled.
 */
RefMeshes repair_meshes(
    JThread::StopToken stop_token,
    Platform::JobManager::ProgressTracker progress,
    RefMeshes meshes
);

} // namespace Slic3r::Biz::Scene
