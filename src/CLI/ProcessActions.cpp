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
#include "libslic3r/GCode/PostProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include <arrange-wrapper/ModelArrange.hpp>
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Format/AMF.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Format/SL1.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/BuildVolume.hpp"

#include "CLI/CLI.hpp"
#include "CLI/ProfilesSharingUtils.hpp"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace Slic3r::CLI {

static bool has_profile_sharing_action(const Data& cli)
{
    return cli.actions_config.has("query-printer-models") || cli.actions_config.has("query-print-filament-profiles");
}

bool has_full_config_from_profiles(const Data& cli)
{
    const DynamicPrintConfig& input = cli.input_config;
    return  !has_profile_sharing_action(cli) &&
           (input.has("print-profile") && !input.opt_string("print-profile").empty() ||
            input.has("material-profile") && !input.option<ConfigOptionStrings>("material-profile")->values.empty() ||
            input.has("printer-profile") && !input.opt_string("printer-profile").empty());
}

bool process_profiles_sharing(const Data& cli)
{
    if (!has_profile_sharing_action(cli))
        return false;

    std::string ret;

    if (cli.actions_config.has("query-printer-models")) {
        ret = Slic3r::get_json_printer_models(get_printer_technology(cli.overrides_config));
    }
    else if (cli.actions_config.has("query-print-filament-profiles")) {
        if (cli.input_config.has("printer-profile") && !cli.input_config.opt_string("printer-profile").empty()) {
            const std::string printer_profile = cli.input_config.opt_string("printer-profile");
            ret = Slic3r::get_json_print_filament_profiles(printer_profile);
            if (ret.empty()) {
                boost::nowide::cerr << "query-print-filament-profiles error: Printer profile '" << printer_profile <<
                                        "' wasn't found among installed printers." << std::endl <<
                                        "Or the request can be wrong." << std::endl;
                return true;
            }
        }
        else {
            boost::nowide::cerr << "query-print-filament-profiles error: This action requires set 'printer-profile' option" << std::endl;
            return true;
        }
    }

    if (ret.empty()) {
        boost::nowide::cerr << "Wrong request" << std::endl;
        return true;
    }

    // use --output when available

    if (cli.misc_config.has("output")) {
        std::string cmdline_param = cli.misc_config.opt_string("output");
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        boost::filesystem::path proposed_path = boost::filesystem::path(Slic3r::resources_dir()) / "out.json";
        if (boost::filesystem::is_directory(cmdline_path))
            proposed_path = (cmdline_path / proposed_path.filename());
        else if (cmdline_path.extension().empty())
            proposed_path = cmdline_path.replace_extension("json");
        else
            proposed_path = cmdline_path;
        const std::string file = proposed_path.string();

        boost::nowide::ofstream c;
        c.open(file, std::ios::out | std::ios::trunc);
        c << ret << std::endl;
        c.close();

        boost::nowide::cout << "Output for your request is written into " << file << std::endl;
    }
    else 
        printf("%s", ret.c_str());

    return true;
}

namespace IO {
    enum ExportFormat : int {
        OBJ,
        STL,
        // SVG, 
        TMF,
        Gcode
    };
}

static std::string output_filepath(const Model& model, IO::ExportFormat format, const std::string& cmdline_param)
{
    std::string ext;
    switch (format) {
    case IO::OBJ: ext = ".obj"; break;
    case IO::STL: ext = ".stl"; break;
    case IO::TMF: ext = ".3mf"; break;
    default: assert(false); break;
    };
    auto proposed_path = boost::filesystem::path(model.propose_export_file_name_and_path(ext));
    // use --output when available
    if (!cmdline_param.empty()) {
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        if (boost::filesystem::is_directory(cmdline_path))
            proposed_path = cmdline_path / proposed_path.filename();
        else
            proposed_path = cmdline_path;
    }
    return proposed_path.string();
}

static bool export_models(std::vector<Model>& models, IO::ExportFormat format, const std::string& cmdline_param)
{
    for (Model& model : models) {
        const std::string path = output_filepath(model, format, cmdline_param);
        bool success = false;
        switch (format) {
        case IO::OBJ: success = Slic3r::store_obj(path.c_str(), &model);          break;
        case IO::STL: success = Slic3r::store_stl(path.c_str(), &model, true);    break;
        case IO::TMF: success = Slic3r::store_3mf(path.c_str(), &model, nullptr, false); break;
        default: assert(false); break;
        }
        if (success)
            std::cout << "File exported to " << path << std::endl;
        else {
            std::cerr << "File export to " << path << " failed" << std::endl;
            return false;
        }
    }
    return true;
}


static ThumbnailData resize_and_crop(const std::vector<unsigned char>& data, int width, int height, int width_new, int height_new) {
    ThumbnailData th;

    float scale_x = float(width_new) / width;
    float scale_y = float(height_new) / height;
    float scale = std::max(scale_x, scale_y);  // Choose the larger scale to fill the box
    int resized_width = int(width * scale);
    int resized_height = int(height * scale);

    std::vector<unsigned char> resized_rgba(resized_width * resized_height * 4);
    stbir_resize_uint8_linear(data.data(), width, height, 4 * width,
                              resized_rgba.data(), resized_width, resized_height, 4 * resized_width,
                              STBIR_RGBA);

    th.set(width_new, height_new);
    int crop_x = (resized_width - width_new) / 2;
    int crop_y = (resized_height - height_new) / 2;

    for (int y = 0; y < height_new; ++y) {
        std::memcpy(th.pixels.data() + y * width_new * 4, 
                    resized_rgba.data() + ((y + crop_y) * resized_width + crop_x) * 4, 
                    width_new * 4);
    }
    return th;
}


static std::function<ThumbnailsList(const ThumbnailsParams&)> get_thumbnail_generator_cli(const std::string& filename)
{
    if (boost::iends_with(filename, ".3mf")) {
        return [filename](const ThumbnailsParams& params) {
            ThumbnailsList list_out;

            mz_zip_archive archive;
            mz_zip_zero_struct(&archive);

            if (!open_zip_reader(&archive, filename))
                return list_out;
            mz_uint num_entries = mz_zip_reader_get_num_files(&archive);
            mz_zip_archive_file_stat stat;

            int index = mz_zip_reader_locate_file(&archive, "Metadata/thumbnail.png", nullptr, 0);
            if (index < 0 || !mz_zip_reader_file_stat(&archive, index, &stat))
                return list_out;
            std::string buffer;
            buffer.resize(int(stat.m_uncomp_size));
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0)
                return list_out;
            close_zip_reader(&archive);

            std::vector<unsigned char> data;
            unsigned width = 0;
            unsigned height = 0;
            png::decode_png(buffer, data, width, height);

            {
                // Flip the image vertically so it matches the convention in Thumbnails generator.
                const int row_size = width * 4; // Each pixel is 4 bytes (RGBA)
                std::vector<unsigned char> temp_row(row_size);
                for (int i = 0; i < height / 2; ++i) {
                    unsigned char* top_row = &data[i * row_size];
                    unsigned char* bottom_row = &data[(height - i - 1) * row_size];
                    std::copy(bottom_row, bottom_row + row_size, temp_row.begin());
                    std::copy(top_row, top_row + row_size, bottom_row);
                    std::copy(temp_row.begin(), temp_row.end(), top_row);
                }
            }

            for (const Vec2d& size : params.sizes) {
                Point isize(size);
                list_out.push_back(resize_and_crop(data, width, height, isize.x(), isize.y()));
            }
            return list_out;
        };
    }

    return [](const ThumbnailsParams&) ->ThumbnailsList { return {}; };
}

static void update_instances_outside_state(Model& model, const DynamicPrintConfig& config)
{
    Pointfs bed_shape = dynamic_cast<const ConfigOptionPoints*>(config.option("bed_shape"))->values;
    BuildVolume build_volume(bed_shape, config.opt_float("max_print_height"));
    s_multiple_beds.update_build_volume(BoundingBoxf(bed_shape));
    model.update_print_volume_state(build_volume);
}

bool process_actions(Data& cli, const DynamicPrintConfig& print_config, std::vector<Model>& models)
{
    DynamicPrintConfig& actions     = cli.actions_config;
    DynamicPrintConfig& transform   = cli.transform_config;

    // doesn't need any aditional input 

    if (actions.has("help")) {
        print_help();
    }
    if (actions.has("help_fff")) {
        print_help(true, ptFFF);
    }
    if (actions.has("help_sla")) {
        print_help(true, ptSLA);
    }

    if (actions.has("info")) {
        if (models.empty()) {
            boost::nowide::cerr << "error: cannot show info for empty models." << std::endl;
            return 1;
        }
        // --info works on unrepaired model
        for (Model& model : models) {
            model.add_default_instances();
            model.print_info();
        }
    }

    if (actions.has("save")) {
        //FIXME check for mixing the FFF / SLA parameters.
        // or better save fff_print_config vs. sla_print_config
        print_config.save(actions.opt_string("save"));
    }

    if (models.empty() && (actions.has("export_stl") || actions.has("export_obj") || actions.has("export_3mf"))) {
        boost::nowide::cerr << "error: cannot export empty models." << std::endl;
        return 1;
    }

    const std::string output = cli.misc_config.has("output") ? cli.misc_config.opt_string("output") : "";

    if (actions.has("export_stl")) {
        for (auto& model : models)
            model.add_default_instances();
        if (!export_models(models, IO::STL, output))
            return 1;
    }
    if (actions.has("export_obj")) {
        for (auto& model : models)
            model.add_default_instances();
        if (!export_models(models, IO::OBJ, output))
            return 1;
    }
    if (actions.has("export_3mf")) {
        if (!export_models(models, IO::TMF, output))
            return 1;
    }

    if (actions.has("slice") || actions.has("export_gcode") || actions.has("export_sla")) {
        PrinterTechnology       printer_technology = Preset::printer_technology(print_config);
        if (actions.has("export_gcode") && printer_technology == ptSLA) {
            boost::nowide::cerr << "error: cannot export G-code for an FFF configuration" << std::endl;
            return 1;
        }
        else if (actions.has("export_sla") && printer_technology == ptFFF) {
            boost::nowide::cerr << "error: cannot export SLA slices for a SLA configuration" << std::endl;
            return 1;
        }

        const Vec2crd           gap{ s_multiple_beds.get_bed_gap() };
        arr2::ArrangeBed        bed = arr2::to_arrange_bed(get_bed_shape(print_config), gap);
        arr2::ArrangeSettings   arrange_cfg;
        arrange_cfg.set_distance_from_objects(min_object_distance(print_config));

        for (Model& model : models) {
            // If all objects have defined instances, their relative positions will be
            // honored when printing (they will be only centered, unless --dont-arrange
            // is supplied); if any object has no instances, it will get a default one
            // and all instances will be rearranged (unless --dont-arrange is supplied).
            if (!transform.has("dont_arrange") || !transform.opt_bool("dont_arrange")) {
                if (transform.has("center")) {
                    Vec2d c = transform.option<ConfigOptionPoint>("center")->value;
                    arrange_objects(model, arr2::InfiniteBed{ scaled(c) }, arrange_cfg);
                }
                else
                    arrange_objects(model, bed, arrange_cfg);
            }

            Print       fff_print;
            SLAPrint    sla_print;
            sla_print.set_status_callback( [](const PrintBase::SlicingStatus& s) {
                if (s.percent >= 0) { // FIXME: is this sufficient?
                    printf("%3d%s %s\n", s.percent, "% =>", s.text.c_str());
                    std::fflush(stdout);
                }
            });

            PrintBase* print = (printer_technology == ptFFF) ? static_cast<PrintBase*>(&fff_print) : static_cast<PrintBase*>(&sla_print);
            if (printer_technology == ptFFF) {
                for (auto* mo : model.objects)
                    fff_print.auto_assign_extruders(mo);
            }

            update_instances_outside_state(model, print_config);
            MultipleBedsUtils::with_single_bed_model_fff(model, 0, [&print, &model, &print_config]()
            {
                print->apply(model, print_config);
            });

            std::string err = print->validate();
            if (!err.empty()) {
                boost::nowide::cerr << err << std::endl;
                return 1;
            }

            std::string outfile = output;

            if (print->empty())
                boost::nowide::cout << "Nothing to print for " << outfile << " . Either the print is empty or no object is fully inside the print volume." << std::endl;
            else
                try {
                std::string outfile_final;
                print->process();
                if (printer_technology == ptFFF) {
                    // The outfile is processed by a PlaceholderParser.
                    const std::string input_file = fff_print.model().objects.empty() ? "" : fff_print.model().objects.front()->input_file;
                    outfile = fff_print.export_gcode(outfile, nullptr, get_thumbnail_generator_cli(input_file));
                    outfile_final = fff_print.print_statistics().finalize_output_path(outfile);
                }
                else {
                    outfile = sla_print.output_filepath(outfile);
                    // We need to finalize the filename beforehand because the export function sets the filename inside the zip metadata
                    outfile_final = sla_print.print_statistics().finalize_output_path(outfile);
                    sla_print.export_print(outfile_final);
                }
                if (outfile != outfile_final) {
                    if (Slic3r::rename_file(outfile, outfile_final)) {
                        boost::nowide::cerr << "Renaming file " << outfile << " to " << outfile_final << " failed" << std::endl;
                        return false;
                    }
                    outfile = outfile_final;
                }
                // Run the post-processing scripts if defined.
                run_post_process_scripts(outfile, fff_print.full_print_config());
                boost::nowide::cout << "Slicing result exported to " << outfile << std::endl;
            }
            catch (const std::exception& ex) {
                boost::nowide::cerr << ex.what() << std::endl;
                return false;
            }

        }
    }

    return true;
}

}