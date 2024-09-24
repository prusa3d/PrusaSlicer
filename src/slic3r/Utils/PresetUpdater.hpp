///|/ Copyright (c) Prusa Research 2018 - 2023 David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include "slic3r/GUI/PresetArchiveDatabase.hpp"

#include <memory>
#include <vector>

#include <wx/event.h>

namespace Slic3r {


class AppConfig;
class PresetBundle;
class Semver;

typedef std::vector<const GUI::ArchiveRepository*> SharedArchiveRepositoryVector;

static constexpr const int SLIC3R_VERSION_BODY_MAX = 256;

class PresetUpdater
{
public:
	PresetUpdater();
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	// If either version check or config updating is enabled, get the appropriate data in the background and cache it.
	void sync(const PresetBundle *preset_bundle, wxEvtHandler* evt_handler, SharedArchiveRepositoryVector&& repositories);
	void cancel_sync();

	void sync_blocking(const PresetBundle* preset_bundle, wxEvtHandler* evt_handler, const SharedArchiveRepositoryVector& repositories);

	// If version check is enabled, check if chaced online slic3r version is newer, notify if so.
	void slic3r_update_notify();

	enum UpdateResult {
		R_NOOP,
		R_INCOMPAT_EXIT,
		R_INCOMPAT_CONFIGURED,
		R_UPDATE_INSTALLED,
		R_UPDATE_REJECT,
		R_UPDATE_NOTIFICATION,
		R_ALL_CANCELED
	};

	enum class UpdateParams {
		SHOW_TEXT_BOX,				// force modal textbox
		SHOW_NOTIFICATION,			// only shows notification
		FORCED_BEFORE_WIZARD		// indicates that check of updated is forced before ConfigWizard opening
	};

	// If updating is enabled, check if updates are available in cache, if so, ask about installation.
	// A false return value implies Slic3r should exit due to incompatibility of configuration.
	// Providing old slic3r version upgrade profiles on upgrade of an application even in case
	// that the config index installed from the Internet is equal to the index contained in the installation package.
	UpdateResult config_update(const Semver &old_slic3r_version, UpdateParams params, const SharedArchiveRepositoryVector& repositories) const;
	
	void update_index_db();

	// "Update" a list of bundles from resources or cache/vendor (behaves like an online update).
	bool install_bundles_rsrc_or_cache_vendor(std::vector<std::string> bundles, const SharedArchiveRepositoryVector& repositories, bool snapshot = true) const;

	void on_update_notification_confirm(const SharedArchiveRepositoryVector& repositories);

	bool version_check_enabled() const;

	void add_additional_archive(const std::string& archive_url, const std::string& download_url);
	void add_additional_archives(const std::vector<std::pair<std::string, std::string>>& archives);
private:
	struct priv;
	std::unique_ptr<priv> p;

	std::vector<std::pair<std::string, std::string>> m_additional_archives;
};

//wxDECLARE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
//wxDECLARE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);
}
#endif
