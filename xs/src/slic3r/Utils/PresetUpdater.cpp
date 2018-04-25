#include "PresetUpdater.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/event.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;


namespace Slic3r {


enum {
	SLIC3R_VERSION_BODY_MAX = 256,
};

static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".download";


struct Update
{
	fs::path source;
	fs::path target;
	Version version;

	Update(fs::path &&source, fs::path &&target, const Version &version) :
		source(std::move(source)),
		target(std::move(target)),
		version(version)
	{}

	std::string name() const { return source.stem().string(); }

	friend std::ostream& operator<<(std::ostream& os , const Update &self) {
		os << "Update(" << self.source.string() << " -> " << self.target.string() << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;

	Incompat(fs::path &&bundle, const Version &version) :
		bundle(std::move(bundle)),
		version(version)
	{}

	std::string name() const { return bundle.stem().string(); }

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};


struct PresetUpdater::priv
{
	int version_online_event;
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;
	bool had_config_update;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	priv(int version_online_event);

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	void prune_tmps() const;
	void sync_version() const;
	void sync_config(const std::set<VendorProfile> vendors) const;

	void check_install_indices() const;
	Updates get_config_updates() const;
	void perform_updates(Updates &&updates, bool snapshot = true) const;
};

PresetUpdater::priv::priv(int version_online_event) :
	version_online_event(version_online_event),
	had_config_update(false),
	cache_path(fs::path(Slic3r::data_dir()) / "cache"),
	rsrc_path(fs::path(resources_dir()) / "profiles"),
	vendor_path(fs::path(Slic3r::data_dir()) / "vendor"),
	cancel(false)
{
	set_download_prefs(GUI::get_app_config());
	check_install_indices();
	index_db = std::move(Index::load_db());
}

// Pull relevant preferences from AppConfig
void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	enabled_version_check = app_config->get("version_check") == "1";
	version_check_url = app_config->get("version_check_url");
	enabled_config_update = app_config->get("preset_update") == "1";
}

// Downloads a file (http get operation). Cancels if the Updater is being destroyed.
bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
	bool res = false;
	fs::path tmp_path = target_path;
	tmp_path += (boost::format(".%1%%2%") % get_current_pid() % TMP_EXTENSION).str();

	BOOST_LOG_TRIVIAL(info) << boost::format("Get: `%1%`\n\t-> `%2%`\n\tvia tmp path `%3%`")
		% url
		% target_path.string()
		% tmp_path.string();

	Http::get(url)
		.on_progress([this](Http::Progress, bool &cancel) {
			if (cancel) { cancel = true; }
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << boost::format("Error getting: `%1%`: HTTP %2%, %3%")
				% url
				% http_status
				% error;
		})
		.on_complete([&](std::string body, unsigned http_status) {
			fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
			file.write(body.c_str(), body.size());
			file.close();
			fs::rename(tmp_path, target_path);
			res = true;
		})
		.perform_sync();

	return res;
}

// Remove leftover paritally downloaded files, if any.
void PresetUpdater::priv::prune_tmps() const
{
	for (fs::directory_iterator it(cache_path); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == TMP_EXTENSION) {
			BOOST_LOG_TRIVIAL(debug) << "Cache prune: " << it->path().string();
			fs::remove(it->path());
		}
	}
}

// Get Slic3rPE version available online, save in AppConfig.
void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

	BOOST_LOG_TRIVIAL(info) << boost::format("Downloading Slic3rPE online version from: `%1%`") % version_check_url;

	Http::get(version_check_url)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << boost::format("Error getting: `%1%`: HTTP %2%, %3%")
				% version_check_url
				% http_status
				% error;
		})
		.on_complete([&](std::string body, unsigned http_status) {
			boost::trim(body);
			BOOST_LOG_TRIVIAL(info) << boost::format("Got Slic3rPE online version: `%1%`. Sending to GUI thread...") % body;
			wxCommandEvent* evt = new wxCommandEvent(version_online_event);
			evt->SetString(body);
			GUI::get_app()->QueueEvent(evt);
		})
		.perform_sync();
}

// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
void PresetUpdater::priv::sync_config(const std::set<VendorProfile> vendors) const
{
	BOOST_LOG_TRIVIAL(info) << "Syncing configuration cache";

	if (!enabled_config_update) { return; }

	// Donwload vendor preset bundles
	for (const auto &index : index_db) {
		if (cancel) { return; }

		const auto vendor_it = vendors.find(VendorProfile(index.vendor()));
		if (vendor_it == vendors.end()) {
			BOOST_LOG_TRIVIAL(warning) << "No such vendor: " << index.vendor();
			continue;
		}

		const VendorProfile &vendor = *vendor_it;
		if (vendor.config_update_url.empty()) {
			BOOST_LOG_TRIVIAL(info) << "Vendor has no config_update_url: " << vendor.name;
			continue;
		}

		// Download a fresh index
		BOOST_LOG_TRIVIAL(info) << "Downloading index for vendor: " << vendor.name;
		const auto idx_url = vendor.config_update_url + "/" + INDEX_FILENAME;
		const auto idx_path = cache_path / (vendor.id + ".idx");
		if (! get_file(idx_url, idx_path)) { continue; }
		if (cancel) { return; }

		// Load the fresh index up
		Index new_index;
		new_index.load(idx_path);

		// See if a there's a new version to download
		const auto recommended_it = new_index.recommended();
		if (recommended_it == new_index.end()) {
			BOOST_LOG_TRIVIAL(error) << "No recommended version for vendor: " << vendor.name << ", invalid index?";
			continue;
		}
		const auto recommended = recommended_it->config_version;

		BOOST_LOG_TRIVIAL(debug) << boost::format("New index for vendor: %1%: current version: %2%, recommended version: %3%")
			% vendor.name
			% vendor.config_version.to_string()
			% recommended.to_string();

		if (vendor.config_version >= recommended) { continue; }

		// Download a fresh bundle
		BOOST_LOG_TRIVIAL(info) << "Downloading new bundle for vendor: " << vendor.name;
		const auto bundle_url = (boost::format("%1%/%2%.ini") % vendor.config_update_url % recommended.to_string()).str();
		const auto bundle_path = cache_path / (vendor.id + ".ini");
		if (! get_file(bundle_url, bundle_path)) { continue; }
		if (cancel) { return; }
	}
}

// Install indicies from resources. Only installs those that are either missing or older than in resources.
void PresetUpdater::priv::check_install_indices() const
{
	BOOST_LOG_TRIVIAL(info) << "Checking if indices need to be installed from resources...";

	for (fs::directory_iterator it(rsrc_path); it != fs::directory_iterator(); ++it) {
		const auto &path = it->path();
		if (path.extension() == ".idx") {
			const auto path_in_cache = cache_path / path.filename();

			if (! fs::exists(path_in_cache)) {
				BOOST_LOG_TRIVIAL(info) << "Install index from resources: " << path.filename();
				fs::copy_file(path, path_in_cache, fs::copy_option::overwrite_if_exists);
			} else {
				Index idx_rsrc, idx_cache;
				idx_rsrc.load(path);
				idx_cache.load(path_in_cache);

				if (idx_cache.version() < idx_rsrc.version()) {
					BOOST_LOG_TRIVIAL(info) << "Update index from resources: " << path.filename();
					fs::copy_file(path, path_in_cache, fs::copy_option::overwrite_if_exists);
				}
			}
		}
	}
}

// Generates a list of bundle updates that are to be performed
Updates PresetUpdater::priv::get_config_updates() const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << "Checking for cached configuration updates...";

	for (const auto idx : index_db) {
		auto bundle_path = vendor_path / (idx.vendor() + ".ini");

		if (! fs::exists(bundle_path)) {
			BOOST_LOG_TRIVIAL(info) << "Bundle not present for index, skipping: " << idx.vendor();
			continue;
		}

		// Perform a basic load and check the version
		const auto vp = VendorProfile::from_ini(bundle_path, false);

		const auto ver_current = idx.find(vp.config_version);
		if (ver_current == idx.end()) {
			BOOST_LOG_TRIVIAL(error) << boost::format("Preset bundle (`%1%`) version not found in index: %2%") % idx.vendor() % vp.config_version.to_string();
			continue;
		}

		const auto recommended = idx.recommended();
		if (recommended == idx.end()) {
			throw std::runtime_error((boost::format("Invalid index: `%1%`") % idx.vendor()).str());
		}

		BOOST_LOG_TRIVIAL(debug) << boost::format("Vendor: %1%, version installed: %2%, version cached: %3%")
			% vp.name
			% ver_current->config_version.to_string()
			% recommended->config_version.to_string();

		if (! ver_current->is_current_slic3r_supported()) {
			BOOST_LOG_TRIVIAL(warning) << "Current Slic3r incompatible with installed bundle: " << bundle_path.string();
			updates.incompats.emplace_back(std::move(bundle_path), *ver_current);
		} else if (recommended->config_version > ver_current->config_version) {
			// Config bundle update situation

			// Check if the update is already present in a snapshot
			const auto recommended_snap = SnapshotDB::singleton().snapshot_with_vendor_preset(vp.name, recommended->config_version);
			if (recommended_snap != SnapshotDB::singleton().end()) {
				BOOST_LOG_TRIVIAL(info) << boost::format("Bundle update %1% %2% already found in snapshot %3%, skipping...")
					% vp.name
					% recommended->config_version.to_string()
					% recommended_snap->id;
				continue;
			}

			auto path_in_cache = cache_path / (idx.vendor() + ".ini");
			if (! fs::exists(path_in_cache)) {
				BOOST_LOG_TRIVIAL(warning) << "Index indicates update, but new bundle not found in cache: " << path_in_cache.string();
				continue;
			}

			const auto cached_vp = VendorProfile::from_ini(path_in_cache, false);
			if (cached_vp.config_version == recommended->config_version) {
				updates.updates.emplace_back(std::move(path_in_cache), std::move(bundle_path), *recommended);
			}
		}
	}

	return updates;
}

void PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
	if (updates.incompats.size() > 0) {
		if (snapshot) {
			BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
			SnapshotDB::singleton().take_snapshot(*GUI::get_app_config(), Snapshot::SNAPSHOT_DOWNGRADE);
		}

		BOOST_LOG_TRIVIAL(info) << boost::format("Deleting %1% incompatible bundles") % updates.incompats.size();

		for (const auto &incompat : updates.incompats) {
			BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
			fs::remove(incompat.bundle);
		}
	}
	else if (updates.updates.size() > 0) {
		if (snapshot) {
			BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
			SnapshotDB::singleton().take_snapshot(*GUI::get_app_config(), Snapshot::SNAPSHOT_UPGRADE);
		}

		BOOST_LOG_TRIVIAL(info) << boost::format("Performing %1% updates") % updates.updates.size();

		for (const auto &update : updates.updates) {
			BOOST_LOG_TRIVIAL(info) << '\t' << update;

			fs::copy_file(update.source, update.target, fs::copy_option::overwrite_if_exists);

			PresetBundle bundle;
			bundle.load_configbundle(update.target.string(), PresetBundle::LOAD_CFGBNDLE_SYSTEM);

			auto preset_remover = [](const Preset &preset) {
				fs::remove(preset.file);
			};

			for (const auto &preset : bundle.prints)    { preset_remover(preset); }
			for (const auto &preset : bundle.filaments) { preset_remover(preset); }
			for (const auto &preset : bundle.printers)  { preset_remover(preset); }
		}
	}
}


PresetUpdater::PresetUpdater(int version_online_event) :
	p(new priv(version_online_event))
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->cancel = true;
		p->thread.join();
	}
}

void PresetUpdater::sync(PresetBundle *preset_bundle)
{
	p->set_download_prefs(GUI::get_app_config());
	if (!p->enabled_version_check && !p->enabled_config_update) { return; }

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
	std::set<VendorProfile> vendors = preset_bundle->vendors;

	p->thread = std::move(std::thread([this, vendors]() {
		this->p->prune_tmps();
		this->p->sync_version();
		this->p->sync_config(std::move(vendors));
	}));
}

void PresetUpdater::slic3r_update_notify()
{
	if (! p->enabled_version_check) { return; }

	if (p->had_config_update) {
		BOOST_LOG_TRIVIAL(info) << "New Slic3r version available, but there was a configuration update, notification won't be displayed";
		return;
	}

	auto* app_config = GUI::get_app_config();
	const auto ver_slic3r = Semver::parse(SLIC3R_VERSION);
	const auto ver_online_str = app_config->get("version_online");
	const auto ver_online = Semver::parse(ver_online_str);
	const auto ver_online_seen = Semver::parse(app_config->get("version_online_seen"));
	if (! ver_slic3r) {
		throw std::runtime_error("Could not parse Slic3r version string: " SLIC3R_VERSION);
	}

	if (ver_online) {
		// Only display the notification if the version available online is newer AND if we haven't seen it before
		if (*ver_online > *ver_slic3r && (! ver_online_seen || *ver_online_seen < *ver_online)) {
			GUI::MsgUpdateSlic3r notification(*ver_slic3r, *ver_online);
			notification.ShowModal();
			if (notification.disable_version_check()) {
				app_config->set("version_check", "0");
				p->enabled_version_check = false;
			}
		}
		app_config->set("version_online_seen", ver_online_str);
	}
}

bool PresetUpdater::config_update() const
{
	if (! p->enabled_config_update) { return true; }

	auto updates = p->get_config_updates();
	if (updates.incompats.size() > 0) {
		BOOST_LOG_TRIVIAL(info) << boost::format("%1% bundles incompatible. Asking for action...") % updates.incompats.size();

		std::unordered_map<std::string, wxString> incompats_map;
		for (const auto &incompat : updates.incompats) {
			auto vendor = incompat.name();
			auto restrictions = wxString::Format(_(L("requires min. %s and max. %s")),
				incompat.version.min_slic3r_version.to_string(),
				incompat.version.max_slic3r_version.to_string()
			);
			incompats_map.emplace(std::make_pair(std::move(vendor), std::move(restrictions)));
		}

		GUI::MsgDataIncompatible dlg(std::move(incompats_map));
		const auto res = dlg.ShowModal();
		if (res == wxID_REPLACE) {
			BOOST_LOG_TRIVIAL(info) << "User wants to re-configure...";
			p->perform_updates(std::move(updates));
			GUI::ConfigWizard wizard(nullptr, GUI::ConfigWizard::RR_DATA_INCOMPAT);
			if (wizard.run(GUI::get_preset_bundle(), this)) {
				p->had_config_update = true;
			} else {
				return false;
			}
		} else {
			BOOST_LOG_TRIVIAL(info) << "User wants to exit Slic3r, bye...";
			return false;
		}
	}
	else if (updates.updates.size() > 0) {
		BOOST_LOG_TRIVIAL(info) << boost::format("Update of %1% bundles available. Asking for confirmation ...") % updates.updates.size();

		std::unordered_map<std::string, std::string> updates_map;
		for (const auto &update : updates.updates) {
			auto vendor = update.name();
			auto ver_str = update.version.config_version.to_string();
			if (! update.version.comment.empty()) {
				ver_str += std::string(" (") + update.version.comment + ")";
			}
			updates_map.emplace(std::make_pair(std::move(vendor), std::move(ver_str)));
		}

		GUI::MsgUpdateConfig dlg(std::move(updates_map));

		const auto res = dlg.ShowModal();
		if (res == wxID_YES) {
			BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
			p->perform_updates(std::move(updates));
		} else {
			BOOST_LOG_TRIVIAL(info) << "User refused the update";
		}

		p->had_config_update = true;
	} else {
		BOOST_LOG_TRIVIAL(info) << "No configuration updates available.";
	}

	return true;
}

void PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << boost::format("Installing %1% bundles from resources ...") % bundles.size();

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = p->rsrc_path / bundle;
		auto path_in_vendors = p->vendor_path / bundle;
		updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version());
	}

	p->perform_updates(std::move(updates), snapshot);
}


}
