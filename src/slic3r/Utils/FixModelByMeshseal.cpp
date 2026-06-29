///|/ meshseal integration into PrusaSlicer.
///|/ Released under AGPLv3+ (PrusaSlicer); meshseal itself is MIT.
///|/
#ifdef HAS_MESHSEAL

#include "FixModelByMeshseal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include <boost/thread.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "../GUI/I18N.hpp"

#include <wx/progdlg.h>

#include <meshseal/meshseal.h>

namespace Slic3r {

bool is_meshseal_available()
{
    // meshseal has no runtime probe — if HAS_MESHSEAL is defined the
    // library was linked in at build time. Always available.
    return true;
}

namespace {

// ITS (float, int) -> meshseal::Mesh (double, uint32_t).
// Both layouts are flat triangle soup: vertex array + index array.
// O(V + F) conversion, no metadata involved.
static meshseal::Mesh its_to_meshseal(const indexed_triangle_set& its)
{
    meshseal::Mesh m;
    m.vertices.reserve(its.vertices.size());
    for (const auto& v : its.vertices)
        m.vertices.push_back({ double(v.x()), double(v.y()), double(v.z()) });
    m.faces.reserve(its.indices.size());
    for (const auto& t : its.indices)
        m.faces.push_back({ uint32_t(t[0]), uint32_t(t[1]), uint32_t(t[2]) });
    return m;
}

// meshseal::Mesh (double, uint32_t) -> ITS (float, int).
static indexed_triangle_set meshseal_to_its(const meshseal::Mesh& m)
{
    indexed_triangle_set out;
    out.vertices.reserve(m.vertices.size());
    for (const auto& v : m.vertices)
        out.vertices.emplace_back(float(v[0]), float(v[1]), float(v[2]));
    out.indices.reserve(m.faces.size());
    for (const auto& f : m.faces)
        out.indices.emplace_back(int(f[0]), int(f[1]), int(f[2]));
    return out;
}

// Sentinel thrown from inside the worker when the user clicks Cancel.
class RepairCanceledException : public std::exception {
public:
    const char* what() const noexcept override { return "Model repair canceled"; }
};

// Repair one ModelVolume in place. Throws Slic3r::RuntimeError on failure,
// RepairCanceledException on user cancellation. The progress callback bridges
// meshseal's per-stage events to the GUI dialog.
template <typename ProgressFn, typename CancelFn>
static void fix_one_volume_in_memory(ModelVolume&    vol,
                                     ProgressFn      on_progress,
                                     CancelFn        throw_on_cancel)
{
    on_progress(L("Preparing mesh"), 5);

    const TriangleMesh& tm  = vol.mesh();
    const indexed_triangle_set& its = tm.its;
    if (its.vertices.empty() || its.indices.empty()) {
        throw Slic3r::RuntimeError("Volume mesh is empty");
    }

    meshseal::Mesh input = its_to_meshseal(its);

    throw_on_cancel();
    on_progress(L("Repairing mesh"), 15);

    meshseal::RepairOptions opts;
    // Wire meshseal's per-stage event stream into the GUI progress dialog
    // and cancellation check. Returning false from the callback raises
    // RepairCanceledException inside meshseal::repair(), which propagates
    // out through std::exception (caught below).
    std::atomic<bool> cancel_requested{false};
    opts.on_progress = [&](const meshseal::ProgressEvent& ev) -> bool {
        // Bridge meshseal's stage names to a localized GUI message.
        // Percentage is a soft estimate: 15..85 mapped against a heuristic
        // budget. meshseal does not know the total work in advance (pipeline
        // branches on input characteristics), so we cap below 90 and let
        // the final "Loading" step take it the rest of the way.
        const int pct = std::min(85, 15 + int(ev.elapsed_ms / 40.0));
        on_progress(ev.stage_name.c_str(), pct);
        // Note: we cannot throw from the callback (meshseal stays plain-C++
        // exception-safe); returning false is the documented cancel path.
        return !cancel_requested.load();
    };
    // Pre-call cancellation check; cancel_requested is wired to the
    // shared `canceled` atomic via throw_on_cancel below.
    try {
        throw_on_cancel();
    } catch (...) {
        cancel_requested.store(true);
        throw;
    }

    meshseal::RepairResult result;
    try {
        result = meshseal::repair(input, opts);
    } catch (const std::exception& e) {
        throw Slic3r::RuntimeError(std::string("meshseal::repair threw: ") + e.what());
    }

    throw_on_cancel();

    if (result.partial_failure) {
        // Look for an explicit "canceled" marker in the notes.
        for (const auto& note : result.notes) {
            if (note.find("canceled") != std::string::npos)
                throw RepairCanceledException();
        }
        // Otherwise partial_failure means the pipeline ran but couldn't
        // produce a strictly-clean output. Adopt the result anyway — it's
        // still likely closer to clean than the input. The user can decide
        // whether to re-run.
    }

    if (result.mesh.vertices.empty() || result.mesh.faces.empty()) {
        throw Slic3r::RuntimeError(
            "meshseal returned an empty mesh (input was unrepairable)");
    }

    on_progress(L("Loading repaired mesh"), 90);

    indexed_triangle_set repaired = meshseal_to_its(result.mesh);
    vol.set_mesh(TriangleMesh(std::move(repaired)));
    vol.calculate_convex_hull();
    vol.set_new_unique_id();
}

} // anonymous namespace

bool fix_model_by_meshseal_gui(ModelObject&        model_object,
                               int                 volume_idx,
                               wxProgressDialog&   progress_dialog,
                               const wxString&     msg_header,
                               std::string&        fix_result)
{
    std::mutex                      mtx;
    std::condition_variable         condition;
    struct Progress {
        std::string                 message;
        int                         percent = 0;
        bool                        updated = false;
    } progress;
    std::atomic<bool>               canceled{false};
    std::atomic<bool>               finished{false};

    std::vector<ModelVolume*> volumes;
    if (volume_idx == -1)
        volumes = model_object.volumes;
    else
        volumes.push_back(model_object.volumes[volume_idx]);

    bool   success = false;
    size_t ivolume = 0;

    auto on_progress = [&](const char* msg, unsigned prcnt) {
        std::unique_lock<std::mutex> lock(mtx);
        progress.message = msg;
        progress.percent = int((float(prcnt) + float(ivolume) * 100.f) / float(volumes.size()));
        progress.updated = true;
        condition.notify_all();
    };
    auto throw_on_cancel = [&]() {
        if (canceled) throw RepairCanceledException();
    };

    // Worker thread keeps the GUI responsive (progress_dialog polled on
    // the main thread). meshseal itself is single-threaded; the worker
    // exists for UX, not parallelism.
    auto worker = boost::thread([&]() {
        try {
            for (; ivolume < volumes.size(); ++ivolume) {
                fix_one_volume_in_memory(*volumes[ivolume],
                                         on_progress,
                                         throw_on_cancel);
            }
            model_object.invalidate_bounding_box();
            on_progress(L("Model repair finished"), 100);
            success  = true;
            finished = true;
        } catch (const RepairCanceledException&) {
            canceled = true;
            finished = true;
            on_progress(L("Model repair canceled"), 100);
        } catch (const std::exception& ex) {
            success  = false;
            finished = true;
            // Stash the message into progress so the main thread sees it.
            std::unique_lock<std::mutex> lock(mtx);
            progress.message = ex.what();
            progress.percent = 100;
            progress.updated = true;
            condition.notify_all();
        }
    });

    while (!finished) {
        std::unique_lock<std::mutex> lock(mtx);
        condition.wait_for(lock, std::chrono::milliseconds(250),
                           [&progress] { return progress.updated; });
        // Decrease by 1 to keep the dialog open until we explicitly close.
        if (!progress_dialog.Update(std::max(0, progress.percent - 1),
                                    msg_header + _(progress.message)))
            canceled = true;
        else
            progress_dialog.Fit();
        progress.updated = false;
    }

    if (canceled) {
        // Nothing to show — caller looks at the return value.
    } else if (success) {
        fix_result.clear();
    } else {
        fix_result = progress.message;
    }
    worker.join();
    return !canceled;
}

} // namespace Slic3r

#endif /* HAS_MESHSEAL */
