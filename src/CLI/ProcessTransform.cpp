#include <cstdio>
#include <string>
#include <cstring>
#include <iostream>
#include <math.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include "libslic3r/libslic3r.h"
#if !SLIC3R_OPENGL_ES
#include <boost/algorithm/string/split.hpp>
#endif // !SLIC3R_OPENGL_ES
#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelProcessing.hpp"
#include "libslic3r/CutUtils.hpp"
#include <arrange-wrapper/ModelArrange.hpp>
#include "libslic3r/MultipleBeds.hpp"

#include "CLI.hpp"

namespace Slic3r::CLI {

bool process_transform(Data& cli, const DynamicPrintConfig& print_config, std::vector<Model>& models)
{
    DynamicPrintConfig& transform = cli.transform_config;
    DynamicPrintConfig& actions   = cli.actions_config;

    const Vec2crd gap{ s_multiple_beds.get_bed_gap() };
    arr2::ArrangeBed bed = arr2::to_arrange_bed(get_bed_shape(print_config), gap);
    arr2::ArrangeSettings arrange_cfg;
    if (transform.has("merge") || transform.has("duplicate"))
        arrange_cfg.set_distance_from_objects(min_object_distance(print_config));

    if (transform.has("merge")) {
        Model m;

        for (auto& model : models)
            for (ModelObject* o : model.objects)
                m.add_object(*o);
        // Rearrange instances unless --dont-arrange is supplied
        if (!transform.has("dont_arrange") && !transform.opt_bool("dont_arrange")) {
            m.add_default_instances();
            if (actions.has("slice"))
                arrange_objects(m, bed, arrange_cfg);
            else
                arrange_objects(m, arr2::InfiniteBed{}, arrange_cfg);//??????
        }
        models.clear();
        models.emplace_back(std::move(m));
    }

    if (transform.has("duplicate")) {
        for (auto& model : models) {
            const bool all_objects_have_instances = std::none_of(
                model.objects.begin(), model.objects.end(),
                [](ModelObject* o) { return o->instances.empty(); }
            );

            int dups = transform.opt_int("duplicate");
            if (!all_objects_have_instances) model.add_default_instances();

            try {
                if (dups > 1) {
                    // if all input objects have defined position(s) apply duplication to the whole model
                    duplicate(model, size_t(dups), bed, arrange_cfg);
                }
                else {
                    arrange_objects(model, bed, arrange_cfg);
                }
            }
            catch (std::exception& ex) {
                boost::nowide::cerr << "error: " << ex.what() << std::endl;
                return false;
            }
        }
    }
    if (transform.has("duplicate_grid")) {
        std::vector<int>& ints = transform.option<ConfigOptionInts>("duplicate_grid")->values;
        const int x = ints.size() > 0 ? ints.at(0) : 1;
        const int y = ints.size() > 1 ? ints.at(1) : 1;
        const double distance = print_config.opt_float("duplicate_distance");
        for (auto& model : models)
            model.duplicate_objects_grid(x, y, (distance > 0) ? distance : 6);  // TODO: this is not the right place for setting a default
    }
            
    if (transform.has("center")) {
        for (auto& model : models) {
            model.add_default_instances();
            // this affects instances:
            model.center_instances_around_point(transform.option<ConfigOptionPoint>("center")->value);
            // this affects volumes:
            //FIXME Vojtech: Who knows why the complete model should be aligned with Z as a single rigid body?
            //model.align_to_ground();
            BoundingBoxf3 bbox;
            for (ModelObject* model_object : model.objects)
                // We are interested into the Z span only, therefore it is sufficient to measure the bounding box of the 1st instance only.
                bbox.merge(model_object->instance_bounding_box(0, false));
            for (ModelObject* model_object : model.objects)
                for (ModelInstance* model_instance : model_object->instances)
                    model_instance->set_offset(Z, model_instance->get_offset(Z) - bbox.min.z());
        }
    }

    if (transform.has("align_xy")) {
        const Vec2d& p = transform.option<ConfigOptionPoint>("align_xy")->value;
        for (auto& model : models) {
            BoundingBoxf3 bb = model.bounding_box_exact();
            // this affects volumes:
            model.translate(-(bb.min.x() - p.x()), -(bb.min.y() - p.y()), -bb.min.z());
        }
    }

    if (transform.has("rotate")) {
        for (auto& model : models)
            for (auto& o : model.objects)
                // this affects volumes:
                o->rotate(Geometry::deg2rad(transform.opt_float("rotate")), Z);
    }
    if (transform.has("rotate_x")) {
        for (auto& model : models)
            for (auto& o : model.objects)
                // this affects volumes:
                o->rotate(Geometry::deg2rad(transform.opt_float("rotate_x")), X);
    }
    if (transform.has("rotate_y")) {
        for (auto& model : models)
            for (auto& o : model.objects)
                // this affects volumes:
                o->rotate(Geometry::deg2rad(transform.opt_float("rotate_y")), Y);
    }

    if (transform.has("scale")) {
        for (auto& model : models)
            for (auto& o : model.objects)
                // this affects volumes:
                o->scale(transform.get_abs_value("scale", 1));
    }
    if (transform.has("scale_to_fit")) {
        const Vec3d& opt = transform.opt<ConfigOptionPoint3>("scale_to_fit")->value;
        if (opt.x() <= 0 || opt.y() <= 0 || opt.z() <= 0) {
            boost::nowide::cerr << "--scale-to-fit requires a positive volume" << std::endl;
            return false;
        }
        for (auto& model : models)
            for (auto& o : model.objects)
                // this affects volumes:
                o->scale_to_fit(opt);
    }

    if (transform.has("cut")) {
        std::vector<Model> new_models;
        const Vec3d plane_center = transform.opt_float("cut") * Vec3d::UnitZ();
        for (auto& model : models) {
            Model new_model;
            model.translate(0, 0, -model.bounding_box_exact().min.z());  // align to z = 0
            size_t num_objects = model.objects.size();
            for (size_t i = 0; i < num_objects; ++i) {
                ModelObject* mo = model.objects.front();
                const Vec3d cut_center_offset = plane_center - mo->instances[0]->get_offset();
                Cut cut(mo, 0, Geometry::translation_transform(cut_center_offset),
                    ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::PlaceOnCutUpper);
                auto cut_objects = cut.perform_with_plane();
                for (ModelObject* obj : cut_objects)
                    new_model.add_object(*obj);
                model.delete_object(size_t(0));
            }
            new_models.push_back(new_model);
        }

        // TODO: copy less stuff around using pointers
        models = new_models;

        if (actions.empty()) {
            // cutting transformations are setting an "export" action.
            actions.set_key_value("export_stl", new ConfigOptionBool(true));
        }
    }

    if (transform.has("split")) {
        for (Model& model : models) {
            size_t num_objects = model.objects.size();
            for (size_t i = 0; i < num_objects; ++i) {
                ModelObjectPtrs new_objects;
                ModelProcessing::split(model.objects.front(), &new_objects);
                model.delete_object(size_t(0));
            }
        }
    }

    // All transforms have been dealt with. Now ensure that the objects are on bed.
    // (Unless the user said otherwise.)
    if (!transform.has("ensure_on_bed") || transform.opt_bool("ensure_on_bed"))
        for (auto& model : models)
            for (auto& o : model.objects)
                o->ensure_on_bed();

    return true;
}

}