#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem/path.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/HwBundleEvaluator.hpp"

#include "Slic3r/Biz/Preset/IO/HwConfigConnectJson.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"

#include "ConnectJsonTestUtils.hpp"

namespace fs = boost::filesystem;
using namespace Slic3r;
struct HwBundleEvaluatorFixture
{

    HwBundleEvaluatorFixture()
    {
        set_data_dir(Tests::get_datadir().string());

        auto paths = Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir());
        paths.local_bundle_path =
            (test_data_dir / "presets").string();
        project_interactor.preset_interactor().load_preset_bundle(paths);
    }

    std::string read_json(const std::string& file_name) const
    {
        auto file_path = test_data_dir / file_name;
        std::ifstream f{file_path.string()};
        if (!f.is_open()) {
            FAIL("Cannot open file " + file_path.string());
        }
        std::string content ((std::istreambuf_iterator(f)), std::istreambuf_iterator<char>());
        f.close();
        return content;
    }

    Domain::Workbench workbench;

    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Biz::Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
    fs::path test_data_dir{Tests::get_datadir() / "hw_config_connect"};
};


TEST_CASE_METHOD(HwBundleEvaluatorFixture, "Evaluate Connect HW Config Tests", "[preset]")
{
    Biz::Preset::HwBundleEvaluator hw_bundle_evaluator{workbench.preset_bundle()};

    for (const auto& [test_input, expected] :
         Biz::Preset::ConnectJsonTestUtils::create_test_desc(true))
    {
        INFO("Printer file: " << test_input.file_name);
        auto json = read_json(test_input.file_name);
        auto hw_config_result = Biz::PhysicalPrinter::hw_config_from_connect_json(json);

        REQUIRE(hw_config_result.has_value());

        auto hw_config_eval_result = hw_bundle_evaluator.evaluate_config(hw_config_result.value());
        REQUIRE(hw_config_eval_result.has_value());
        const auto& hw_config = hw_config_eval_result.value();
        REQUIRE(hw_config.model == expected.model);
        REQUIRE(hw_config.technology == expected.technology);
        REQUIRE(hw_config.tool_count == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tools.size());
        for (size_t i = 0; i < hw_config.tools.size(); ++i) {
            INFO("tool: " << i);
            const auto& exp = expected.tools.at(i);
            const auto& t = hw_config.tools.at(i);

            REQUIRE(t.features == exp.features);
        }
    }
}