#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"
#include "PresetUpdaterFileHash.hpp"
#include "Slic3r/Assert.hpp"
#include <string>
#include <boost/filesystem/path.hpp>

#include "nlohmann/json.hpp"

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterProcessStatus;

class AbstractPresetUpdaterRepository
{
public:
    // Use std::move when calling constructor.
    AbstractPresetUpdaterRepository(
        const std::string& uuid,
        PresetUpdaterRepositoryDescriptor&& data,
        bool selected
    ) :
        m_data(std::move(data)),
        m_uuid(uuid),
        m_selected(selected)
    {
        m_data.uuid = m_uuid;
    }

    AbstractPresetUpdaterRepository(const AbstractPresetUpdaterRepository&)            = default;
    AbstractPresetUpdaterRepository(AbstractPresetUpdaterRepository&&) noexcept        = default;
    AbstractPresetUpdaterRepository& operator=(const AbstractPresetUpdaterRepository&) = default;
    AbstractPresetUpdaterRepository& operator=(AbstractPresetUpdaterRepository&&) noexcept = default;
    virtual ~AbstractPresetUpdaterRepository() = default;

    /**
     * @brief Peforms GET on vendor_indices.zip to target_path.
     **/
    virtual bool get_archive(
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const = 0;

    /**
     * @brief Peforms GET from target_path.
     * Performs check via filehash.
     **/
    virtual bool get_file(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const = 0;

    /**
     * @brief Peforms GET from target_path.
     * Does not perform check via filehash - should be used only to get files manifest.
     **/
    virtual bool get_version_manifest(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const = 0;

    /**
     * @brief Whether reaching this repository means reaching the network.
     */
    virtual bool is_online() const = 0;

    const PresetUpdaterRepositoryDescriptor& descriptor() const
    {
        return m_data;
    }

    std::string uuid() const
    {
        return m_uuid;
    }

    void set_descriptor(PresetUpdaterRepositoryDescriptor&& descriptor)
    {
        m_data = std::move(descriptor);
    }

    bool is_selected() const
    {
        return m_selected;
    }

    void set_selected(bool selected)
    {
        m_selected = selected;
    }

    static bool extract_repository_header(
        const nlohmann::json& json,
        PresetUpdaterRepositoryDescriptor& data,
        std::string& warning_msg
    );

protected:
    PresetUpdaterRepositoryDescriptor m_data;
    std::string m_uuid;
    bool m_selected;
};

class OnlinePresetUpdaterRepository : public AbstractPresetUpdaterRepository
{
public:
    OnlinePresetUpdaterRepository(
        const std::string& uuid,
        PresetUpdaterRepositoryDescriptor&& data,
        bool selected
    ) :
        AbstractPresetUpdaterRepository(uuid, std::move(data), selected)
    {
        if (!m_data.url.empty() && m_data.url.back() != '/') {
            m_data.url += "/";
        }
    }

    OnlinePresetUpdaterRepository(const OnlinePresetUpdaterRepository&)                = default;
    OnlinePresetUpdaterRepository(OnlinePresetUpdaterRepository&&) noexcept            = default;
    OnlinePresetUpdaterRepository& operator=(const OnlinePresetUpdaterRepository&)     = default;
    OnlinePresetUpdaterRepository& operator=(OnlinePresetUpdaterRepository&&) noexcept = default;
    ~OnlinePresetUpdaterRepository() override                                          = default;

    /**
     * @brief Peforms GET on vendor_indices.zip to target_path.
     **/
    bool get_archive(
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    /**
     * @brief Peforms GET from target_path.
     * Performs check via filehash.
     **/
    virtual bool get_file(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    /**
     * @brief Peforms GET from target_path.
     * Does not perform check via filehash - should be used only to get files manifest.
     **/
    virtual bool get_version_manifest(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    bool is_online() const override
    {
        return true;
    }

private:
    bool get_file_inner(
        const std::string& url,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const;
};

class LocalPresetUpdaterRepository : public AbstractPresetUpdaterRepository
{
public:
    LocalPresetUpdaterRepository(
        const std::string& uuid,
        PresetUpdaterRepositoryDescriptor&& data,
        bool selected
    ) :
        AbstractPresetUpdaterRepository(uuid, std::move(data), selected)
    {}

    LocalPresetUpdaterRepository(const LocalPresetUpdaterRepository&)                = default;
    LocalPresetUpdaterRepository(LocalPresetUpdaterRepository&&) noexcept            = default;
    LocalPresetUpdaterRepository& operator=(const LocalPresetUpdaterRepository&)     = default;
    LocalPresetUpdaterRepository& operator=(LocalPresetUpdaterRepository&&) noexcept = default;
    ~LocalPresetUpdaterRepository() override                                         = default;

    /**
     * @brief Peforms GET on vendor_indices.zip to target_path.
     **/
    bool get_archive(
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    /**
     * @brief Peforms GET from target_path.
     * Performs check via filehash.
     **/
    virtual bool get_file(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    /**
     * @brief Peforms GET from target_path.
     * Does not perform check via filehash - should be used only to get files manifest.
     **/
    virtual bool get_version_manifest(
        const std::string& source_subpath,
        const boost::filesystem::path& target_path,
        PresetUpdaterProcessStatus* process_status
    ) const override;

    bool is_online() const override
    {
        return false;
    }

    static bool extract_local_archive_repository(
        PresetUpdaterRepositoryDescriptor& manifest_data,
        std::string& error_msg
    );

    static bool data_structure_check(
        const boost::filesystem::path& unzipped_path,
        std::string& error_msg
    );

private:
    bool get_file_inner(
        const boost::filesystem::path& source_path,
        const boost::filesystem::path& target_path
    ) const;
};

} // namespace Slic3r::Biz::PresetUpdater
