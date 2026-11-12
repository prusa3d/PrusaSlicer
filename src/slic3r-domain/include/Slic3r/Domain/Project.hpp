#pragma once

#include "Slic3r/Domain/ProjectMetadata.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/BedContainer.hpp"
#include "Slic3r/Domain/FindById.hpp"
#include "Slic3r/Domain/ProjectDirectoryStorage.hpp"

namespace Slic3r::Domain {
class Model;
class ModelInstance;
class ModelVolume;
class ModelObject;
enum class ModelVolumeType : int;

/**
 * All data that can be loaded/saved into .3mf file.
 */
class Project final
{
public:
    using ConfigContainerList = std::vector<std::unique_ptr<ConfigContainer>>;

    Project();
    Project(const Project& project);

    Project(Project&& other) noexcept = default;
    Project& operator=(Project&& other) noexcept = default;

    void set_metadata(const ProjectMetadata& metadata)
    {
        m_metadata = metadata;
    }

    const ProjectMetadata& metadata() const
    {
        return m_metadata;
    }

    void increment_version()
    {
        m_metadata.increment_version();
    }

    /**
     * @warning Do not call this method directly if project is already handled by ProjectInteractor
     * UI would not be notified
     */
    void set_file_name(const std::string& file_name)
    {
        m_file_name = file_name;
    }

    /**
     * @warning Do not call this method directly if project is already handled by ProjectInteractor
     * UI would not be notified
     */
    void set_file_path(const boost::filesystem::path& file_path)
    {
        m_loaded_file_path = file_path;
        set_file_name(file_path.stem().string());
    }

    [[nodiscard]] const std::string& file_name() const
    {
        return m_file_name;
    }

    [[nodiscard]] const boost::filesystem::path& loaded_file_path() const
    {
        return m_loaded_file_path;
    }

    [[nodiscard]] ConfigContainerList& config_containers()
    {
        return m_config_containers;
    }

    [[nodiscard]] const ConfigContainerList& config_containers() const
    {
        return m_config_containers;
    }

    [[nodiscard]] const Domain::Model& model() const
    {
        return *m_model;
    }

    [[nodiscard]] Domain::Model& model()
    {
        return *m_model;
    }

    [[nodiscard]] const ConfigContainer* find_config_container(size_t id) const;
    [[nodiscard]] ConfigContainer* find_config_container(size_t id);
    [[nodiscard]] const ConfigContainer* find_config_container_by_bed_instance_id(size_t id) const;
    [[nodiscard]] ConfigContainer* find_config_container_by_bed_instance_id(size_t id);

    [[nodiscard]] const Domain::ModelObject* find_object_by_id(size_t id) const;
    [[nodiscard]] const Domain::ModelVolume* find_volume_by_id(size_t obj_id, size_t vol_id) const;
    [[nodiscard]] const Domain::ModelInstance*
    find_instance_by_id(size_t obj_id, size_t inst_id) const;
    [[nodiscard]] const Domain::ModelInstance* find_instance_by_id(size_t inst_id) const;
    [[nodiscard]] Domain::ModelObject* find_object_by_id(size_t id);
    [[nodiscard]] Domain::ModelVolume* find_volume_by_id(size_t obj_id, size_t vol_id);
    [[nodiscard]] Domain::ModelInstance* find_instance_by_id(size_t obj_id, size_t inst_id);
    [[nodiscard]] Domain::ModelInstance* find_instance_by_id(size_t inst_id);

    [[nodiscard]] const BedInstance* find_bed_instance_by_id(size_t id) const;
    [[nodiscard]] BedInstance* find_bed_instance_by_id(size_t id);

    [[nodiscard]] const BedContainer& bed_container() const
    {
        return m_bed_container;
    }

    [[nodiscard]] BedContainer& bed_container()
    {
        return m_bed_container;
    }

    [[nodiscard]] const Bed* find_bed_by_id(size_t id) const;
    [[nodiscard]] Bed* find_bed_by_id(size_t id);

    [[nodiscard]] const ConfigContainer*
    find_config_container_by_element(const Domain::ElementRef& element) const;
    [[nodiscard]] ConfigContainer*
    find_config_container_by_element(const Domain::ElementRef& element);

    /**
     * @brief Updates all config container's printer presets with given @a preset if its IDs match.
     * @param printer preset to update stored evaluated presets with.
     */
    void update_preset(const Preset::EvaluatedPrinterPreset::Preset& printer);

    /**
     * @brief Updates all config container's print presets with given @a preset if its IDs match.
     * @param print preset to update stored evaluated presets with.
     */
    void update_preset(const Preset::EvaluatedPrintPreset::Preset& print);

    /**
     * @brief Updates all config container's tool-print presets with given @a preset if its IDs match.
     * @param tool_print preset to update stored evaluated presets with.
     */
    void update_preset(const Preset::EvaluatedToolPrintPreset::Preset& tool_print);

    /**
     * @brief Updates all config container's material presets with given @a preset if its IDs match.
     * @param material preset to update stored evaluated presets with.
     */
    void update_preset(const Preset::EvaluatedMaterialPreset::Preset& material);

    ModelInstanceList& unplaced_model_instances()
    {
        return m_unplaced_model_instances;
    }

    const ModelInstanceList& unplaced_model_instances() const
    {
        return m_unplaced_model_instances;
    }

    ProjectDirectoryStorage& directory_storage()
    {
        return m_directory_storage;
    }

private:
    ProjectMetadata m_metadata;
    std::string m_file_name; ///< Used when saving project, can be changed
    boost::filesystem::path
        m_loaded_file_path; ///< Can be empty, path to the project file, set only on load and save
    ConfigContainerList m_config_containers;
    BedContainer m_bed_container;
    std::unique_ptr<Domain::Model> m_model;
    ModelInstanceList m_unplaced_model_instances;
    ProjectDirectoryStorage m_directory_storage;
};

} // namespace Slic3r::Domain
