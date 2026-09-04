#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <Slic3r/Assert.hpp>
#include <Slic3r/Domain/Workbench.hpp>
#include <Slic3r/Domain/Project.hpp>
#include <Slic3r/Domain/Bed.hpp>
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Biz/UserAccount/ConnectUtils.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/Scene/BedFactory.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/Utils/SetDiff.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "Slic3r/Directories.hpp"

#include <Slic3r/Biz/I18N/I18N.hpp> // translations
#include <boost/filesystem/path.hpp>
#include <tracy/Tracy.hpp>

using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::Preset::SelectedPreset;
using Slic3r::Domain::Preset::SelectedPresetMetadata;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

namespace Slic3r::Biz {
Domain::SelectionId ProjectInteractor::Selection::config_container_id() const
{
    if (project_id == Domain::INVALID_ID)
        return Domain::INVALID_ID;
    auto it = project_config_container.find(project_id);
    if (it == project_config_container.end())
        return Domain::INVALID_ID;
    return it->second;
}

void ProjectInteractor::Selection::set_config_container_id(Domain::SelectionId container_id)
{
    project_config_container[project_id] = container_id;
}

const Domain::Project& ProjectInteractor::selected_project() const
{
    ASSERT(m_selection.project_id != Domain::INVALID_ID);
    return m_workbench.project(m_selection.project_id);
}

Domain::Project& ProjectInteractor::selected_project()
{
    ASSERT(m_selection.project_id != Domain::INVALID_ID);
    return m_workbench.project(m_selection.project_id);
}

bool ProjectInteractor::project_exists(size_t project_id) const {
    for (const auto& [id,_] : m_workbench.projects()) {
        if (id == project_id)
            return true;
    }
    return false;
}

const Domain::Project& ProjectInteractor::project(size_t project_id) const {
    ASSERT(project_exists(project_id));
    return m_workbench.project(project_id);
}

Domain::Project& ProjectInteractor::project(size_t project_id)
{
    ASSERT(project_exists(project_id));
    return m_workbench.project(project_id);
}

void ProjectInteractor::initialize_bed(Domain::SelectionId project_id, Domain::SelectionId config_container_id,
    Domain::BedContainer& bed_container)
{
    Domain::Project& project{ m_workbench.project(project_id) };
    Domain::ConfigContainer* config_container = project.find_config_container(config_container_id);
    DEBUG_ASSERT(config_container != nullptr);
    Domain::Bed& bed{ 
        Scene::get_or_create_bed(
            bed_container, *config_container, resources_dir(), project_id, config_container_id,
            [this](Domain::SelectionId project_id, Domain::SelectionId config_container_id) {
                return m_preset_interactor.system_preset_bed_shape(project_id, config_container_id);
            }
        )
    };
    config_container->set_bed(bed);
    m_scene_interactor.add_bed_instance(config_container_id);
}

Domain::SelectionId ProjectInteractor::new_project()
{
    return new_project_with_modification([](auto& _){});
}

Domain::SelectionId ProjectInteractor::new_project_with_modification(
    const std::function<void(Domain::Project&)>& modifier
)
{
    Domain::Project project;

    project.config_containers().emplace_back(std::make_unique<Domain::ConfigContainer>());
    auto& config_container = *project.config_containers().front();
    Domain::SelectionId project_id;
    {
        InvokeLaterBag bag;
        project_id = add_project(std::move(project), bag);
        m_preset_interactor.initialize_config_container_with_default(config_container);

        Domain::Project& added_project{m_workbench.project(project_id)};
        initialize_bed(project_id, config_container.id().id, added_project.bed_container());

        modifier(added_project);
        m_scene_interactor.prepare_added_project(project_id);
    }
    invoke_listeners<IProjectsChangedListener>([project_id](auto* l) {
        l->on_project_loaded(project_id);
    });

    return project_id;
}

tl::expected<SelectionId, std::string> ProjectInteractor::do_load_project(
    Project&& project,
    const std::optional<boost::filesystem::path>& project_file_path
)
{
    const auto report_error = [this](const std::string& description) -> tl::unexpected<std::string>
    {
        SPDLOG_ERROR(description);
        invoke_listeners<IProjectsChangedListener>([&description](IProjectsChangedListener* l)
                                                   { l->on_project_load_failed(description); });

        return tl::unexpected<std::string>{description};
    };

    if (project.config_containers().empty() && project.model().objects.empty()) {
        return report_error(Biz::_u8L("Loading file failed: The loaded project is empty."));
    }

    SelectionId project_id;
    {
        InvokeLaterBag bag;
        const SelectionId original_project_id = m_selection.project_id;
        project_id                            = this->add_project(std::move(project), bag);
        Project& added_project{m_workbench.project(project_id)};

        for (std::unique_ptr<ConfigContainer>& config_container : added_project.config_containers())
        {
            const tl::expected<void, std::string> result =
                m_preset_interactor.load_selected_preset_from_3mf(
                    project_id,
                    config_container->mutable_selected_preset()
                );
            if (!result.has_value()) {
                // clean up project state
                this->select_project(original_project_id);
                this->remove_project(project_id);

                // invoke error listener and quit
                return report_error(result.error());
            }
        }

        if (added_project.config_containers().empty()) {
            added_project.config_containers().emplace_back(std::make_unique<ConfigContainer>());
            ConfigContainer* config_container = added_project.config_containers().back().get();
            m_preset_interactor.initialize_config_container_with_default(*config_container);
        }

        for (std::unique_ptr<ConfigContainer>& config_container : added_project.config_containers())
        {
            if (config_container->bed_instances().empty()) {
                this->initialize_bed(
                    project_id,
                    config_container->id().id,
                    added_project.bed_container()
                );
            }
        }

        // Ensure bed selection is valid for the config container.
        const ConfigContainer& config_container{*added_project.config_containers().front()};
        m_scene_interactor.bed_selection().select_one(
            {config_container.id().id, config_container.bed_instances().front()->id().id},
            Scene::CameraActionOnBedSelection::CenterOnBed
        );

        this->do_select_config_container(added_project.config_containers().front()->id().id);

        m_scene_interactor.prepare_added_project(project_id);

        if (project_file_path.has_value()) {
            this->set_project_dir(project_id, project_file_path.value());
        }
    }

    invoke_listeners<IProjectsChangedListener>([project_id](auto* l)
                                               { l->on_project_loaded(project_id); });

    return project_id;
}

void ProjectInteractor::load_project(const boost::filesystem::path& file_path)
{
    auto report_error{
        [this](const std::string& description)
        {
            SPDLOG_ERROR(description);
            invoke_listeners<IProjectsChangedListener>([&description](IProjectsChangedListener* l)
                                                       { l->on_project_load_failed(description); });
        }
    };

    auto on_result{[this, file_path](Project&& project)
                   { this->do_load_project(std::move(project), file_path); }};

    auto on_error{[report_error](std::exception_ptr eptr)
                  {
                      std::string description = "Unknown error";
                      try {
                          std::rethrow_exception(eptr);
                      } catch (Loaded3MFException& e) {
                          description = fmt::format("Loading file failed: {}", e.issue.msg);
                      } catch (std::exception& e) {
                          description = fmt::format("Loading file failed: {}", e.what());
                      } catch (...) {
                      }
                      report_error(description);
                  }};

    Platform::PlatformServices::instance()
        .job_manager()
        .create_job(
            "project_load",
            // TODO: preset_bundle may change, making its copy wouldn't help
            [&preset_bundle = m_workbench.preset_bundle(), dialog_provider = m_dialog_provider](
                Biz::JThread::StopToken stop_token,
                const boost::filesystem::path file_path
            ) -> Domain::Project
            {
                return FileLoadingLogic::load_file_as_project(
                    file_path,
                    preset_bundle,
                    dialog_provider
                );
            },
            file_path
        )
        .on_result(on_result)
        .on_exception(on_error)
        .start();
}

void ProjectInteractor::load_projects(
    const std::vector<boost::filesystem::path>& file_paths,
    bool restored_projects
)
{
    struct State
    {
        std::deque<boost::filesystem::path> queue;
        std::list<boost::filesystem::path> restored;
        std::function<void()> next;
    };

    auto state = std::make_shared<State>();
    state->queue.assign(file_paths.begin(), file_paths.end());

    state->next = [this, state, restored_projects]
    {
        if (state->queue.empty()) {
            if (restored_projects) {
                for (const boost::filesystem::path& restored : state->restored) {
                    if (!boost::filesystem::remove(restored)) {
                        SPDLOG_WARN("Could not removed backed project marked for deletion");
                    }
                }
            }
            return;
        }

        const boost::filesystem::path file_path = std::move(state->queue.front());
        state->queue.pop_front();

        Platform::PlatformServices::instance()
            .job_manager()
            .create_job(
                "project_load",
                [&preset_bundle = m_workbench.preset_bundle(),
                 dialog_provider =
                     m_dialog_provider](Biz::JThread::StopToken, boost::filesystem::path file_path)
                {
                    return FileLoadingLogic::load_file_as_project(
                        file_path,
                        preset_bundle,
                        dialog_provider
                    );
                },
                file_path
            )
            .on_result(
                [this, state, restored_projects, file_path](Project project)
                {
                    if (restored_projects) {
                        auto name = project.file_name();

                        if (const auto first = name.find('_'); first != std::string::npos) {
                            if (const auto second = name.find('_', first + 1);
                                second != std::string::npos)
                            {
                                name.erase(0, second + 1);
                            }
                        }

                        name.append(Biz::_u8L("(restored)"));
                        project.set_file_name(name);

                        state->restored.push_back(file_path);
                    }

                    do_load_project(std::move(project), {});
                    state->next();
                }
            )
            .on_exception(
                [this, state](std::exception_ptr eptr)
                {
                    std::string description = "Unknown error";

                    try {
                        std::rethrow_exception(eptr);
                    } catch (Loaded3MFException& e) {
                        description = fmt::format("Loading file failed: {}", e.issue.msg);
                    } catch (std::exception& e) {
                        description = fmt::format("Loading file failed: {}", e.what());
                    } catch (...) {
                    }

                    SPDLOG_ERROR(description);
                    invoke_listeners<IProjectsChangedListener>(
                        [&description](auto* l) { l->on_project_load_failed(description); }
                    );

                    state->next();
                }
            )
            .start();
    };

    state->next();
}

tl::expected<SelectionId, std::string> ProjectInteractor::new_project_with_preset(
    const SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config_pack
)
{
    Project project;
    project.config_containers().emplace_back(std::make_unique<ConfigContainer>());
    ConfigContainer& config_container = *project.config_containers().front();

    config_container.mutable_selected_preset() = SelectedPreset::make(preset_metadata, config_pack);
    if (const ConfigPackFDM* config_pack_fdm = std::get_if<ConfigPackFDM>(&config_pack)) {
        config_container.project_settings() = config_pack_fdm->project;
    }

    return do_load_project(std::move(project));
}

namespace {
std::string get_file_name(const std::string& file_path)
{
    size_t pos_last_delimiter = file_path.find_last_of("/\\");
    size_t pos_point = file_path.find_last_of('.');
    size_t offset = pos_last_delimiter + 1;
    size_t count = pos_point - pos_last_delimiter - 1;
    return file_path.substr(offset, count);
}
using SvgFile = Domain::EmbossShape::SvgFile;
using SvgFiles = std::vector<SvgFile*>;
std::string create_unique_3mf_filepath(const std::string& file, const SvgFiles svgs)
{
    // const std::string MODEL_FOLDER = "3D/"; // copy from file 3mf.cpp
    std::string path_in_3mf = "3D/" + file + ".svg";
    size_t suffix_number = 0;
    bool is_unique = false;
    do {
        is_unique = true;
        path_in_3mf = "3D/" + file + ((suffix_number++) ? ("_" + std::to_string(suffix_number)) : "") + ".svg";
        for (SvgFile* svgfile : svgs) {
            if (svgfile->path_in_3mf.empty())
                continue;
            if (svgfile->path_in_3mf.compare(path_in_3mf) == 0) {
                is_unique = false;
                break;
            }
        }
    } while (!is_unique);
    return path_in_3mf;
}

bool set_by_local_path(SvgFile& svg, const SvgFiles& svgs)
{
    // Try to find already used svg file
    for (SvgFile* svg_ : svgs) {
        if (svg_->path_in_3mf.empty())
            continue;
        if (svg.path.compare(svg_->path) == 0) {
            svg.path_in_3mf = svg_->path_in_3mf;
            return true;
        }
    }
    return false;
}

/**
    @brief Function to secure private data before store to 3mf
    @param model    - Data(also private) to clean before publishing
    @param messager - Ability to polite ask users
**/
void publish(Domain::Model& model, IMessageDialogProvider* messager) {

    // SVG file publishing
    bool exist_new_svg = false;
    SvgFiles svgs;
    for (Domain::ModelObject* mo : model.objects) {
        for (Domain::ModelVolume* mv : mo->volumes) {
            if (!mv->emboss_shape.has_value())
                continue; // do not contain emboss shape
            if (!mv->emboss_shape->svg_file.has_value())
                continue; // do not contain svg file
            SvgFile* svg = &(*mv->emboss_shape->svg_file);
            if (svg->path_in_3mf.empty())
                exist_new_svg = true;
            svgs.push_back(svg);
        }
    }

    if (exist_new_svg && messager != nullptr) {
        std::string message_title = Biz::_u8L("SVG file obfuscate");
        std::string message_text = Biz::_u8L("Are you sure you want to store original SVGs with their local paths into the 3MF file ? \n"
            "If you hit 'NO', all SVGs in the project will not be editable any more.");
        IMessageDialogProvider::YesNoCallback callback = [&model](bool answer) {
            if (answer == false) {
                for (Domain::ModelObject* mo : model.objects) {
                    for (Domain::ModelVolume* mv : mo->volumes) {
                        if (mv->emboss_shape.has_value() &&
                            mv->emboss_shape->svg_file.has_value() &&
                            mv->emboss_shape->svg_file->path_in_3mf.empty()) {
                            mv->emboss_shape.reset(); // became regular volume
                        }
                    }
                }
            }
        };
        messager->show_yesno_dialog(message_title, message_text, callback);
    }

    for (SvgFile* svgfile : svgs) {
        if (!svgfile->path_in_3mf.empty())
            continue; // already suggested path (previous save)
        // create unique name for svgs, when local path differ
        std::string filename = "unknown";
        if (!svgfile->path.empty()) {
            if (set_by_local_path(*svgfile, svgs))
                continue;
            // check whether original filename is already in:
            filename = get_file_name(svgfile->path);
        }
        svgfile->path_in_3mf = create_unique_3mf_filepath(filename, svgs);
    }
}
}

void ProjectInteractor::save_selected_project(
    const boost::filesystem::path& file_path,
    const Store3mfParam& params
)
{
    save_project(selected_project_id(), file_path, params);
}

void ProjectInteractor::save_project(
    Domain::SelectionId project_id,
    const boost::filesystem::path& file_path,
    const Store3mfParam& params
)
{
    Domain::Project& project = m_workbench.project(project_id);
    project.increment_version();
    project.set_file_path(file_path);
    publish(project.model(), m_dialog_provider);
    store_3mf(file_path.string(), project, params);

    project.directory_storage().set_project_dir(file_path);

    invoke_listeners<IProjectsChangedListener>(
        [this, project_id](auto* l)
        {
            l->on_project_changed(project_id);
            l->on_project_saved(project_id);
        }
    );
}

void ProjectInteractor::select_project(Domain::SelectionId project_id)
{
    if (project_id != m_selection.project_id) {
        {
            InvokeLaterBag bag;
            do_select_project(project_id, bag);
        }

        if (m_selection.config_container_id() == Domain::INVALID_ID) {
            const auto& projects         = m_workbench.projects();
            const auto& config_container = projects.at(project_id).config_containers().front();
            const Domain::SelectionId first_container_id = config_container->id().id;
            do_select_config_container(first_container_id);
        } else {
            // TODO: remove this once the right side panel elements are correctly storing selected
            // config container per project
            auto container_id = m_selection.config_container_id();
            invoke_listeners<ISelectedConfigContainerChangedListener>(
                [container_id, project_id](auto* l)
                { l->on_selected_config_container_changed(project_id, container_id); }
            );
        }
    }
}

ObservableProjectList& ProjectInteractor::observable_project_list()
{
    return m_project_list;
}

Domain::SlicingId ProjectInteractor::selected_bed_slicing_id() const
{
    return {selected_project_id(), m_scene_interactor.bed_selection().last_selected_bed().instance_id};
}

void ProjectInteractor::on_selected_bed_instances_changed(Domain::SelectionId project_id, const Scene::BedSelection& selection)
{
    const Domain::BedRef last_selected_bed{selection.last_selected_bed()};
    const Domain::SelectionId container_id{last_selected_bed.config_container_id};

    if (container_id != m_selection.config_container_id())
        do_select_config_container(container_id);
}

void ProjectInteractor::set_dialog_provider(IMessageDialogProvider* dialog_provider)
{
    m_dialog_provider = dialog_provider;
}

void ProjectInteractor::on_slicing_input_changed(const Domain::BedRef& bed_instance)
{
    ZoneScoped;

    auto& project = selected_project();
    const Domain::BedInstance* instance{project.find_bed_instance_by_id(bed_instance.instance_id)};
    ASSERT(instance);
    const auto* config_container{project.find_config_container(bed_instance.config_container_id)};
    ASSERT(config_container);

    const auto& selected_preset = config_container->selected_preset();

    project.model().assert_is_valid();
    m_slicing_interactor.update_process(
        project.model(),
        project.metadata(),
        selected_preset.metadata(),
        config_container->build_print_config(),
        *instance
    );
}

void ProjectInteractor::on_slicing_input_removed(const Domain::BedRef& bed_instance)
{
    m_slicing_interactor.remove_bed(bed_instance.instance_id);
}

void ProjectInteractor::on_colors_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& /*colors*/
)
{
    auto& project = selected_project();
    const auto* config_container = project.find_config_container(config_container_id);
    if (!config_container)
        return;

    const auto& selected_preset = config_container->selected_preset();
    for (const auto& bed_instance : config_container->bed_instances()) {
        m_slicing_interactor.update_process(
            project.model(),
            project.metadata(),
            selected_preset.metadata(),
            config_container->build_print_config(),
            *bed_instance
        );
    }
}

void ProjectInteractor::on_virtual_extruders_changed(
    SelectionId project_id,
    SelectionId config_container_id
)
{
    Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return;
    }

    const ConfigContainer* config_container = project->find_config_container(config_container_id);
    if (config_container == nullptr) {
        return;
    }

    const SelectedPreset& selected_preset = config_container->selected_preset();
    for (const auto& bed_instance : config_container->bed_instances()) {
        m_slicing_interactor.update_process(
            project->model(),
            project->metadata(),
            selected_preset.metadata(),
            config_container->build_print_config(),
            *bed_instance
        );
    }
}

tl::expected<void, std::string> ProjectInteractor::apply_virtual_extruders(
    SelectionId project_id,
    SelectionId config_container_id,
    const VirtualExtruders& new_virtual_extruders,
    const std::vector<unsigned int>& removed_virtual_extruder_ids
)
{
    Project* project = m_workbench.find_project_by_id(project_id);
    if (project == nullptr) {
        return tl::make_unexpected(std::string("Project not found."));
    }

    // Validates first, so on failure the model stays untouched.
    tl::expected<void, std::string> write_result =
        m_virtual_extruder_interactor
            .set_virtual_extruders(project_id, config_container_id, new_virtual_extruders);
    if (!write_result.has_value()) {
        return write_result;
    }

    // Facet states are numbered project wide, so ids have to be checked against every group.
    std::set<unsigned int> all_defined_ids;
    unsigned int max_physical_slot_count = 0;
    for (const std::unique_ptr<ConfigContainer>& config_container : project->config_containers()) {
        for (const VirtualExtruder& definition : config_container->virtual_extruders()) {
            all_defined_ids.insert(definition.id);
        }

        if (config_container->print_technology() == PrinterTechnology::FFF) {
            max_physical_slot_count = std::max(
                max_physical_slot_count,
                static_cast<unsigned int>(
                    config_container->selected_preset().hw_config.material_slot_count()
                )
            );
        }
    }

    // Drop references to virtual extruder ids that no ConfigGroup defines.
    bool any_reference_dropped = false;
    std::map<TriangleStateType, TriangleStateType> state_remap;
    ElementRefs volume_refs_to_remap;
    for (const unsigned int removed_id : removed_virtual_extruder_ids) {
        if (all_defined_ids.contains(removed_id) || removed_id <= max_physical_slot_count) {
            continue;
        }

        const bool id_can_be_painted = static_cast<size_t>(removed_id) < TRIANGLE_STATE_TYPE_COUNT;
        if (id_can_be_painted) {
            state_remap
                .emplace(static_cast<TriangleStateType>(removed_id), TriangleStateType::NONE);
        }

        for (ModelObject* object : project->model().objects) {
            if (object == nullptr) {
                continue;
            }

            // 0 means "inherit the defaults".
            ConfigItem& object_extruder = object->object_settings.items.opt("extruder");
            if (object_extruder.get<int>() == static_cast<int>(removed_id)) {
                object_extruder.set(0);
                any_reference_dropped = true;
            }

            for (ModelVolume* volume : object->volumes) {
                if (volume == nullptr) {
                    continue;
                }

                // Disabling the override falls back to the object value.
                const std::optional<ConfigItem> volume_extruder =
                    volume->volume_settings.overrides.get("extruder");
                if (volume_extruder.has_value()
                    && volume_extruder->get<int>() == static_cast<int>(removed_id))
                {
                    volume->volume_settings.overrides.disable("extruder");
                    any_reference_dropped = true;
                }

                if (!id_can_be_painted || !volume->is_mm_painted()) {
                    continue;
                }

                const std::vector<bool>& used_states =
                    volume->mm_segmentation_facets.get_data().used_states;
                if (static_cast<std::size_t>(removed_id) >= used_states.size()
                    || !used_states[static_cast<std::size_t>(removed_id)])
                {
                    continue;
                }

                volume_refs_to_remap.emplace_back(object->id().id, 0, volume->id().id);
                any_reference_dropped = true;
            }
        }
    }

    m_scene_interactor.modify_facets_annotations(
        volume_refs_to_remap,
        Domain::FacetsAnnotationKind::MultiMaterial,
        [&state_remap](const Domain::ElementRef&, ModelVolume& volume)
        {
            Algorithms::TriangleSelector selector(volume.mesh());
            selector.deserialize(volume.mm_segmentation_facets.get_data(), false);
            selector.remap_states(state_remap);

            volume.mm_segmentation_facets.set_data(selector.serialize());
            return true;
        }
    );

    if (!any_reference_dropped) {
        return {};
    }

    // Dropping the references changed the model, so every bed has to reslice.
    for (const std::unique_ptr<ConfigContainer>& config_container : project->config_containers()) {
        const SelectedPreset& selected_preset = config_container->selected_preset();
        for (const auto& bed_instance : config_container->bed_instances()) {
            m_slicing_interactor.update_process(
                project->model(),
                project->metadata(),
                selected_preset.metadata(),
                config_container->build_print_config(),
                *bed_instance
            );
        }
    }

    return {};
}

void ProjectInteractor::do_select_project(Domain::SelectionId project_id, InvokeLaterBag& bag)
{
    m_selection.project_id = project_id;

    invoke_listeners<ISelectedProjectChangedListener>(
        [project_id](auto* l) { l->on_selected_project_changed(project_id); }
    );

    bag.add(
        [this, project_id]
        {
            invoke_listeners<ISelectedProjectChangedListener>(
                [project_id](auto* l) { l->on_selected_project_changed_final(project_id); }
            );
        }
    );
}

void ProjectInteractor::do_select_config_container(Domain::SelectionId container_id)
{
    m_selection.set_config_container_id(container_id);
    Domain::SelectionId project_id  = m_selection.project_id;
    invoke_listeners<ISelectedConfigContainerChangedListener>(
        [container_id, project_id](auto* l)
        { l->on_selected_config_container_changed(project_id, container_id); }
    );
}

Domain::SelectionId ProjectInteractor::add_project(Domain::Project&& p)
{
    InvokeLaterBag bag;
    return add_project(std::move(p), bag);
}

Domain::SelectionId ProjectInteractor::add_project(Domain::Project&& p, InvokeLaterBag& bag)
{
    auto& projects                 = m_workbench.projects();
    Domain::SelectionId project_id = m_workbench.next_project_id();
    projects.emplace(project_id, std::move(p));
    invoke_listeners<IProjectsChangedListener>(
        [project_id](auto* l) { l->on_project_added_uninitialized(project_id); }
    );
    // select project
    do_select_project(project_id, bag);
    return project_id;
}

void ProjectInteractor::remove_project(Domain::SelectionId project_id)
{
    auto& projects = m_workbench.projects();
    auto it        = projects.find(project_id);

    ASSERT(it != projects.end());

    Platform::PlatformServices::instance().job_manager().cancel_jobs_for_project(project_id);

    invoke_listeners<IProjectsChangedListener>(
        [project_id](auto* l) { l->on_project_will_be_removed(project_id); }
    );

    for (const std::unique_ptr<Domain::ConfigContainer>& config_container :
         it->second.config_containers())
    {
        for (const std::unique_ptr<Domain::BedInstance>& bed_instance :
             config_container->bed_instances())
        {
            m_slicing_interactor.remove_bed(bed_instance->id().id);
        }
    }

    it = projects.erase(it);

    invoke_listeners<IProjectsChangedListener>(
        [project_id](auto* l) { l->on_project_removed(project_id); }
    );

    // At least one project need to exist at all times
    if (projects.empty()) {
        new_project();
    } else {
        if (m_selection.project_id == project_id) {
            Domain::SelectionId next_selected_project_id = Domain::INVALID_ID;
            if (it != projects.end()) {
                next_selected_project_id = it->first;
            } else {
                next_selected_project_id = projects.begin()->first;
            }

            select_project(next_selected_project_id);
        }
    }
}

void ProjectInteractor::do_result_export_inner(const Domain::SlicingId id, PhysicalPrinter::PhysicalPrinterConfig&& print_host_config, PrintHost::PrintHostJobData&& job_data)
{
    // Find confing container with matching bed instance id.
    const Domain::ConfigContainer* config_container = nullptr;
    for (const auto& cc : m_workbench.project(id.project_id).config_containers()) {
        for (const auto& bi : cc->bed_instances()) {
            if (bi->id() == id.bed_instance_id) {
                config_container = cc.get();
                break;
            }
        }
        if (config_container != nullptr) {
            break;
        }
    }
    ASSERT(config_container);
    Domain::PrinterTechnology tech = config_container->selected_preset().hw_config.technology;
    if (tech == Domain::PrinterTechnology::FFF) {
        const std::optional<FDMResultRef> fdm_result{m_fdm_result_cache.get_result(id)};
        ASSERT(fdm_result);
        job_data.data_ptr = fdm_result.value().get().const_gcode();
        m_result_export_interactor.perform(std::move(print_host_config), std::move(job_data));
    } else if (tech == Domain::PrinterTechnology::SLA) {

        const std::optional<SLAResultRef> sla_result{m_sla_result_cache.get_result(id)};
        ASSERT(sla_result);
        job_data.data_ptr = sla_result.value().get().export_data;
        m_result_export_interactor.perform(std::move(print_host_config), std::move(job_data));

    } else {
        ASSERT(false);
    }
}

void ProjectInteractor::do_result_export(const Domain::SlicingId id, const boost::filesystem::path& dest_path)
{
    set_output_dir(id.project_id, dest_path);
    set_output_extension(id.project_id, dest_path.extension().string());
    PhysicalPrinter::PhysicalPrinterConfig config;
    config.payload = PhysicalPrinter::FileSystemExport{};
    PrintHost::PrintHostJobData data{
        std::monostate{},
        dest_path,
        PrintHost::get_export_format_from_extension(dest_path.extension().string())
    };
    do_result_export_inner(id, std::move(config), std::move(data));
}

void ProjectInteractor::do_result_upload(
    const Domain::SlicingId id,
    const std::string& filename,
    PrintHost::PrintHostAfterUploadAction post_action,
    const PhysicalPrinter::PhysicalPrinterConfig& print_host_config
)
{
    set_output_extension(id.project_id, boost::filesystem::path(filename).extension().string());
    PhysicalPrinter::PhysicalPrinterConfig config {print_host_config};
    boost::filesystem::path dest_path(filename);
    PrintHost::PrintHostJobData data{
        std::monostate{},
        dest_path,
        PrintHost::get_export_format_from_extension(dest_path.extension().string())
    };
    data.post_action = post_action;
    do_result_export_inner(id, std::move(config), std::move(data));
}

void ProjectInteractor::do_result_upload_connect(
    const Domain::SlicingId id,
    const std::string& connect_msg,
    const std::string& filename_override /* = std::string()*/
)
{
    PhysicalPrinter::PhysicalPrinterConfig config;
    config.host = Network::ServiceConfig::instance().connect_url();
    PhysicalPrinter::ConnectUpload auth;
    auth.access_token = m_user_account_interactor.access_token();
    config.payload = std::move(auth);

    std::string filename;
    std::string body_json;
    if (!UserAccount::ConnectUtils::config_from_json(connect_msg, config, filename, body_json)) {
        SPDLOG_ERROR("Upload to Connect has failed - failed to read Connect message.");
        return;
    }

    if (!filename_override.empty()) {
        filename = filename_override;
    }

    set_output_extension(id.project_id, boost::filesystem::path(filename).extension().string());
    PrintHost::PrintHostJobData data{
        std::monostate{},
        filename,
        PrintHost::get_export_format_from_extension(boost::filesystem::path(filename).extension().string())
    };
    data.request_body_json = std::move(body_json);
    do_result_export_inner(id, std::move(config), std::move(data));
}

void ProjectInteractor::on_model_downloaded(const std::vector<boost::filesystem::path>& paths, bool in_new_project)
{
    ASSERT(!paths.empty());
    boost::filesystem::path file_path = paths.front();
    std::string ext_str = file_path.extension().string();
    // file path could have locale dependent characters, do not use tolower
    bool load_as_single_project = paths.size() ==1 && (ext_str == ".3mf" || ext_str == ".3MF");
    if (in_new_project && load_as_single_project) {
        load_project(paths.front());
        return;
    }

    if (in_new_project) {
        new_project();
    }
    load_models_to_project(paths);
}

std::string ProjectInteractor::get_project_name(Domain::SelectionId project_id) const
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    return it->second.file_name();
}

std::string ProjectInteractor::get_project_save_name(Domain::SelectionId project_id) const
{
    Domain::Project* project = m_workbench.find_project_by_id(project_id);
    std::string project_name = project->file_name();
    if (project_name.empty()) {
        // if project name is empty, try to find any model_object and name the project after it
        for (Domain::ModelObject* model_object : project->model().objects) {
            if (!model_object->input_file.empty()) {
                project_name = boost::filesystem::path(model_object->input_file).stem().string();
                break;
            } else if (!model_object->name.empty()) {
                project_name = boost::filesystem::path(model_object->name).stem().string();
                break;
            }
        }
    }

    return project_name;
}

boost::filesystem::path ProjectInteractor::project_dir(Domain::SelectionId project_id, const std::string& app_config_val) const
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    return it->second.directory_storage().project_dir(app_config_val);
}

void ProjectInteractor::set_project_dir(Domain::SelectionId project_id, const boost::filesystem::path& path)
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    it->second.directory_storage().set_project_dir(path);
}

boost::filesystem::path ProjectInteractor::output_dir(Domain::SelectionId project_id, bool only_removable, const std::string& app_config_val) const
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    if (only_removable) {
        return m_removable_drive_service.get_path_on_removable_drive(it->second.directory_storage().output_dir(app_config_val));
    }
    return it->second.directory_storage().output_dir(app_config_val);
}

void ProjectInteractor::set_output_dir(Domain::SelectionId project_id, const boost::filesystem::path& path)
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    it->second.directory_storage().set_output_dir(path);
}

std::string ProjectInteractor::output_extension(Domain::SelectionId project_id, const std::string& app_config_val) const
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    return it->second.directory_storage().output_extension(app_config_val);
}

void ProjectInteractor::set_output_extension(Domain::SelectionId project_id, const std::string& extension)
{
    auto it = m_workbench.projects().find(project_id);
    ASSERT(it != m_workbench.projects().end());

    it->second.directory_storage().set_output_extension(extension);
}

void ProjectInteractor::load_models_to_project(std::vector<boost::filesystem::path> paths)
{
    const auto& proj            = m_workbench.project(selected_project_id());
    Domain::BedRef selected_bed = scene_interactor().bed_selection().last_selected_bed();
    const Domain::ConfigContainer* cc =
        proj.find_config_container(selected_bed.config_container_id);
    const Domain::BedInstance& inst = cc->find_bed_instance(selected_bed.instance_id);
    int slot_count             = cc->selected_preset().hw_config.material_slot_count();
    const Domain::ElementRefs new_instances = FileLoadingLogic::import_files_and_add_to_scene(
        paths,
        slot_count,
        scene_interactor(),
        cc->bed().center() + Biz::Algorithms::Point::to_2d(inst.transformation.get_offset()),
        m_dialog_provider
    );

    if (new_instances.empty()) {
        return;
    }

    set_project_dir(selected_project_id(), paths.front());

    arrange_interactor().arrange_added_instances(
        selected_project_id(),
        new_instances,
        selected_bed,
        UndoSnapshotType::AddObject
    );
}

Domain::SelectionId ProjectInteractor::add_config_container()
{
    auto& p = m_workbench.project(m_selection.project_id);
    p.config_containers().emplace_back(std::make_unique<Domain::ConfigContainer>());
    auto& cc = *p.config_containers().back();

    m_preset_interactor.initialize_config_container_with_selected(cc);
    auto id = cc.id().id;
    initialize_bed(m_selection.project_id, id, p.bed_container());

    select_config_container(id);
    return id;
}

Domain::SelectionId ProjectInteractor::insert_config_container(
    Domain::SelectionId project_id,
    std::unique_ptr<Domain::ConfigContainer> config_container,
    std::size_t position
)
{
    auto& p = m_workbench.project(project_id);
    ASSERT(position <= p.config_containers().size());
    auto it{p.config_containers().insert(
        p.config_containers().begin() + position,
        std::move(config_container)
    )};
    auto& cc = **it;
    auto id = cc.id().id;

    m_scene_interactor.update_beds(project_id, id);

    return id;
}

Domain::SelectionId ProjectInteractor::duplicate_config_container(Domain::SelectionId config_container_id)
{
    select_config_container(config_container_id); 
    return add_config_container();
}

void ProjectInteractor::remove_config_container(Domain::SelectionId config_container_id)
{
    auto& project = m_workbench.project(m_selection.project_id);
    auto& ccs = project.config_containers();

    ASSERT(ccs.size() > 1);

    auto it = std::find_if(ccs.begin(), ccs.end(), [config_container_id](const auto& cc_ptr) { return cc_ptr->id().id == config_container_id; });
    ASSERT(it != ccs.end());

    auto to_select_it = it;
    if (++to_select_it == ccs.end()) {
        to_select_it = ccs.begin();
    }
    select_config_container((*to_select_it)->id().id);

    // Remove all beds from container before its will be erased
    Domain::ConfigContainer* cc_ptr = it->get();
    while (!cc_ptr->bed_instances().empty()) {
        const size_t bed_id = cc_ptr->bed_instances().back().get()->id().id;
        scene_interactor().remove_bed_instance(
            {.config_container_id = config_container_id, .instance_id = bed_id},
            true
        );
    }

    it = ccs.erase(it);
    scene_interactor().on_removed_config_container(project);
}

void ProjectInteractor::select_config_container(Domain::SelectionId container_id)
{
    if (container_id != m_selection.config_container_id())
        do_select_config_container(container_id);
}

static std::set<std::size_t> get_ids(const Domain::Project::ConfigContainerList& config_containers)
{
    std::set<std::size_t> result;
    std::ranges::transform(
        config_containers,
        std::inserter(result, result.begin()),
        [](const std::unique_ptr<Domain::ConfigContainer>& config_container)
        { return config_container->id().id; }
    );
    return result;
}

static std::set<std::size_t> get_ids(const Domain::ConfigContainer::BedInstanceList& bed_instances)
{
    std::set<std::size_t> result;
    std::ranges::transform(
        bed_instances,
        std::inserter(result, result.begin()),
        [](const std::unique_ptr<Domain::BedInstance>& bed_instance)
        { return bed_instance->id().id; }
    );
    return result;
}

static void reload_config_container_after_undo(
    Domain::SelectionId project_id,
    std::unique_ptr<Domain::ConfigContainer>& active_container,
    const std::unique_ptr<Domain::ConfigContainer>& new_container,
    Biz::Scene::SceneInteractor& scene_interactor,
    bool is_different_printer
)
{
    active_container->set_bed(new_container->bed());

    if (is_different_printer)
    {
        active_container->mutable_selected_preset() = new_container->selected_preset();
    }

    std::set<std::size_t> new_bed_instance_ids{get_ids(new_container->bed_instances())};
    std::set<std::size_t> old_bed_instance_ids{get_ids(active_container->bed_instances())};

    const Utils::SetDiff<std::size_t> bed_instances_diff{
        Utils::get_sets_diff(old_bed_instance_ids, new_bed_instance_ids)
    };

    for (std::size_t bed_instance_id : bed_instances_diff.removed) {
        scene_interactor.erase_bed_instance(
            project_id,
            Domain::BedRef{active_container->id().id, bed_instance_id}
        );
    }

    for (std::size_t bed_instance_id : bed_instances_diff.added) {
        const auto instance_it{std::ranges::find_if(
            new_container->bed_instances(),
            [&](const auto& bi) { return bi->id().id == bed_instance_id; }
        )};

        const std::size_t position{static_cast<std::size_t>(
            std::distance(new_container->bed_instances().begin(), instance_it)
        )};
        ASSERT(position < new_container->bed_instances().size());

        scene_interactor.insert_bed_instance(
            project_id,
            active_container->id().id,
            position,
            std::move(*instance_it)
        );
        // Moved out unique ptr is already guaranteed nullptr, but lets be explicit.
        *instance_it = nullptr;
    }

    for (std::size_t bed_instance_id : bed_instances_diff.changed) {
        const auto active_instance_it{std::ranges::find_if(
            active_container->bed_instances(),
            [&](const auto& bi) { return bi->id().id == bed_instance_id; }
        )};
        const auto new_instance_it{std::ranges::find_if(
            new_container->bed_instances(),
            [&](const auto& bi)
            {
                // Skip moved out of instances.
                if (!bi) {
                    return false;
                }
                return bi->id().id == bed_instance_id;
            }
        )};

        ASSERT(active_instance_it != active_container->bed_instances().end());
        ASSERT(new_instance_it != new_container->bed_instances().end());

        Domain::BedInstance& active_instance{**active_instance_it};
        const Domain::BedInstance& new_instance{**new_instance_it};

        active_instance.set_index(new_instance.index());
        active_instance.bed                  = new_instance.bed;
        active_instance.transformation       = new_instance.transformation;
        active_instance.print_volume_enabled = new_instance.print_volume_enabled;
        active_instance.wipe_tower           = new_instance.wipe_tower;
        active_instance.custom_gcode         = new_instance.custom_gcode;
    }
}

void ProjectInteractor::reload_config_containers_after_undo(
    Domain::SelectionId project_id,
    Domain::Project::ConfigContainerList new_containers,
    InvokeLaterBag& listener_notifications
)
{
    Domain::Project::ConfigContainerList& old_containers{
        m_workbench.project(project_id).config_containers()
    };
    std::set<std::size_t> new_container_ids{get_ids(new_containers)};
    std::set<std::size_t> old_container_ids{get_ids(old_containers)};

    const Utils::SetDiff<std::size_t> containers_diff{
        Utils::get_sets_diff(old_container_ids, new_container_ids)
    };

    for (std::size_t id : containers_diff.removed) {
        remove_config_container(id);
    }
    for (std::size_t id : containers_diff.added) {
        auto it{std::ranges::find_if(new_containers,
            [&](const auto& cc)
            {
                ASSERT(cc);
                return cc->id().id == id;
            })
        };
        const std::size_t position{
            static_cast<std::size_t>(std::distance(new_containers.begin(), it))
        };
        ASSERT(position < new_containers.size());

        // Since the containers are in order, and modified only containers
        // are not removed, the position should be always valid, even
        // if project.config_containers.size() != new_containers.size().
        insert_config_container(project_id, std::move(*it), position);
        // Moved out unique ptr is already guaranteed nullptr, but lets be explicit.
        *it = nullptr;

        listener_notifications.add(
            [this, project_id, id]
            { m_virtual_extruder_interactor.notify_virtual_extruders_changed(project_id, id); }
        );
    }

    for (std::size_t config_container_id : containers_diff.changed) {
        auto active_it{std::ranges::find_if(
            old_containers,
            [&](const auto& cc) { return cc->id().id == config_container_id; }
        )};
        auto new_it{std::ranges::find_if(new_containers,
             [&](const auto& cc)
             {
                 // Skip moved out of containers.
                 if (!cc) {
                     return false;
                 }
                 return cc->id().id == config_container_id;
             }
        )};

        ASSERT(active_it != old_containers.end());
        ASSERT(new_it != new_containers.end());

        const bool is_different_printer{
            (*active_it)->selected_preset().hw_config.id
            != (*new_it)->selected_preset().hw_config.id
        };

        reload_config_container_after_undo(
            project_id,
            *active_it,
            *new_it,
            m_scene_interactor,
            is_different_printer
        );

        const bool virtual_extruders_differ =
            (*active_it)->virtual_extruders() != (*new_it)->virtual_extruders();

        if (virtual_extruders_differ) {
            m_virtual_extruder_interactor.restore_virtual_extruders_after_undo(
                project_id,
                config_container_id,
                std::move((*new_it)->virtual_extruders()),
                listener_notifications
            );
        }

        if (is_different_printer) {
            invoke_listeners<ISelectedConfigContainerChangedListener>(
                [&](auto* l)
                { l->on_selected_config_container_changed(project_id, config_container_id); }
            );
        }

        m_scene_interactor.update_beds(project_id, config_container_id);
    }
}

} // namespace Slic3r::Biz
