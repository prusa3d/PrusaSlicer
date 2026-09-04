#if SLIC3R_ENABLE_FORMAT_STEP

#include "Slic3r/Biz/Format/STEP.hpp"
#include "OCCTWrapper.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <string>
#include <functional>

#ifdef _WIN32
    #include<windows.h>
#else
    #include <dlfcn.h>
#endif



namespace Slic3r::Biz {

#if __APPLE__
extern "C" bool load_step_internal(const char *path, OCCTResult* res, std::optional<std::pair<double, double>> deflections /*= std::nullopt*/);
#endif

// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
LoadStepFn get_load_step_fn()
{
    static LoadStepFn load_step_fn = nullptr;

#ifndef __APPLE__
    constexpr const char* fn_name = "load_step_internal";
#endif

    if (!load_step_fn) {
        auto libpath = boost::dll::program_location().parent_path();
#ifdef _WIN32
        libpath /= "OCCTWrapper.dll";
        HMODULE module = LoadLibraryW(libpath.wstring().c_str());
        if (module == NULL)
            throw Slic3r::RuntimeError("Cannot load OCCTWrapper.dll");

        try {
            FARPROC farproc = GetProcAddress(module, fn_name);
            if (! farproc) {
                DWORD ec = GetLastError();
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.dll: ") + fn_name
                                           + "\n\nError code: " + std::to_string(ec));
            }
            load_step_fn = reinterpret_cast<LoadStepFn>(farproc);
        } catch (const Slic3r::RuntimeError&) {
            FreeLibrary(module);
            throw;
        }
#elif __APPLE__
        load_step_fn = &load_step_internal;
#else
        libpath /= "OCCTWrapper.so";
        void *plugin_ptr = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);

        if (plugin_ptr) {
            load_step_fn = reinterpret_cast<LoadStepFn>(dlsym(plugin_ptr, fn_name));
            if (!load_step_fn) {
                dlclose(plugin_ptr);
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.so: ") + fn_name
                                           + "\n\n" + dlerror());
            }
        } else {
            throw Slic3r::RuntimeError(std::string("Cannot load OCCTWrapper.so:\n\n") + dlerror());
        }
#endif
    }

    return load_step_fn;
}

tl::expected<Domain::Model, std::string> load_step(
    const std::string& path,
    std::optional<std::pair<double, double>> deflections)
{
    OCCTResult occt_object;

    LoadStepFn load_step_fn;
    try {
        load_step_fn = get_load_step_fn();
    } catch (Slic3r::RuntimeError& ex) {
        return tl::make_unexpected(ex.what());
    }

    if (! load_step_fn(path.c_str(), &occt_object, deflections)) {
        return tl::make_unexpected(occt_object.error_str);
    }

    ASSERT(! occt_object.volumes.empty());
    
    ASSERT(boost::algorithm::iends_with(occt_object.object_name, ".stp")
        || boost::algorithm::iends_with(occt_object.object_name, ".step"));
    occt_object.object_name.erase(occt_object.object_name.find("."));
    ASSERT(! occt_object.object_name.empty());

    Domain::Model model;
    Domain::ModelObject* new_object = model.add_object();
    new_object->input_file = path;
    if (new_object->volumes.size() == 1 && ! occt_object.volumes.front().volume_name.empty())
        new_object->name = new_object->volumes.front()->name;
    else
        new_object->name = occt_object.object_name;

    using Biz::Algorithms::TriangleMesh::construct;
    for (size_t i = 0; i < occt_object.volumes.size(); ++i) {
        Domain::TriangleMesh triangle_mesh{construct(std::move(occt_object.volumes[i].facets))};
        Domain::ModelVolume* new_volume = Algorithms::ModelObject::add_volume(new_object, std::move(triangle_mesh));

        new_volume->name = occt_object.volumes[i].volume_name.empty()
                       ? std::string("Part") + std::to_string(i + 1)
                       : occt_object.volumes[i].volume_name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int)model.objects.size() - 1;
        new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    }

    return model;
}

} // namespace Slic3r::Biz

#endif // SLIC3R_ENABLE_FORMAT_STEP
