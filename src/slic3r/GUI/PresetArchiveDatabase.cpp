#include "PresetArchiveDatabase.hpp"

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UserAccount.hpp"
#include "slic3r/Utils/PresetUpdaterWrapper.hpp"
#include "slic3r/GUI/Field.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/log/trivial.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/nowide/fstream.hpp> // IWYU pragma: keep
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cctype>
#include <curl/curl.h>
#include <iostream>
#include <fstream>

namespace pt = boost::property_tree;
namespace fs = boost::filesystem;
namespace Slic3r {

static const char* TMP_EXTENSION = ".download";

namespace {
bool unzip_repository(const fs::path& source_path, const fs::path& target_path)
{
	mz_zip_archive archive;
	mz_zip_zero_struct(&archive);
	if (!open_zip_reader(&archive, source_path.string())) {
		BOOST_LOG_TRIVIAL(error) << "Couldn't open zipped Archive source. " << source_path;
		return false;
	}
	size_t num_files = mz_zip_reader_get_num_files(&archive);

	for (size_t i = 0; i < num_files; ++i) {
		mz_zip_archive_file_stat file_stat;
		if (!mz_zip_reader_file_stat(&archive, i, &file_stat)) {
			BOOST_LOG_TRIVIAL(error) << "Failed to get file stat for file #" << i << " in the zip archive. Ending Unzipping.";
			close_zip_reader(&archive);
			return false;
		}
		fs::path extracted_path = target_path / file_stat.m_filename;
		if (file_stat.m_is_directory) {
			// Create directory if it doesn't exist
			fs::create_directories(extracted_path);
			continue;
		}
		// Create parent directory if it doesn't exist
		fs::create_directories(extracted_path.parent_path());
		// Extract file
		if (!mz_zip_reader_extract_to_file(&archive, i, extracted_path.string().c_str(), 0)) {
			BOOST_LOG_TRIVIAL(error) << "Failed to extract file #" << i << " from the zip archive. Ending Unzipping.";
			close_zip_reader(&archive);
			return false;
		}
	}
	close_zip_reader(&archive);
	return true;
}

bool extract_repository_header(const pt::ptree& ptree, ArchiveRepository::RepositoryManifest& data)
{
	// mandatory atributes
	if (const auto name = ptree.get_optional<std::string>("name"); name){
		data.name = *name;
	} else {
		BOOST_LOG_TRIVIAL(error) << "Failed to find \"name\" parameter in source manifest. Source is invalid.";
		return false;
	}
	if (const auto id = ptree.get_optional<std::string>("id"); id) {
		data.id = *id;
	}
	else {
		BOOST_LOG_TRIVIAL(error) << "Failed to find \"id\" parameter in source manifest. Source is invalid.";
		return false;
	}
	if (const auto url = ptree.get_optional<std::string>("url"); url) {
		data.url = *url;
	}
	else {
		BOOST_LOG_TRIVIAL(error) << "Failed to find \"url\" parameter in source manifest. Source is invalid.";
		return false;
	}
	// optional atributes
	if (const auto index_url = ptree.get_optional<std::string>("index_url"); index_url) {
		data.index_url = *index_url;
	}
	if (const auto description = ptree.get_optional<std::string>("description"); description) {
		data.description = *description;
	}
	if (const auto visibility = ptree.get_optional<std::string>("visibility"); visibility) {
		data.visibility = *visibility;
	}
	return true;
}

void delete_path_recursive(const fs::path& path)
{
	try {
        boost::system::error_code ec;
		if (fs::exists(path, ec) && !ec) {
			for (fs::directory_iterator it(path); it != fs::directory_iterator(); ++it) {
				const fs::path subpath = it->path();
				if (fs::is_directory(subpath)) {
					delete_path_recursive(subpath);
				} else {
					fs::remove(subpath);
				}
			}
			fs::remove(path);
		}
	}
	catch (const std::exception&) {
		BOOST_LOG_TRIVIAL(error) << "Failed to delete files at: " << path;
	}
}

bool extract_local_archive_repository( ArchiveRepository::RepositoryManifest& manifest_data)
{
    assert(!manifest_data.tmp_path.empty());
    assert(!manifest_data.source_path.empty());
	// Delete previous data before unzip.
	// We have unique path in temp set for whole run of slicer and in it folder for each repo. 
	delete_path_recursive(manifest_data.tmp_path);
	fs::create_directories(manifest_data.tmp_path);
	// Unzip repository zip to unique path in temp directory.
    if (!unzip_repository(manifest_data.source_path, manifest_data.tmp_path)) {
		return false;
	}
	// Read the manifest file.
	fs::path manifest_path = manifest_data.tmp_path / "manifest.json";
	try
	{
		pt::ptree ptree;
		pt::read_json(manifest_path.string(), ptree);
		if (!extract_repository_header(ptree, manifest_data)) {
            BOOST_LOG_TRIVIAL(error) << "Failed to load source " << manifest_data.tmp_path;
			return false;
		}
	}
	catch (const std::exception& e)
	{
		BOOST_LOG_TRIVIAL(error) << "Failed to read source manifest JSON " << manifest_path << ". reason: " << e.what();
		return false;
	}
	return true;
}

std::string escape_string(const std::string& unescaped)
{
	std::string ret_val;
	CURL* curl = curl_easy_init();
	if (curl) {
		char* decoded = curl_easy_escape(curl, unescaped.c_str(), unescaped.size());
		if (decoded) {
			ret_val = std::string(decoded);
			curl_free(decoded);
		}
		curl_easy_cleanup(curl);
	}
	return ret_val;
}
std::string escape_path_by_element(const std::string& path_string)
{
	const boost::filesystem::path path(path_string);
	std::string ret_val = escape_string(path.filename().string());
	boost::filesystem::path parent(path.parent_path());
	while (!parent.empty() && parent.string() != "/") // "/" check is for case "/file.gcode" was inserted. Then boost takes "/" as parent_path.
	{
		ret_val = escape_string(parent.filename().string()) + "/" + ret_val;
		parent = parent.parent_path();
	}
	return ret_val;
}

bool add_authorization_header(Http& http)
{
    if (wxApp::GetInstance() == nullptr || ! GUI::wxGetApp().plater())
        return false;
    const std::string access_token = GUI::wxGetApp().plater()->get_user_account()->get_access_token();
    if (!access_token.empty()) {
        http.header("Authorization", "Bearer " + access_token);
    }
    return true;
}

}

bool OnlineArchiveRepository::get_file_inner(const std::string& url, const fs::path& target_path, PresetUpdaterUIStatus* ui_status) const
{

	bool res = false;
	fs::path tmp_path = target_path;
	tmp_path += format(".%1%%2%", get_current_pid(), TMP_EXTENSION);
	BOOST_LOG_TRIVIAL(info) << format("Get: `%1%`\n\t-> `%2%`\n\tvia tmp path `%3%`",
		url,
		target_path.string(),
		tmp_path.string());

	auto http = Http::get(url);
    if (!add_authorization_header(http))
        return false;
    http
		.timeout_max(30)
		.on_progress([](Http::Progress, bool& cancel) {
			//if (cancel) { cancel = true; }
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%", url, http_status, body);
             ui_status->set_error(error);
             res = false;
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			if (body.empty()) {
				return;
			}
			fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
			file.write(body.c_str(), body.size());
			file.close();
			fs::rename(tmp_path, target_path);
			res = true;
		})
        .on_retry([&](int attempt, unsigned delay) {
            return !ui_status->on_attempt(attempt, delay);
		})
		.perform_sync(ui_status->get_retry_policy());	

	return res;
}

bool OnlineArchiveRepository::get_archive(const fs::path& target_path, PresetUpdaterUIStatus* ui_status) const
{
	return get_file_inner(m_data.index_url.empty() ? m_data.url + "vendor_indices.zip" : m_data.index_url, target_path, ui_status);
}

bool OnlineArchiveRepository::get_file(const std::string& source_subpath, const fs::path& target_path, const std::string& repository_id, PresetUpdaterUIStatus* ui_status) const
{
	if (repository_id != m_data.id) {
		BOOST_LOG_TRIVIAL(error) << "Error getting file " << source_subpath << ". The repository_id was not matching.";
	    return false;
	}
    
    ui_status->set_target(target_path.filename().string());
    
	const std::string escaped_source_subpath = escape_path_by_element(source_subpath);
	return get_file_inner(m_data.url + escaped_source_subpath, target_path, ui_status);
}

bool OnlineArchiveRepository::get_ini_no_id(const std::string& source_subpath, const fs::path& target_path, PresetUpdaterUIStatus* ui_status) const
{
    ui_status->set_target(target_path.filename().string());
    
	const std::string escaped_source_subpath = escape_path_by_element(source_subpath);
	return get_file_inner(m_data.url + escaped_source_subpath, target_path, ui_status);
}

bool LocalArchiveRepository::get_file_inner(const fs::path& source_path, const fs::path& target_path) const
{
	BOOST_LOG_TRIVIAL(debug) << format("Copying %1% to %2%", source_path, target_path);
	std::string error_message;
	CopyFileResult cfr = Slic3r::copy_file(source_path.string(), target_path.string(), error_message, false);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Copying of " << source_path << " to " << target_path << " has failed (" << cfr << "): " << error_message;
		// remove target file, even if it was there before
        boost::system::error_code ec;
		if (fs::exists(target_path, ec) && !ec) {
            ec.clear();
			fs::remove(target_path, ec);
			if (ec) {
				BOOST_LOG_TRIVIAL(error) << format("Failed to delete file: %1%", ec.message());
			}
		}
		return false;
	}
	// Permissions should be copied from the source file by copy_file(). We are not sure about the source
	// permissions, let's rewrite them with 644.
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target_path, perms);

	return true;
}

bool LocalArchiveRepository::get_file(const std::string& source_subpath, const fs::path& target_path, const std::string& repository_id, PresetUpdaterUIStatus* ui_status) const
{
	if (repository_id != m_data.id) {
		BOOST_LOG_TRIVIAL(error) << "Error getting file " << source_subpath << ". The repository_id was not matching.";
		return false;
	}
	return get_file_inner(m_data.tmp_path / source_subpath, target_path);
}
bool LocalArchiveRepository::get_ini_no_id(const std::string& source_subpath, const fs::path& target_path, PresetUpdaterUIStatus* ui_status) const
{
	return get_file_inner(m_data.tmp_path / source_subpath, target_path);
}
bool LocalArchiveRepository::get_archive(const fs::path& target_path, PresetUpdaterUIStatus* ui_status) const
{
	fs::path source_path = fs::path(m_data.tmp_path) / "vendor_indices.zip";
	return get_file_inner(std::move(source_path), target_path);
}

void LocalArchiveRepository::do_extract() 
{
    RepositoryManifest new_manifest;
    new_manifest.source_path = this->get_manifest().source_path;
    new_manifest.tmp_path = this->get_manifest().tmp_path;
    m_extracted = extract_local_archive_repository(new_manifest);
    set_manifest(std::move(new_manifest));
}

//-------------------------------------PresetArchiveDatabase-------------------------------------------------------------------------------------------------------------------------

PresetArchiveDatabase::PresetArchiveDatabase()
{
    boost::system::error_code ec;
	m_unq_tmp_path = fs::temp_directory_path() / fs::unique_path();
	fs::create_directories(m_unq_tmp_path, ec);
	assert(!ec);

	load_app_manifest_json();
}

bool PresetArchiveDatabase::set_selected_repositories(const std::vector<std::string>& selected_uuids, std::string& msg)
{
    // First re-extract locals, this will set is_extracted flag
    extract_local_archives();
	// Check if some uuids leads to the same id (online vs local conflict)
	std::map<std::string, std::string> used_set;
	for (const std::string& uuid : selected_uuids) {
		std::string id;
		std::string name;
		for (const auto& archive : m_archive_repositories) {
			if (archive->get_uuid() != uuid) {
                continue;
            }        
		    id = archive->get_manifest().id;
		    name = archive->get_manifest().name;
            if (!archive->is_extracted()) {
                // non existent local repo since start selected
                msg = GUI::format(
                    _L("Cannot select local source from path: %1%. It was not extracted."),
                    archive->get_manifest().source_path
                );
                return false;
            }
		    break;
		}
		assert(!id.empty());
		if (auto it = used_set.find(id); it != used_set.end()) {
			msg = GUI::format(_L("Cannot select two sources with the same id: %1% and %2%"), it->second, name);
			return false;
		}
		used_set.emplace(id, name);
	}
	// deselect all first
	for (auto& pair : m_selected_repositories_uuid) {
		pair.second = false;
	}
	for (const std::string& uuid : selected_uuids) {
		m_selected_repositories_uuid[uuid] = true;
	}
	save_app_manifest_json();
	return true;
}
bool PresetArchiveDatabase::extract_archives_with_check(std::string &msg)
{
    extract_local_archives();
    // std::map<std::string, bool> m_selected_repositories_uuid
    for (const auto& pair : m_selected_repositories_uuid) {
        if (!pair.second) {
            continue;
        }
        const std::string uuid = pair.first;
        auto compare_repo = [&uuid](const std::unique_ptr<ArchiveRepository> &repo) {
            return repo->get_uuid() == uuid;
        };

        const auto& archives_it =std::find_if(m_archive_repositories.begin(), m_archive_repositories.end(), compare_repo);
        assert(archives_it != m_archive_repositories.end());
        if (!archives_it->get()->is_extracted()) {
            // non existent local repo since start selected
            msg += std::string(msg.empty() ? "" : "\n") + archives_it->get()->get_manifest().source_path.string();
        }
    }
    return msg.empty();
}
void PresetArchiveDatabase::set_installed_printer_repositories(const std::vector<std::string> &used_ids)
{
	// set all uuids as not having installed printer
    m_has_installed_printer_repositories_uuid.clear();
    for (const auto &archive : m_archive_repositories) {
        m_has_installed_printer_repositories_uuid.emplace(archive->get_uuid(), false);
	}
	// set correct repos as having installed printer
    for (const std::string &used_id : used_ids) {
		// find archive with id and is used
        std::vector<std::string> selected_uuid;
        std::vector<std::string> unselected_uuid;
        for (const auto &archive : m_archive_repositories) {
            if (archive->get_manifest().id != used_id) {
				continue;
			}	
			const std::string uuid = archive->get_uuid();
            if (m_selected_repositories_uuid[uuid]) {
                selected_uuid.emplace_back(uuid);
            } else {
                unselected_uuid.emplace_back(uuid);
            }
		}
        
        if (selected_uuid.empty() && unselected_uuid.empty()) {
            // there is id in used_ids that is not in m_archive_repositories - BAD
            assert(false);
            continue;
        } else if (selected_uuid.size() == 1){
            // regular case
             m_has_installed_printer_repositories_uuid[selected_uuid.front()] = true;
        } else if (selected_uuid.size() > 1) {
            // this should not happen, only one repo of same id should be selected (online / local conflict)
            assert(false);
            // select first one to solve the conflict
            m_has_installed_printer_repositories_uuid[selected_uuid.front()] = true;
            // unselect the rest
            for (size_t i = 1; i < selected_uuid.size(); i++) {
                m_selected_repositories_uuid[selected_uuid[i]] = false;
            }
        } else if (selected_uuid.empty()) {
            // This is a rare case, where there are no selected repos with matching id but id has installed printers
            // Repro: install printer, unselect repo in the next run of wizard, next, cancel wizard, run wizard again and press finish.
            // Solution: Select the first unselected 
            m_has_installed_printer_repositories_uuid[unselected_uuid.front()] = true;
            m_selected_repositories_uuid[unselected_uuid.front()] = true;
        }

	}
    save_app_manifest_json();
}

std::string PresetArchiveDatabase::add_local_archive(const boost::filesystem::path path, std::string& msg)
{
	if (auto it = std::find_if(m_archive_repositories.begin(), m_archive_repositories.end(), [path](const std::unique_ptr<ArchiveRepository>& ptr) {
		return ptr->get_manifest().source_path == path;
		}); it != m_archive_repositories.end())
	{
		msg = GUI::format(_L("Failed to add local archive %1%. Path already used."), path);
		BOOST_LOG_TRIVIAL(error) << msg;
		return std::string();
	}
	std::string uuid = get_next_uuid();
	ArchiveRepository::RepositoryManifest header_data;
    header_data.source_path = path;
    header_data.tmp_path = m_unq_tmp_path / uuid;
	if (!extract_local_archive_repository(header_data)) {
		msg = GUI::format(_L("Failed to extract local archive %1%."), path);
		BOOST_LOG_TRIVIAL(error) << msg;
		return std::string();
	}
	// Solve if it can be set true first.
	m_selected_repositories_uuid[uuid] = false;
    m_has_installed_printer_repositories_uuid[uuid] = false;
	m_archive_repositories.emplace_back(std::make_unique<LocalArchiveRepository>(uuid, std::move(header_data), true));

	save_app_manifest_json();
	return uuid;
}
void PresetArchiveDatabase::remove_local_archive(const std::string& uuid)
{
	auto compare_repo = [uuid](const std::unique_ptr<ArchiveRepository>& repo) {
		return repo->get_uuid() == uuid;
	};

	auto archives_it = std::find_if(m_archive_repositories.begin(), m_archive_repositories.end(), compare_repo);
	assert(archives_it != m_archive_repositories.end());
	std::string removed_uuid = archives_it->get()->get_uuid();
	m_archive_repositories.erase(archives_it);
	
	auto used_it = m_selected_repositories_uuid.find(removed_uuid);
	assert(used_it != m_selected_repositories_uuid.end());
	m_selected_repositories_uuid.erase(used_it);

    auto inst_it = m_has_installed_printer_repositories_uuid.find(removed_uuid);
    assert(inst_it != m_has_installed_printer_repositories_uuid.end());
    m_has_installed_printer_repositories_uuid.erase(inst_it);

	save_app_manifest_json();
}

 void PresetArchiveDatabase::extract_local_archives()
 {
    for (auto &archive : m_archive_repositories) {
         archive->do_extract();
    }
 }    

void PresetArchiveDatabase::load_app_manifest_json()
{
	const fs::path path = get_stored_manifest_path();
    boost::system::error_code ec;
	if (!fs::exists(path, ec) || ec) {
		copy_initial_manifest();
	}
	boost::nowide::ifstream file(path.string());
	std::string data;
	if (file.is_open()) {
		std::string line;
		while (getline(file, line)) {
			data += line;
		}
		file.close();
	}
	else {
		assert(false);
		BOOST_LOG_TRIVIAL(error) << "Failed to read Archive Source Manifest at " << path;
	}
	if (data.empty()) {
		return;
	}

	m_archive_repositories.clear();
    m_selected_repositories_uuid.clear();
    m_has_installed_printer_repositories_uuid.clear();
	try
	{
		std::stringstream ss(data);
		pt::ptree ptree;
		pt::read_json(ss, ptree);
		for (const auto& subtree : ptree) {
			// if has tmp_path its local repo else its online repo (manifest is written in its zip, not in our json)
			if (const auto source_path = subtree.second.get_optional<std::string>("source_path"); source_path) {
				ArchiveRepository::RepositoryManifest manifest;
				std::string uuid = get_next_uuid();
                manifest.source_path = boost::filesystem::path(*source_path);
                manifest.tmp_path = m_unq_tmp_path / uuid;
                bool extracted = extract_local_archive_repository(manifest);
				// "selected" flag
				if(const auto used = subtree.second.get_optional<bool>("selected"); used) {
                    m_selected_repositories_uuid[uuid] = extracted && *used;
				} else {
					assert(false);
                    m_selected_repositories_uuid[uuid] = extracted;
				}
				// "has_installed_printers" flag
                if (const auto used = subtree.second.get_optional<bool>("has_installed_printers"); used) {
                    m_has_installed_printer_repositories_uuid[uuid] = extracted && *used;
                } else {
                    assert(false);
                    m_has_installed_printer_repositories_uuid[uuid] = false;
                }
				m_archive_repositories.emplace_back(std::make_unique<LocalArchiveRepository>(std::move(uuid), std::move(manifest), extracted));
			
				continue;
			}
			// online repo
			ArchiveRepository::RepositoryManifest manifest;
			std::string uuid = get_next_uuid();
			if (!extract_repository_header(subtree.second, manifest)) {
				assert(false);
				BOOST_LOG_TRIVIAL(error) << "Failed to read one of source headers.";
				continue;
			}
            // "selected" flag
			if (const auto used = subtree.second.get_optional<bool>("selected"); used) {
				m_selected_repositories_uuid[uuid] = *used;
			} else {
				assert(false);
                m_selected_repositories_uuid[uuid] = true;
			}
            // "has_installed_printers" flag
            if (const auto used = subtree.second.get_optional<bool>("has_installed_printers"); used) {
                m_has_installed_printer_repositories_uuid[uuid] = *used;
            } else {
                assert(false);
                m_has_installed_printer_repositories_uuid[uuid] = false;
            }
			m_archive_repositories.emplace_back(std::make_unique<OnlineArchiveRepository>(std::move(uuid), std::move(manifest)));
		}
	}
	catch (const std::exception& e)
	{
		BOOST_LOG_TRIVIAL(error) << "Failed to read archives JSON. " << e.what();
	}
}

void PresetArchiveDatabase::copy_initial_manifest()
{
	const fs::path target_path = get_stored_manifest_path();
	const fs::path source_path = fs::path(resources_dir()) / "profiles" / "ArchiveRepositoryManifest.json";
	assert(fs::exists(source_path));
	std::string error_message;
	CopyFileResult cfr = Slic3r::copy_file(source_path.string(), target_path.string(), error_message, false);
	assert(cfr == CopyFileResult::SUCCESS);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Failed to copy ArchiveRepositoryManifest.json from resources.";
		return;
	}
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target_path, perms);
}

void PresetArchiveDatabase::save_app_manifest_json() const
{
	/*
	[{
		"name": "Production",
		"description": "Production repository",
		"visibility": null,
		"id": "prod",
		"url": "http://10.24.3.3:8001/v1/repos/prod",
		"index_url": "http://10.24.3.3:8001/v1/repos/prod/vendor_indices.zip"
        "selected": 1
		"has_installed_printers": 1
	}, {
		"name": "Development",
		"description": "Production repository",
        "visibility": "developers only",
		"id": "dev",
		"url": "http://10.24.3.3:8001/v1/repos/dev",
		"index_url": "http://10.24.3.3:8001/v1/repos/dev/vendor_indices.zip"
        "selected": 0
        "has_installed_printers": 0
	}]
	*/
	std::string data = "[";

	for (const auto& archive : m_archive_repositories) {
		// local writes only source_path and "selected". Rest is read from zip on source_path.
		if (!archive->get_manifest().tmp_path.empty()) {
			const ArchiveRepository::RepositoryManifest& man = archive->get_manifest();
			std::string line = archive == m_archive_repositories.front() ? std::string() : ",";
			line += GUI::format(
				"{"
				"\"source_path\": \"%1%\","
				"\"selected\": %2%,"
				"\"has_installed_printers\": %3%"
				"}",
                man.source_path.generic_string()
				, is_selected(archive->get_uuid()) ? "1" : "0"
                , has_installed_printers(archive->get_uuid()) ? "1" : "0"
            );
			data += line;
			continue;
		}
		// online repo writes whole manifest - in case of offline run, this info is load from here
		const ArchiveRepository::RepositoryManifest& man = archive->get_manifest();
		std::string line = archive == m_archive_repositories.front() ? std::string() : ",";
		line += GUI::format(
			"{\"name\": \"%1%\","
			"\"description\": \"%2%\","
			"\"visibility\": \"%3%\","
			"\"id\": \"%4%\","
			"\"url\": \"%5%\","
			"\"index_url\": \"%6%\","
			"\"selected\": %7%,"
            "\"has_installed_printers\": %8%"
			"}"
			, man.name, man.description
			, man. visibility
			, man.id
			, man.url
			, man.index_url
			, is_selected(archive->get_uuid()) ? "1" : "0"
			, has_installed_printers(archive->get_uuid()) ? "1" : "0"
		);
		data += line;
	}
	data += "]";

	std::string path = get_stored_manifest_path().string();
	boost::nowide::ofstream file(path);
	if (file.is_open()) {
		file << data;
		file.close();
	} else {
		assert(false);
		BOOST_LOG_TRIVIAL(error) << "Failed to write Archive Repository Manifest to " << path;
	}
}

fs::path PresetArchiveDatabase::get_stored_manifest_path() const
{
	return (boost::filesystem::path(Slic3r::data_dir()) / "ArchiveRepositoryManifest.json").make_preferred();
}

bool PresetArchiveDatabase::is_selected(const std::string& uuid) const
{
	auto search = m_selected_repositories_uuid.find(uuid);
	assert(search != m_selected_repositories_uuid.end()); 
	return search->second;
}
bool PresetArchiveDatabase::has_installed_printers(const std::string &uuid) const 
{
    auto search = m_has_installed_printer_repositories_uuid.find(uuid);
    assert(search != m_has_installed_printer_repositories_uuid.end());
    return search->second;
}
void PresetArchiveDatabase::clear_online_repos()
{
	auto it = m_archive_repositories.begin();
	while (it != m_archive_repositories.end()) {
		// Do not clean repos with local path (local repo).
        if ((*it)->get_manifest().tmp_path.empty()) {
			it = m_archive_repositories.erase(it);
		} else {
			++it;
		}
	}
}

void PresetArchiveDatabase::read_server_manifest(const std::string& json_body)
{
	pt::ptree ptree;
	try
	{
		std::stringstream ss(json_body);
		pt::read_json(ss, ptree);
	}
	catch (const std::exception& e)
	{
		BOOST_LOG_TRIVIAL(error) << "Failed to read archives JSON. " << e.what();

	}
	// Online repo manifests are in json_body. We already have read local manifest and online manifest from last run.
	// Keep the local ones and replace the online ones but keep uuid for same id so the selected map is correct.
	// Solution: Create id - uuid translate table for online repos.
	std::map<std::string, std::string> id_to_uuid;
	for (const auto& repo_ptr : m_archive_repositories) {
		if (repo_ptr->get_manifest().source_path.empty()){
			id_to_uuid[repo_ptr->get_manifest().id] = repo_ptr->get_uuid();
		}
	}
	
	// Make a stash of secret repos that are online and has installed printers.
	// If some of these will be missing afer reading the json tree, it needs to be added back to main population.
	PrivateArchiveRepositoryVector secret_online_used_repos_cache;
    for (const auto &repo_ptr : m_archive_repositories) {
        if (repo_ptr->get_manifest().visibility.empty() || !repo_ptr->get_manifest().tmp_path.empty()) {
            continue;
		}
        const auto &it = m_has_installed_printer_repositories_uuid.find(repo_ptr->get_uuid());
        assert(it != m_has_installed_printer_repositories_uuid.end());
        if (it->second) {
            ArchiveRepository::RepositoryManifest manifest(repo_ptr->get_manifest());
            secret_online_used_repos_cache.emplace_back(std::make_unique<OnlineArchiveRepository>(repo_ptr->get_uuid(), std::move(manifest)));
		}
	}

    clear_online_repos();
	
	for (const auto& subtree : ptree) {
		ArchiveRepository::RepositoryManifest manifest;
		if (!extract_repository_header(subtree.second, manifest)) {
			assert(false);
			BOOST_LOG_TRIVIAL(error) << "Failed to read one of repository headers.";
			continue;
		}
		auto id_it = id_to_uuid.find(manifest.id);
		std::string uuid = (id_it == id_to_uuid.end() ? get_next_uuid() : id_it->second);
		// Set default selected value to true - its a never before seen repository
		if (auto search = m_selected_repositories_uuid.find(uuid); search == m_selected_repositories_uuid.end()) {
			m_selected_repositories_uuid[uuid] = true;
		}
        // Set default "has installed printers" value to false - its a never before seen repository
        if (auto search = m_has_installed_printer_repositories_uuid.find(uuid);
            search == m_has_installed_printer_repositories_uuid.end()) {
            m_has_installed_printer_repositories_uuid[uuid] = false;
        }
		m_archive_repositories.emplace_back(std::make_unique<OnlineArchiveRepository>(uuid, std::move(manifest)));
	}
	
	// return missing secret online repos with installed printers to the vector
	for (const auto &repo_ptr : secret_online_used_repos_cache) {
        std::string uuid = repo_ptr->get_uuid();
        if (std::find_if(
                m_archive_repositories.begin(), m_archive_repositories.end(),
                [uuid](const std::unique_ptr<ArchiveRepository> &ptr) {
                    return ptr->get_uuid() == uuid;
                }
            ) == m_archive_repositories.end())
		{
            ArchiveRepository::RepositoryManifest manifest(repo_ptr->get_manifest());
            m_archive_repositories.emplace_back(std::make_unique<OnlineArchiveRepository>(repo_ptr->get_uuid(), std::move(manifest)));
	    }
	}

	consolidate_uuid_maps();
	save_app_manifest_json();
}

SharedArchiveRepositoryVector PresetArchiveDatabase::get_all_archive_repositories() const 
{
    SharedArchiveRepositoryVector result;
    result.reserve(m_archive_repositories.size());
    for (const auto &repo_ptr : m_archive_repositories) 
    {
        result.emplace_back(repo_ptr.get());
    }
    return result;
}

SharedArchiveRepositoryVector PresetArchiveDatabase::get_selected_archive_repositories() const 
{
    SharedArchiveRepositoryVector result;
    result.reserve(m_archive_repositories.size());
    for (const auto &repo_ptr : m_archive_repositories) 
    {
        auto it = m_selected_repositories_uuid.find(repo_ptr->get_uuid());
        assert(it != m_selected_repositories_uuid.end());
        if (it->second) {
            result.emplace_back(repo_ptr.get());
        }   
    }
    return result;
}

bool PresetArchiveDatabase::is_selected_repository_by_uuid(const std::string& uuid) const
{
	auto selected_it = m_selected_repositories_uuid.find(uuid);
	assert(selected_it != m_selected_repositories_uuid.end());
	return selected_it->second;
}
bool PresetArchiveDatabase::is_selected_repository_by_id(const std::string& repo_id) const
{
	assert(!repo_id.empty());
	for (const auto& repo_ptr : m_archive_repositories) {
		if (repo_ptr->get_manifest().id == repo_id) {
			return true;
		}
	}
	return false;
}
void PresetArchiveDatabase::consolidate_uuid_maps()
{
	//std::vector<std::unique_ptr<ArchiveRepository>> m_archive_repositories;
	//std::map<std::string, bool> m_selected_repositories_uuid;
	auto selected_it = m_selected_repositories_uuid.begin();
    while (selected_it != m_selected_repositories_uuid.end()) {
		bool found = false;
		for (const auto& repo_ptr : m_archive_repositories) {
            if (repo_ptr->get_uuid() == selected_it->first) {
				found = true;
				break;	 
			}
		}
		if (!found) {
            selected_it = m_selected_repositories_uuid.erase(selected_it);
		} else {
            ++selected_it;
		}
	}
	// Do the same for m_has_installed_printer_repositories_uuid
    auto installed_it = m_has_installed_printer_repositories_uuid.begin();
    while (installed_it != m_has_installed_printer_repositories_uuid.end()) {
        bool found = false;
        for (const auto &repo_ptr : m_archive_repositories) {
            if (repo_ptr->get_uuid() == installed_it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            installed_it = m_has_installed_printer_repositories_uuid.erase(installed_it);
        } else {
            ++installed_it;
        }
    }
}

std::string PresetArchiveDatabase::get_next_uuid()
{
	boost::uuids::uuid uuid = m_uuid_generator();
	return boost::uuids::to_string(uuid);
}

namespace {
bool sync_inner(std::string& manifest, PresetUpdaterUIStatus* ui_status)
{
	bool ret = false;
    std::string url = Utils::ServiceConfig::instance().preset_repo_repos_url();
    auto http = Http::get(std::move(url));
    if (!add_authorization_header(http))
        return false;
    http
		.timeout_max(30)
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			BOOST_LOG_TRIVIAL(error) << "Failed to get online archive source manifests: "<< body << " ; " << error << " ; " << http_status;
            ui_status->set_error(error);
			ret = false;
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			manifest = body;
			ret = true;
		})
        .on_retry([&](int attempt, unsigned delay) {
            return !ui_status->on_attempt(attempt, delay);
		})
		.perform_sync(ui_status->get_retry_policy());
	return ret;
}
}

bool PresetArchiveDatabase::sync_blocking(PresetUpdaterUIStatus* ui_status)
{
    assert(ui_status);
	std::string manifest;
    bool sync_res = false;
    ui_status->set_target("Archive Database Mainfest");
    sync_res = sync_inner(manifest, ui_status);
    if (!sync_res) {
        return false;
    }    
	read_server_manifest(std::move(manifest));
    return true;
}

} // Slic3r
