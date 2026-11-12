#include "Slic3r/Biz/Scene/MeshRepairJob.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Log.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>

#if SLIC3R_ENABLE_WIN10_MESH_REPAIR

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <roapi.h> // Windows Runtime
#include <wrl/client.h> // for ComPtr
#include <VersionHelpers.h> // for IsWindows10OrGreater()
#include <winrt/robuffer.h>
#include <winrt/windows.graphics.printing3d.h>

#include <chrono>
#include <thread>

namespace Slic3r::Biz::Scene {

namespace {

// Minimal WinRT activation plumbing, ported from the legacy FixModelByWin10.cpp.
// RoInitialize/RoActivateInstance are resolved dynamically (rather than linked)
// so that this translation unit does not force a hard dependency on ComBase.dll
// at process load time, matching the legacy behaviour.

extern "C" {
typedef HRESULT(__stdcall* FunctionRoInitialize)(int);
typedef HRESULT(__stdcall* FunctionRoUninitialize)();
typedef HRESULT(__stdcall* FunctionRoActivateInstance)(HSTRING, IInspectable**);
typedef HRESULT(__stdcall* FunctionRoGetActivationFactory)(HSTRING, REFIID, void**);
typedef HRESULT(__stdcall* FunctionWindowsCreateString)(LPCWSTR, UINT32, HSTRING*);
typedef HRESULT(__stdcall* FunctionWindowsDeleteString)(HSTRING);
}

struct WinRTFunctions
{
    WinRTFunctions()
    {
        if (HMODULE library = LoadLibrary(L"ComBase.dll"); library) {
            ro_initialize              = (FunctionRoInitialize)GetProcAddress(library, "RoInitialize");
            ro_uninitialize            = (FunctionRoUninitialize)GetProcAddress(library, "RoUninitialize");
            ro_activate_instance       = (FunctionRoActivateInstance)GetProcAddress(library, "RoActivateInstance");
            ro_get_activation_factory  = (FunctionRoGetActivationFactory)GetProcAddress(library, "RoGetActivationFactory");
            windows_create_string      = (FunctionWindowsCreateString)GetProcAddress(library, "WindowsCreateString");
            windows_delete_string      = (FunctionWindowsDeleteString)GetProcAddress(library, "WindowsDeleteString");
        } else {
            throw Slic3r::RuntimeError("Failed to load the WinRT runtime library (ComBase.dll).");
        }
        if (! ro_initialize || ! ro_uninitialize || ! ro_activate_instance || ! ro_get_activation_factory
            || ! windows_create_string || ! windows_delete_string)
            throw Slic3r::RuntimeError("Failed to load the WinRT runtime library (ComBase.dll).");
    }

    FunctionRoInitialize ro_initialize = nullptr;
    FunctionRoUninitialize ro_uninitialize = nullptr;
    FunctionRoActivateInstance ro_activate_instance = nullptr;
    FunctionRoGetActivationFactory ro_get_activation_factory = nullptr;
    FunctionWindowsCreateString windows_create_string = nullptr;
    FunctionWindowsDeleteString windows_delete_string = nullptr;
};

WinRTFunctions& winrt_functions()
{
    // Function pointers resolved via GetProcAddress are process-wide and safe to
    // cache in a static; unlike COM/WinRT object instances they are not tied to a
    // particular thread's apartment, so this (unlike anything below) may be reused
    // across repair jobs / threads.
    // Initialization is thread-safe since C++11, if it ever comes to that.
    static WinRTFunctions functions;
    return functions;
}

HRESULT winrt_activate_instance(const WinRTFunctions& fns, const std::wstring& class_name, IInspectable** out)
{
    HSTRING hstr;
    HRESULT hr = fns.windows_create_string(class_name.c_str(), UINT32(class_name.size()), &hstr);
    if (FAILED(hr))
        return hr;
    hr = fns.ro_activate_instance(hstr, out);
    fns.windows_delete_string(hstr);
    return hr;
}

template <typename T>
HRESULT winrt_activate_instance(const WinRTFunctions& fns, const std::wstring& class_name, T** out)
{
    IInspectable* inspectable = nullptr;
    HRESULT hr = winrt_activate_instance(fns, class_name, &inspectable);
    if (FAILED(hr))
        return hr;
    hr = inspectable->QueryInterface(__uuidof(T), reinterpret_cast<void**>(out));
    inspectable->Release();
    return hr;
}

// Polls an async WinRT operation until it finishes, periodically checking stop_token
// so the repair can be cancelled cooperatively (there is no WinRT callback mechanism
// wired up here, same as in the legacy code this replaces).
template <typename T>
AsyncStatus winrt_async_await(const Microsoft::WRL::ComPtr<T>& async_action, const JThread::StopToken& stop_token)
{
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> async_info;
    async_action.As(&async_info);
    for (;;) {
        AsyncStatus status;
        async_info->get_Status(&status);
        if (status != AsyncStatus::Started)
            return status;
        if (stop_token.stop_requested())
            return AsyncStatus::Canceled;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Initializes the WinRT runtime for the lifetime of the calling thread (a repair job
// runs on its own dedicated JThread, so this is scoped to a single repair_meshes() call).
class WinRTScope
{
public:
    WinRTScope()
    {
        // Windows.Graphics.Printing3D exists from the first Windows 10 SDK onward, but
        // IsWindows10OrGreater() is what actually reflects the *running* OS (unlike the
        // legacy registry-ProductName check this replaces, it correctly returns true on
        // Windows 11 too, since it reports major=10/minor=0 internally).
        if (!IsWindows10OrGreater())
            throw Slic3r::RuntimeError("Mesh repair requires Windows 10 or newer.");

        WinRTFunctions& fns = winrt_functions();
        if (HRESULT hr = fns.ro_initialize(RO_INIT_MULTITHREADED); FAILED(hr))
            throw Slic3r::RuntimeError("Failed to initialize the WinRT runtime.");
    }
    ~WinRTScope() { winrt_functions().ro_uninitialize(); }

    WinRTScope(const WinRTScope&) = delete;
    WinRTScope& operator=(const WinRTScope&) = delete;
};

using Microsoft::WRL::ComPtr;
namespace Streams = ABI::Windows::Storage::Streams;
namespace Printing3D = ABI::Windows::Graphics::Printing3D;

// Copies data into a WinRT buffer that a Printing3DMesh already allocated internally
// (via one of its CreateVertexPositions/CreateTriangleIndices/... methods).
void winrt_fill_buffer(Streams::IBuffer* buffer, const void* data, uint32_t length_bytes)
{
    byte* buffer_ptr = nullptr;
    {
        ComPtr<Windows::Storage::Streams::IBufferByteAccess> byte_access;
        if (HRESULT hr = buffer->QueryInterface(IID_PPV_ARGS(byte_access.GetAddressOf())); FAILED(hr))
            throw Slic3r::RuntimeError("Failed to access a WinRT mesh data buffer.");
        if (HRESULT hr = byte_access->Buffer(&buffer_ptr); FAILED(hr))
            throw Slic3r::RuntimeError("Failed to access a WinRT mesh data buffer.");
    }
    std::memcpy(buffer_ptr, data, length_bytes);

    if (HRESULT hr = buffer->put_Length(length_bytes); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the length of a WinRT mesh data buffer.");
}

// Reads the whole content of a WinRT buffer out into a plain, owned byte vector.
std::vector<uint8_t> winrt_read_buffer(Streams::IBuffer* buffer)
{
    uint32_t length = 0;
    if (HRESULT hr = buffer->get_Length(&length); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to read the length of a WinRT mesh data buffer.");

    byte* buffer_ptr = nullptr;
    ComPtr<Windows::Storage::Streams::IBufferByteAccess> byte_access;
    if (HRESULT hr = buffer->QueryInterface(IID_PPV_ARGS(byte_access.GetAddressOf())); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access a WinRT mesh data buffer.");
    if (HRESULT hr = byte_access->Buffer(&buffer_ptr); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access a WinRT mesh data buffer.");

    return std::vector<uint8_t>(buffer_ptr, buffer_ptr + length);
}

ComPtr<Printing3D::IPrinting3DMesh> winrt_mesh_from_triangle_mesh(
    const WinRTFunctions& fns,
    const Domain::TriangleMesh& mesh
)
{
    ComPtr<Printing3D::IPrinting3DMesh> winrt_mesh;
    if (HRESULT hr = winrt_activate_instance(fns, L"Windows.Graphics.Printing3D.Printing3DMesh", winrt_mesh.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to create a WinRT Printing3DMesh instance.");

    const indexed_triangle_set& its = mesh.its;
    const uint32_t vertex_count   = uint32_t(its.vertices.size());
    const uint32_t triangle_count = uint32_t(its.indices.size());
    SPDLOG_INFO("MeshRepairJob: building WinRT mesh, {} vertices, {} triangles", vertex_count, triangle_count);

    if (HRESULT hr = winrt_mesh->put_VertexCount(vertex_count); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the vertex count of a WinRT mesh.");
    // Per Microsoft's own sample ("Generate a 3D Manufacturing Format package"), IndexCount
    // is the number of *triangles*, matching Stride=3 (three indices per element) below --
    // not the flat scalar index count.
    if (HRESULT hr = winrt_mesh->put_IndexCount(triangle_count); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the triangle index count of a WinRT mesh.");

    // Stride is a component count (e.g. 3 for one xyz vertex, or 3 indices per triangle),
    // not a byte count -- and per the same sample, vertex positions are doubles, not floats.
    if (HRESULT hr = winrt_mesh->put_VertexPositionsDescription(
            Printing3D::Printing3DBufferDescription{Printing3D::Printing3DBufferFormat_Printing3DDouble, 3}
        ); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the vertex positions format of a WinRT mesh.");
    if (HRESULT hr = winrt_mesh->put_TriangleIndicesDescription(
            Printing3D::Printing3DBufferDescription{Printing3D::Printing3DBufferFormat_Printing3DUInt, 3}
        ); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the triangle indices format of a WinRT mesh.");

    std::vector<double> positions(size_t(vertex_count) * 3);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        positions[i * 3 + 0] = its.vertices[i].x();
        positions[i * 3 + 1] = its.vertices[i].y();
        positions[i * 3 + 2] = its.vertices[i].z();
    }
    const uint32_t position_bytes = uint32_t(positions.size() * sizeof(double));
    if (HRESULT hr = winrt_mesh->CreateVertexPositions(position_bytes); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to allocate the vertex positions of a WinRT mesh.");
    ComPtr<Streams::IBuffer> vertex_buffer;
    if (HRESULT hr = winrt_mesh->GetVertexPositions(vertex_buffer.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access the vertex positions of a WinRT mesh.");
    winrt_fill_buffer(vertex_buffer.Get(), positions.data(), position_bytes);

    std::vector<uint32_t> indices(size_t(triangle_count) * 3);
    for (uint32_t i = 0; i < triangle_count; ++i) {
        indices[i * 3 + 0] = uint32_t(its.indices[i][0]);
        indices[i * 3 + 1] = uint32_t(its.indices[i][1]);
        indices[i * 3 + 2] = uint32_t(its.indices[i][2]);
    }
    const uint32_t index_bytes = uint32_t(indices.size() * sizeof(uint32_t));
    if (HRESULT hr = winrt_mesh->CreateTriangleIndices(index_bytes); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to allocate the triangle indices of a WinRT mesh.");
    ComPtr<Streams::IBuffer> index_buffer;
    if (HRESULT hr = winrt_mesh->GetTriangleIndices(index_buffer.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access the triangle indices of a WinRT mesh.");
    winrt_fill_buffer(index_buffer.Get(), indices.data(), index_bytes);

    return winrt_mesh;
}

Domain::TriangleMesh triangle_mesh_from_winrt_mesh(Printing3D::IPrinting3DMesh* winrt_mesh)
{
    uint32_t vertex_count   = 0;
    uint32_t triangle_count = 0;
    if (HRESULT hr = winrt_mesh->get_VertexCount(&vertex_count); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to read the vertex count of the repaired mesh.");
    if (HRESULT hr = winrt_mesh->get_IndexCount(&triangle_count); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to read the triangle index count of the repaired mesh.");
    SPDLOG_INFO("MeshRepairJob: repaired mesh reports {} vertices, {} triangles", vertex_count, triangle_count);

    ComPtr<Streams::IBuffer> vertex_buffer;
    if (HRESULT hr = winrt_mesh->GetVertexPositions(vertex_buffer.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to read the vertex positions of the repaired mesh.");
    ComPtr<Streams::IBuffer> index_buffer;
    if (HRESULT hr = winrt_mesh->GetTriangleIndices(index_buffer.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to read the triangle indices of the repaired mesh.");

    const std::vector<uint8_t> position_bytes = winrt_read_buffer(vertex_buffer.Get());
    const std::vector<uint8_t> index_bytes    = winrt_read_buffer(index_buffer.Get());
    SPDLOG_INFO(
        "MeshRepairJob: repaired mesh buffers: {} position bytes (expected {}), {} index bytes (expected {})",
        position_bytes.size(), size_t(vertex_count) * 3 * sizeof(double),
        index_bytes.size(), size_t(triangle_count) * 3 * sizeof(uint32_t)
    );
    if (position_bytes.size() < size_t(vertex_count) * 3 * sizeof(double))
        throw Slic3r::RuntimeError("Repaired WinRT mesh returned a vertex buffer smaller than its reported vertex count.");
    if (index_bytes.size() < size_t(triangle_count) * 3 * sizeof(uint32_t))
        throw Slic3r::RuntimeError("Repaired WinRT mesh returned an index buffer smaller than its reported triangle count.");
    // memcpy into properly-typed buffers rather than reinterpret_cast'ing the raw uint8_t
    // storage: no double/uint32_t objects actually live in position_bytes/index_bytes, so
    // reading through a reinterpret_cast pointer would violate strict aliasing (and isn't
    // even guaranteed to be aligned for double).
    std::vector<double> positions(size_t(vertex_count) * 3);
    std::memcpy(positions.data(), position_bytes.data(), positions.size() * sizeof(double));
    std::vector<uint32_t> indices_raw(size_t(triangle_count) * 3);
    std::memcpy(indices_raw.data(), index_bytes.data(), indices_raw.size() * sizeof(uint32_t));

    std::vector<Domain::Vec3f> vertices(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i)
        vertices[i] = Domain::Vec3f{float(positions[i * 3 + 0]), float(positions[i * 3 + 1]), float(positions[i * 3 + 2])};

    std::vector<Domain::Index3> faces(triangle_count);
    for (uint32_t i = 0; i < triangle_count; ++i)
        faces[i] = Domain::Index3{
            int(indices_raw[i * 3 + 0]),
            int(indices_raw[i * 3 + 1]),
            int(indices_raw[i * 3 + 2])
        };

    return Biz::Algorithms::TriangleMesh::construct(std::move(vertices), std::move(faces));
}

// Repairs a single mesh via the Windows 10+ mesh repair service. Returns nullopt if
// cancelled mid-flight (the caller discards the whole batch on cancellation, matching
// the all-or-nothing behaviour of the legacy code).
std::optional<Domain::TriangleMesh> repair_single_mesh(
    const WinRTFunctions& fns,
    const Domain::TriangleMesh& mesh,
    const JThread::StopToken& stop_token
)
{
    ComPtr<Printing3D::IPrinting3DModel> model;
    if (HRESULT hr = winrt_activate_instance(fns, L"Windows.Graphics.Printing3D.Printing3DModel", model.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to create a WinRT Printing3DModel instance.");
    // PrusaSlicer meshes are in millimeters; Unit defaults to Meter (0) if left unset, which
    // would make the repair engine see a 1000x-too-large model.
    if (HRESULT hr = model->put_Unit(Printing3D::Printing3DModelUnit_Millimeter); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the unit of a WinRT model.");

    ComPtr<Printing3D::IPrinting3DMesh> winrt_mesh = winrt_mesh_from_triangle_mesh(fns, mesh);

    ComPtr<ABI::Windows::Foundation::Collections::IVector<Printing3D::Printing3DMesh*>> meshes;
    if (HRESULT hr = model->get_Meshes(meshes.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access the mesh collection of a WinRT model.");
    if (HRESULT hr = meshes->Append(winrt_mesh.Get()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to add a mesh to a WinRT model.");

    // Meshes is just a flat resource pool; Build is the root component that actually
    // defines what gets printed (and, in turn, what RepairAsync operates on). Without
    // wiring the mesh into it, the model has nothing reachable to repair.
    ComPtr<Printing3D::IPrinting3DComponent> build_component;
    if (HRESULT hr = winrt_activate_instance(fns, L"Windows.Graphics.Printing3D.Printing3DComponent", build_component.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to create a WinRT Printing3DComponent instance.");
    if (HRESULT hr = build_component->put_Mesh(winrt_mesh.Get()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to attach the mesh to a WinRT build component.");
    if (HRESULT hr = model->put_Build(build_component.Get()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to set the build component of a WinRT model.");

    ComPtr<ABI::Windows::Foundation::IAsyncAction> repair_async;
    if (HRESULT hr = model->RepairAsync(repair_async.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to start the Windows mesh repair operation.");
    SPDLOG_INFO("MeshRepairJob: RepairAsync started");

    const AsyncStatus status = winrt_async_await(repair_async, stop_token);
    SPDLOG_INFO("MeshRepairJob: RepairAsync finished, status={}", int(status));
    if (status == AsyncStatus::Canceled)
        return std::nullopt;
    // GetResults() also finalizes the async action (retrieves/clears its stored result),
    // which the WinRT object expects regardless of outcome; skipping it on the error path
    // left the action in a state where releasing it could crash.
    const HRESULT repair_hr = repair_async->GetResults();
    SPDLOG_INFO("MeshRepairJob: RepairAsync GetResults() hr=0x{:x}", static_cast<unsigned long>(repair_hr));
    if (status != AsyncStatus::Completed || FAILED(repair_hr)) {
        std::stringstream message;
        message << "Windows mesh repair failed (status " << int(status) << ", hr 0x" << std::hex << repair_hr << ").";
        throw Slic3r::RuntimeError(message.str());
    }

    meshes.Reset();
    if (HRESULT hr = model->get_Meshes(meshes.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to access the repaired WinRT mesh collection.");
    unsigned mesh_count = 0;
    meshes->get_Size(&mesh_count);
    SPDLOG_INFO("MeshRepairJob: repaired mesh collection has {} mesh(es)", mesh_count);
    if (mesh_count != 1)
        throw Slic3r::RuntimeError("Windows repair returned an unexpected number of meshes.");

    ComPtr<Printing3D::IPrinting3DMesh> repaired_mesh;
    if (HRESULT hr = meshes->GetAt(0, repaired_mesh.GetAddressOf()); FAILED(hr))
        throw Slic3r::RuntimeError("Failed to retrieve the repaired WinRT mesh.");

    return triangle_mesh_from_winrt_mesh(repaired_mesh.Get());
}

} // namespace

RefMeshes repair_meshes(
    JThread::StopToken stop_token,
    Platform::JobManager::ProgressTracker progress,
    RefMeshes meshes
)
{
    if (meshes.empty())
        return meshes;

    WinRTScope winrt_scope;
    const WinRTFunctions& fns = winrt_functions();

    RefMeshes result;
    result.reserve(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) {
        if (stop_token.stop_requested())
            return meshes;

        progress.set(Domain::Percentage{double(i) / double(meshes.size())});

        std::optional<Domain::TriangleMesh> repaired =
            repair_single_mesh(fns, meshes[i].second, stop_token);
        if (!repaired)
            return meshes; // cancelled mid-repair: discard everything, same as the legacy behaviour

        result.emplace_back(meshes[i].first, std::move(*repaired));
    }

    progress.set(Domain::Percentage{1.0});
    return result;
}

} // namespace Slic3r::Biz::Scene

#else // SLIC3R_ENABLE_WIN10_MESH_REPAIR

namespace Slic3r::Biz::Scene {

RefMeshes repair_meshes(
    JThread::StopToken /*stop_token*/,
    Platform::JobManager::ProgressTracker /*progress*/,
    RefMeshes meshes
)
{
    // Built without SLIC3R_ENABLE_WIN10_MESH_REPAIR (the default on every platform,
    // including Windows, unless a build explicitly opts in via that CMake option).
    // Every call path into this function (SceneInteractor::repair_selected_object()
    // and can_repair_selected_object(), and the menu commands that reach them) is
    // gated on the same option, so reaching this body is a bug, not a reachable
    // runtime condition: something is calling the mesh-repair job on a build that
    // was never meant to expose it.
    PANIC("repair_meshes() called without SLIC3R_ENABLE_WIN10_MESH_REPAIR");
    return meshes;
}

} // namespace Slic3r::Biz::Scene

#endif // SLIC3R_ENABLE_WIN10_MESH_REPAIR
