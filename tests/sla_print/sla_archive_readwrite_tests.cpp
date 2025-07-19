#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Format/SLAArchiveFormatRegistry.hpp"
#include "libslic3r/Format/SLAArchiveWriter.hpp"
#include "libslic3r/Format/SLAArchiveReader.hpp"
#include "libslic3r/FileReader.hpp"

#include <boost/filesystem.hpp>

using namespace Slic3r;

TEST_CASE("Archive export test", "[sla_archives]") {
    auto registry = registered_sla_archives();

    for (const char * pname : {"20mm_cube", "extruder_idler"})
    for (const ArchiveEntry &entry : registry) {
        INFO(std::string("Testing archive type: ") + entry.id + " -- writing...");
        SLAPrint print;
        SLAFullPrintConfig fullcfg;

        auto m = FileReader::load_model(TEST_DATA_DIR PATH_SEPARATOR + std::string(pname) + ".obj");

        fullcfg.printer_technology.setInt(ptSLA); // FIXME this should be ensured
        fullcfg.set("sla_archive_format", entry.id);
        fullcfg.set("supports_enable", false);
        fullcfg.set("pad_enable", false);

        DynamicPrintConfig cfg;
        cfg.apply(fullcfg);

        print.set_status_callback([](const PrintBase::SlicingStatus&) {});
        print.apply(m, cfg);
        print.process();

        ThumbnailsList thumbnails;
        auto outputfname = std::string("output_") + pname + "." + entry.ext;

        print.export_print(outputfname, thumbnails, pname);

        // Not much can be checked about the archives...
        REQUIRE(boost::filesystem::exists(outputfname));

        double vol_written = m.mesh().volume();

        if (entry.rdfactoryfn) {
            INFO(std::string("Testing archive type: ") + entry.id + " -- reading back...");
            indexed_triangle_set its;
            DynamicPrintConfig cfg;

            try {
                // Leave format_id deliberetaly empty, guessing should always
                // work here.
                import_sla_archive(outputfname, "", its, cfg);
            } catch (...) {
                REQUIRE(false);
            }

            // its_write_obj(its, (outputfname + ".obj").c_str());

            REQUIRE(!cfg.empty());
            REQUIRE(!its.empty());

            double vol_read = its_volume(its);
            double rel_err  = std::abs(vol_written - vol_read) / vol_written;
            REQUIRE(rel_err < 0.1);
        }
    }
}
