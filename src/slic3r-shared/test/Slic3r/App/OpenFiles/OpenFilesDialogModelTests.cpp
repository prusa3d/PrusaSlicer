#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/OpenFiles/OpenFilesDialogModel.hpp"

using namespace Slic3r::App;
using namespace Slic3r::Biz::FileOpen;

namespace {

ArchiveFile file(const std::string& name, bool is_project, std::optional<SkipReason> skip_reason = std::nullopt)
{
    ArchiveFile item;
    item.archive     = "models.zip";
    item.name        = name;
    item.is_project  = is_project;
    item.skip_reason = skip_reason;
    if (!skip_reason) {
        item.path = "extracted/" + name;
    }
    return item;
}

std::vector<ArchiveFile> archive_files()
{
    return {
        file("project.3mf", true),
        file("part.stl", false),
        file("other.obj", false),
        file("evil.stl", false, SkipReason::UnsafePath),
    };
}

} // namespace

TEST_CASE("OpenFilesDialogModel defaults", "[OpenFilesDialog]")
{
    const OpenFilesDialogModel model(archive_files(), {});
    CHECK(model.selected(0));
    CHECK(model.as_project(0));
    CHECK(model.selected(1));
    CHECK_FALSE(model.as_project(1));
    CHECK(model.selected(2));
    CHECK_FALSE(model.selectable(3));
    CHECK_FALSE(model.selected(3));
    CHECK(model.selected_count() == 3);
    CHECK_FALSE(model.models_to_new_project());

    CHECK(OpenFilesDialogModel(archive_files(), {.new_project = true}).models_to_new_project());
}

TEST_CASE("OpenFilesDialogModel as_geometry preselects projects as models", "[OpenFilesDialog]")
{
    OpenFilesDialogModel model(archive_files(), {.as_geometry = true});
    CHECK(model.selected(0));
    CHECK_FALSE(model.as_project(0));
    model.set_as_project(0, true);
    CHECK(model.as_project(0));
}

TEST_CASE("OpenFilesDialogModel selection", "[OpenFilesDialog]")
{
    OpenFilesDialogModel model(archive_files(), {});
    model.select_all(false);
    CHECK(model.selected_count() == 0);
    model.select_all(true);
    CHECK(model.selected_count() == 3);
    model.set_selected(3, true);
    CHECK_FALSE(model.selected(3));
    model.set_as_project(1, true);
    CHECK_FALSE(model.as_project(1));
}

TEST_CASE("OpenFilesDialogModel imports_models", "[OpenFilesDialog]")
{
    OpenFilesDialogModel model(archive_files(), {});
    CHECK(model.imports_models());
    model.set_selected(1, false);
    model.set_selected(2, false);
    CHECK_FALSE(model.imports_models());
    model.set_as_project(0, false);
    CHECK(model.imports_models());
    model.set_selected(0, false);
    CHECK_FALSE(model.imports_models());
}

TEST_CASE("OpenFilesDialogModel to_selection", "[OpenFilesDialog]")
{
    OpenFilesDialogModel model(archive_files(), {});

    SECTION("unchanged") {
        const ArchiveSelection selection = model.to_selection();
        CHECK(selection.projects == std::vector<boost::filesystem::path>{"extracted/project.3mf"});
        CHECK(selection.models == std::vector<boost::filesystem::path>{"extracted/part.stl", "extracted/other.obj"});
        CHECK_FALSE(selection.models_to_new_project);
    }
    SECTION("edited") {
        model.set_as_project(0, false);
        model.set_selected(2, false);
        model.set_models_to_new_project(true);
        const ArchiveSelection selection = model.to_selection();
        CHECK(selection.projects.empty());
        CHECK(selection.models == std::vector<boost::filesystem::path>{"extracted/project.3mf", "extracted/part.stl"});
        CHECK(selection.models_to_new_project);
    }
}

TEST_CASE("format_file_size", "[OpenFilesDialog]")
{
    CHECK(format_file_size(0) == "0 B");
    CHECK(format_file_size(1023) == "1023 B");
    CHECK(format_file_size(1024) == "1.0 KB");
    CHECK(format_file_size(1536) == "1.5 KB");
    CHECK(format_file_size(5ull * 1024 * 1024) == "5.0 MB");
    CHECK(format_file_size(3ull * 1024 * 1024 * 1024) == "3.0 GB");
}
