#ifndef slic3r_PresetArchiveDatabase_hpp_
#define slic3r_PresetArchiveDatabase_hpp_

#include "Event.hpp"

#include <boost/uuid/uuid_generators.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace Slic3r {
class AppConfig;
namespace GUI {

struct ArchiveRepositoryGetFileArgs {
	boost::filesystem::path target_path;
	
	std::string repository_id;
};

class ArchiveRepository
{
public:
	struct RepositoryManifest {
		// mandatory
		std::string id;
		std::string name;
		std::string url;
		// optional
		std::string index_url;
		std::string description;
		std::string visibility;
		// not read from manifest json
		boost::filesystem::path tmp_path; // Where archive is unzziped. Created each app run. 
		boost::filesystem::path source_path; // Path given by user. Stored between app runs.

        RepositoryManifest() = default;
        RepositoryManifest(
            const std::string &id,
            const std::string &name,
            const std::string &url,
            const std::string &index_url = "",
            const std::string &description = "",
            const std::string &visibility = "",
            const boost::filesystem::path &tmp_path = "",
            const boost::filesystem::path &source_path = ""
        )
            : id(id)
            , name(name)
            , url(url)
            , index_url(index_url)
            , description(description)
            , visibility(visibility)
            , tmp_path(tmp_path)
            , source_path(source_path) 
		{}
        RepositoryManifest(const RepositoryManifest &other)
            : id(other.id)
            , name(other.name)
            , url(other.url)
            , index_url(other.index_url)
            , description(other.description)
            , visibility(other.visibility)
            , tmp_path(other.tmp_path)
            , source_path(other.source_path) 
		{}
	};
	// Use std::move when calling constructor.
	ArchiveRepository(const std::string& uuid, RepositoryManifest&& data) 
		: m_data(std::move(data))
		, m_uuid(uuid) 
	{}
	virtual ~ArchiveRepository() {}
	// Gets vendor_indices.zip to target_path
	virtual bool get_archive(const boost::filesystem::path& target_path) const = 0;
	// Gets file if repository_id arg matches m_id.
	// Should be used to get the most recent ini file and every missing resource. 
	virtual bool get_file(const std::string& source_subpath, const boost::filesystem::path& target_path, const std::string& repository_id) const = 0;
	// Gets file without id check - for not yet encountered vendors only!
	virtual bool get_ini_no_id(const std::string& source_subpath, const boost::filesystem::path& target_path) const = 0;
	const RepositoryManifest& get_manifest() const { return m_data; }
	std::string get_uuid() const { return m_uuid; }
    // Only local archvies can return false
    virtual bool is_extracted() const { return true; }
    virtual void do_extract() {}
    void set_manifest(RepositoryManifest &&manifest) { m_data = std::move(manifest); }

protected:
	RepositoryManifest m_data;
	std::string m_uuid;
};

class OnlineArchiveRepository : public ArchiveRepository
{
public:
	OnlineArchiveRepository(const std::string& uuid, RepositoryManifest&& data) : ArchiveRepository(uuid, std::move(data))
	{
		if (m_data.url.back() != '/') {
			m_data.url += "/";
		}
	}
	// Gets vendor_indices.zip to target_path.
	bool get_archive(const boost::filesystem::path& target_path) const override;
	// Gets file if repository_id arg matches m_id.
	// Should be used to get the most recent ini file and every missing resource. 
	bool get_file(const std::string& source_subpath, const boost::filesystem::path& target_path, const std::string& repository_id) const override;
	// Gets file without checking id.
	// Should be used only if no previous ini file exists.
	bool get_ini_no_id(const std::string& source_subpath, const boost::filesystem::path& target_path) const override;
private:
	bool get_file_inner(const std::string& url, const boost::filesystem::path& target_path) const;
};

class LocalArchiveRepository : public ArchiveRepository
{
public:
	LocalArchiveRepository(const std::string& uuid, RepositoryManifest&& data, bool extracted) : ArchiveRepository(uuid, std::move(data)), m_extracted(extracted) 
    {}
	// Gets vendor_indices.zip to target_path.
	bool get_archive(const boost::filesystem::path& target_path) const override;
	// Gets file if repository_id arg matches m_id.
	// Should be used to get the most recent ini file and every missing resource. 
	bool get_file(const std::string& source_subpath, const boost::filesystem::path& target_path, const std::string& repository_id) const override;
	// Gets file without checking id.
	// Should be used only if no previous ini file exists.
	bool get_ini_no_id(const std::string& source_subpath, const boost::filesystem::path& target_path) const override;
    bool is_extracted() const override { return m_extracted;  }
    void do_extract() override;
    
private:
	bool get_file_inner(const boost::filesystem::path& source_path, const boost::filesystem::path& target_path) const;
    bool m_extracted;
};

typedef std::vector<std::unique_ptr<ArchiveRepository>> PrivateArchiveRepositoryVector;
typedef std::vector<const ArchiveRepository*> SharedArchiveRepositoryVector;

class PresetArchiveDatabase
{
public:
	PresetArchiveDatabase(AppConfig* app_config, wxEvtHandler* evt_handler);
	~PresetArchiveDatabase() {}

	void sync_blocking();

	// Do not use get_all_archive_repositories to perform any GET calls. Use get_selected_archive_repositories instead.
    SharedArchiveRepositoryVector get_all_archive_repositories() const;
    // Creates copy of m_archive_repositories of shared pointers that are selected in m_selected_repositories_uuid.
    SharedArchiveRepositoryVector get_selected_archive_repositories() const;
	bool is_selected_repository_by_uuid(const std::string& uuid) const;
	bool is_selected_repository_by_id(const std::string& repo_id) const;
	const std::map<std::string, bool>& get_selected_repositories_uuid() const { assert(m_selected_repositories_uuid.size() == m_archive_repositories.size()); return m_selected_repositories_uuid; }
    // Does re-extract all local archives
	bool set_selected_repositories(const std::vector<std::string>& used_uuids, std::string& msg);
    void set_installed_printer_repositories(const std::vector<std::string> &used_ids);
	std::string add_local_archive(const boost::filesystem::path path, std::string& msg);
	void remove_local_archive(const std::string& uuid);
    bool extract_archives_with_check(std::string &msg);

private:
	void load_app_manifest_json();
	void copy_initial_manifest();
	void read_server_manifest(const std::string& json_body);
	void save_app_manifest_json() const;
	void clear_online_repos();
	bool is_selected(const std::string& uuid) const;
    bool has_installed_printers(const std::string &uuid) const;
	boost::filesystem::path get_stored_manifest_path() const;
	void consolidate_uuid_maps();
    void extract_local_archives();
	std::string get_next_uuid();
	wxEvtHandler*					p_evt_handler;
	boost::filesystem::path			m_unq_tmp_path;
    PrivateArchiveRepositoryVector  m_archive_repositories;
	std::map<std::string, bool>		m_selected_repositories_uuid;
    std::map<std::string, bool>		m_has_installed_printer_repositories_uuid;
	boost::uuids::random_generator	m_uuid_generator;
};

}} // Slic3r::GUI

#endif // PresetArchiveDatabase