#pragma once

#include "Slic3r/App/PresetUpdater/PresetUpdaterActivityReporter.hpp"
#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Semver.hpp"

#include <boost/filesystem/path.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::Biz::Platform {
class TimerQueue;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::App::PresetUpdater {

using Biz::PresetUpdater::JobId;
using Biz::PresetUpdater::VendorReconfigurationState;

// ---------------------------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------------------------

struct VendorKey
{
    std::string repo_id;
    std::string vendor_id;

    auto operator<=>(const VendorKey&) const = default;
};

// ---------------------------------------------------------------------------------------------
// View projections
//
// Everything below is derived. It is produced wholesale by SourceStore::publish and is never
// edited in place, which is what keeps a derived value from outliving the truth it came from.
// ---------------------------------------------------------------------------------------------

enum class InstallState
{
    Idle,
    Queued,
    Running,
    Done,
    Failed
};

struct VendorRowState
{
    std::string repo_id;
    std::string vendor_id;
    std::string comment;
    VendorReconfigurationState state{VendorReconfigurationState::Update};
    Slic3r::Semver current_version;
    Slic3r::Semver recommended_version;

    InstallState install_state{InstallState::Idle};
    std::string error_text;

    bool install_locked{false};
    bool skipped{false};

    bool operator==(const VendorRowState&) const = default;
};

using VendorRowList = Biz::ObservableList<VendorRowState>;

struct SourceCounts
{
    int updates{0};
    int new_vendors{0};
    int required{0};
    int unknown_versions{0};

    int pending() const
    {
        return updates + new_vendors + required + unknown_versions;
    }

    /// What cannot be left alone, because those profiles are unusable until they are changed.
    int attention() const
    {
        return required + unknown_versions;
    }

    bool operator==(const SourceCounts&) const = default;
};

struct SourceRowState
{
    enum class UpdateState
    {
        NotChecked,
        Waiting,
        Checking,
        Installing,
        UpToDate,
        HasUpdates
    };

    std::string uuid;
    std::string id;
    std::string name;
    std::string description;
    std::string visibility;
    boost::filesystem::path zip_path; ///< Non-empty for a local (offline) source.

    bool selected{false};
    bool required{false}; ///< Held by a forced reconfiguration, so it is checked either way.
    bool selection_locked{false};
    bool install_locked{false};

    UpdateState update_state{UpdateState::NotChecked};
    SourceCounts counts;
    std::vector<std::string> skipped_vendors;
    bool check_failed{false};

    /// Stable for the lifetime of the source, so a row view may hold a weak reference to it.
    std::shared_ptr<VendorRowList> vendors;

    bool operator==(const SourceRowState&) const = default;
};

using SourceRowList = Biz::ObservableList<SourceRowState>;

// ---------------------------------------------------------------------------------------------
// Job ledger
// ---------------------------------------------------------------------------------------------

enum class JobKind
{
    ForcedCheck,
    Listing,
    Check,
    Selection,
    Install,
    AddSource,
    RemoveSource
};

/**
 * @brief Remembers what every submitted job was for.
 *
 * Two questions have to be answerable when a result arrives, and neither can be answered from the
 * payload: what did this job set out to do, and is it still the newest job of its kind? Anything
 * that is not newest is a superseded job reporting late, and its result is dropped rather than
 * written over fresher state.
 */
class JobLedger
{
public:
    struct Record
    {
        JobKind kind{JobKind::Check};
        std::string subject; ///< What the job was about, for an error message that can name it.
        std::vector<VendorKey> keys; ///< Install jobs only.
    };

    /// @return false for k_invalid_job_id, which means the interactor refused the work.
    bool track(JobId job_id, JobKind kind, std::string subject = {});

    void set_install_keys(JobId job_id, std::vector<VendorKey> keys);

    const Record* find(JobId job_id) const;

    /// The newest tracked job of this kind, or k_invalid_job_id.
    JobId newest(JobKind kind) const;

    bool is_newest(JobId job_id) const;

    bool any_pending(JobKind kind) const;

    void forget(JobId job_id);

    void clear();

private:
    std::map<JobId, Record> m_records;
    std::map<JobKind, JobId> m_newest;
};

// ---------------------------------------------------------------------------------------------
// Forced reconfigurations
// ---------------------------------------------------------------------------------------------

/**
 * @brief The set of vendors whose installed profiles cannot be used until they are changed.
 *
 * A check may only report on the sources it actually evaluated, so it may add to this set and may
 * clear entries it has evidence for, but silence about a source is never taken as proof that the
 * source is fine. Anything else lets an offline, skipped or failed check release the application
 * from a forced reconfiguration it never resolved.
 */
class ForcedReconfigurations
{
public:
    void reset(const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations);

    /**
     * @param evaluated_repos the sources this check ran over. A required vendor of one of these
     * that the check did not report is taken as resolved; every other entry is kept.
     * @param unjudged vendors the check could not reach a verdict on, kept whatever the rest
     * of it says.
     */
    void reconcile(
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::set<std::string>& evaluated_repos,
        const std::vector<VendorKey>& unjudged
    );

    void mark_satisfied(const VendorKey& key);

    void clear();

    /**
     * @brief Whether a forced reconfiguration is holding the application. Presets this
     * application cannot use are unusable whether their source is selected or not, so only a
     * check or a finished install releases the hold.
     */
    bool active() const;

    bool requires_vendor(const VendorKey& key) const;

    const std::vector<VendorKey>& required() const;

    /// The sources the required vendors came from, each named once.
    std::set<std::string> required_repo_ids() const;

private:
    std::vector<VendorKey> m_required;
};

// ---------------------------------------------------------------------------------------------
// Selection committer
// ---------------------------------------------------------------------------------------------

/**
 * @brief Debounces source selection changes into one write of the app manifest.
 *
 * Owns the whole debounce: nothing outside it needs to know about generations, arming, or the
 * commit that has to follow one already in flight. A queued timer holds a weak reference, so the
 * committer may be destroyed while a timer is still on its way.
 */
class SelectionCommitter
{
public:
    using CommitFn = std::function<void()>;

    SelectionCommitter(Biz::Platform::TimerQueue& timer_queue, CommitFn commit);
    ~SelectionCommitter();

    SelectionCommitter(const SelectionCommitter&)            = delete;
    SelectionCommitter& operator=(const SelectionCommitter&) = delete;

    /// Something changed. Restarts the debounce.
    void touch();

    /// Commits now if anything is waiting. For the moment the dialog closes.
    void flush();

    /// Called when a commit finishes, so a change made while it ran is not lost.
    void on_commit_finished();

    void set_commit_running(bool running);

    /**
     * @brief The commit did not reach the disk after all, so the edit is still owed one.
     *
     * Deliberately does not restart the timer: a refusal that repeats would otherwise retry for
     * ever. The edit is written by the next one, or by the flush when the dialog closes.
     */
    void mark_unwritten();

    void cancel();

    bool pending() const;

private:
    void fire();

    Biz::Platform::TimerQueue& m_timer_queue;
    CommitFn m_commit;

    std::shared_ptr<int> m_life{std::make_shared<int>(0)};
    uint64_t m_generation{0};
    bool m_armed{false};
    bool m_dirty{false};
    bool m_running{false};
};

// ---------------------------------------------------------------------------------------------
// Source store
// ---------------------------------------------------------------------------------------------

/**
 * @brief The truth about sources and vendors, and the only writer of the two published lists.
 *
 * Callers change the authoritative entries through named operations and then publish once. The
 * published rows are recomputed from scratch and diffed, so a listener is notified for a row that
 * really changed and for no other, and no derived field can be left behind by a partial update.
 * Every query is const, so reading state can never disturb it.
 */
class SourceStore
{
public:
    struct VendorReport
    {
        std::string vendor_id;
        std::string comment;
        VendorReconfigurationState state{VendorReconfigurationState::Update};
        Slic3r::Semver current_version;
        Slic3r::Semver recommended_version;
        bool skipped{false};
    };

    /// What one check said, sorted by source. Built once, applied once.
    struct CheckOutcome
    {
        std::map<std::string, std::vector<VendorReport>> by_repo;
        std::map<std::string, std::set<std::string>> skipped_by_repo;
        std::set<std::string> failed_repos;

        static CheckOutcome from(
            const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
            const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
        );

        /// The vendors this check warned about, which is not the same as finding them fine.
        std::vector<VendorKey> unjudged_keys() const;
    };

    struct InstallRequest
    {
        Biz::PresetUpdater::PresetUpdaterReconfigurationList list;
        /// The subset of the requested keys that exists and can be installed.
        std::vector<VendorKey> keys;
    };

    struct Locks
    {
        bool install_locked{false};
        std::set<std::string> locked_selection_ids;
    };

    enum class RepoVisibility
    {
        Unknown, ///< Nothing has been listed yet, so the source cannot be judged.
        Selected,
        Required, ///< Not selected, but a forced reconfiguration needs it.
        Deselected
    };

    struct RepoLookup
    {
        RepoVisibility visibility{RepoVisibility::Unknown};
        std::string name;
    };

    // -- queries ------------------------------------------------------------------------------

    bool listed() const;

    bool has_selected_with_id(const std::string& id) const;

    /// An active source has no result yet, so a check is owed.
    bool needs_check() const;

    bool every_active_checked() const;

    bool any_check_failed() const;

    std::set<std::string> installing_ids() const;

    bool selection_locked(const std::string& uuid) const;

    std::string name_of(const std::string& uuid) const;

    RepoLookup lookup_repo(const std::string& repo_id) const;

    std::vector<VendorKey> actionable_keys(bool required_only) const;

    std::vector<VendorKey> actionable_keys_of_source(
        const std::string& uuid, bool required_only
    ) const;

    bool any_actionable(bool required_only) const;

    InstallRequest make_install_request(const std::vector<VendorKey>& keys) const;

    std::vector<PresetUpdaterActivityReporter::InstalledVendor> installed_vendors_for(
        const std::vector<VendorKey>& keys
    ) const;

    Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector selection() const;

    // -- authoritative changes ----------------------------------------------------------------

    /**
     * @brief Replaces the known sources, keeping the check results of the ones that survive.
     * @param keep_selection_edits a selection the user has changed but not committed yet wins
     * over the descriptor, so a listing landing mid-edit does not undo the edit.
     */
    void reset_from(
        const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
        bool keep_selection_edits
    );

    /// @return false when the source is unknown or an install holds its selection.
    bool set_selected(const std::string& uuid, bool selected);

    /**
     * @brief Names the sources a forced reconfiguration needs, which are checked and installed
     * like selected ones. Being required is not being selected and is never written back.
     */
    void set_required_repos(const std::set<std::string>& repo_ids);

    /// Drops every check result. An install still running is kept.
    void forget_results();

    void begin_check();

    /// @return the sources this check evaluated, which is what makes its silence meaningful.
    std::set<std::string> apply_check(const CheckOutcome& outcome);

    /// The check itself failed or was dropped, as opposed to failing for one source.
    void abandon_check();

    void set_install_state(const std::vector<VendorKey>& keys, InstallState state);

    void set_install_failed(const VendorKey& key, const std::string& error_text);

    void set_install_done(const VendorKey& key);

    /// The install job ended without a verdict for these vendors.
    void abandon_install(const std::vector<VendorKey>& keys, bool canceled);

    void promote_queued(const std::vector<VendorKey>& keys);

    // -- publishing ---------------------------------------------------------------------------

    /// @return true when anything the views observe actually changed.
    bool publish(const Locks& locks);

    Biz::IObservableList<SourceRowState>& online_rows();
    Biz::IObservableList<SourceRowState>& local_rows();

private:
    enum class CheckPhase
    {
        NotChecked,
        Waiting,
        Checking,
        Rechecking, ///< Being checked again, still showing the result of the previous check.
        Checked,
        Failed
    };

    struct VendorEntry
    {
        std::string vendor_id;
        std::string comment;
        VendorReconfigurationState state{VendorReconfigurationState::Update};
        Slic3r::Semver current_version;
        Slic3r::Semver recommended_version;
        bool skipped{false};

        InstallState install_state{InstallState::Idle};
        std::string error_text;
    };

    struct SourceEntry
    {
        Biz::PresetUpdater::PresetUpdaterRepositoryDescriptor descriptor;
        bool selected{false};
        bool required{false};

        CheckPhase phase{CheckPhase::NotChecked};
        std::vector<std::string> skipped_vendors;
        std::vector<VendorEntry> vendors;

        std::shared_ptr<VendorRowList> vendor_rows{std::make_shared<VendorRowList>()};
    };

    /**
     * @brief One source entry, its last published projection, and proof of whether they agree.
     *
     * The entry is only reachable for writing through write(), which counts the change. Publishing
     * can therefore tell a source that moved from one that did not without comparing them, and
     * rebuild only what moved. A count that runs ahead of reality costs one wasted projection and
     * nothing else - the row comparison in publish_rows stays the authority on what is announced.
     */
    class Slot
    {
    public:
        explicit Slot(SourceEntry entry) : m_entry(std::move(entry)) {}

        const SourceEntry& read() const
        {
            return m_entry;
        }

        SourceEntry& write()
        {
            ++m_revision;
            return m_entry;
        }

        bool needs_projection(bool install_locked, bool selection_locked) const
        {
            return m_projected_revision != m_revision
                || m_projected_install_locked != install_locked
                || m_projected_selection_locked != selection_locked;
        }

        void store_projection(SourceRowState row, bool install_locked, bool selection_locked)
        {
            m_row                        = std::move(row);
            m_projected_revision         = m_revision;
            m_projected_install_locked   = install_locked;
            m_projected_selection_locked = selection_locked;
        }

        const SourceRowState& row() const
        {
            return m_row;
        }

    private:
        SourceEntry m_entry;
        SourceRowState m_row;

        uint64_t m_revision{1};
        uint64_t m_projected_revision{0};
        bool m_projected_install_locked{false};
        bool m_projected_selection_locked{false};
    };

    Slot* find_slot(const std::string& uuid);
    const Slot* find_slot(const std::string& uuid) const;
    Slot* find_active_slot_by_repo(const std::string& repo_id);
    const Slot* find_active_slot_by_repo(const std::string& repo_id) const;

    static VendorEntry* find_vendor_in(SourceEntry& entry, const std::string& vendor_id);
    static const VendorEntry* find_vendor_in(
        const SourceEntry& entry, const std::string& vendor_id
    );

    VendorEntry* find_vendor(const VendorKey& key);
    const VendorEntry* find_vendor(const VendorKey& key) const;

    static void merge_vendors(SourceEntry& entry, const std::vector<VendorReport>& incoming);

    static bool active(const SourceEntry& entry);

    void apply_required_flags();

    static SourceCounts counts_of(const SourceEntry& entry);
    static bool installing(const SourceEntry& entry);
    static SourceRowState::UpdateState display_state_of(const SourceEntry& entry);
    static std::vector<VendorKey> actionable_of(const SourceEntry& entry, bool required_only);
    static CheckPhase next_check_phase(CheckPhase current);

    static SourceRowState project(
        const SourceEntry& entry, bool install_locked, bool selection_locked
    );
    static bool publish_vendors(const SourceEntry& entry, bool install_locked);
    static bool publish_rows(SourceRowList& list, const std::vector<const SourceRowState*>& rows);

    std::vector<Slot> m_entries;
    std::set<std::string> m_required_repos;
    bool m_listed{false};

    SourceRowList m_online_rows;
    SourceRowList m_local_rows;
};

// ---------------------------------------------------------------------------------------------
// Controller
// ---------------------------------------------------------------------------------------------

class IPresetUpdaterControllerListener
{
public:
    virtual ~IPresetUpdaterControllerListener() = default;

    virtual void on_preset_updater_changed() {}
};

/// Everything the controller needs from the running application, so that none of it is reached
/// for through a singleton and all of it can be supplied by a test.
struct ControllerEnvironment
{
    Biz::Platform::TimerQueue* timer_queue{nullptr};
    /// Whether the preset updater may use the network. Asked once per operation, never cached.
    std::function<bool()> online_allowed;
    /// The application draws on demand, so a change made outside a frame has to ask for one.
    std::function<void()> request_render;
};

/**
 * @brief What the preset updater is doing right now.
 *
 * One value, one meaning. "Nothing is running" and "there is nothing to install" are separate
 * questions, answered by separate predicates.
 */
enum class ControllerActivity
{
    None,
    ListingSources,
    Checking,
    Installing
};

/**
 * @brief State behind the preset updater dialog, shared by every instance of it.
 *
 * Owns the four things the preset updater has to keep straight, and owns them separately: what
 * the sources are (SourceStore), what has been submitted (JobLedger), what the application is
 * being held on (ForcedReconfigurations), and when a selection edit reaches the disk
 * (SelectionCommitter).
 *
 * Two rules hold everywhere. Whether a dialog happens to be open never decides whether state is
 * recorded - it decides only what is shown. And every change happens inside a transaction, so one
 * operation produces one publish and one notification however many fields it touched.
 */
class PresetUpdaterController :
    public Biz::PresetUpdater::IPresetUpdaterResultListener,
    public WithListeners<IPresetUpdaterControllerListener>
{
public:
    PresetUpdaterController(
        Biz::PresetUpdater::PresetUpdaterInteractor& interactor, ControllerEnvironment environment
    );

    ~PresetUpdaterController() override;

    PresetUpdaterController(const PresetUpdaterController&)            = delete;
    PresetUpdaterController& operator=(const PresetUpdaterController&) = delete;

    /// Releases the interactor and stops the controller doing anything. The destructor calls it.
    void shutdown();

    // -- launch -------------------------------------------------------------------------------

    /**
     * @brief Starts the check a launch owes: whether the installed profiles are usable at all.
     *
     * Answered by the forced state callback, always, including when the work could not be
     * submitted. Nothing downstream may be left waiting for an answer that cannot come.
     */
    void start();

    /// Starts the check for updates. Reports what it finds, never that it is running.
    void start_background_check();

    void set_show_dialog_callback(std::function<void()> callback);
    void set_presets_installed_callback(std::function<void()> callback);

    /**
     * @brief Reports whether a forced reconfiguration is holding the application back.
     *
     * Called with the answer of the launch check, and called again with false whenever a forced
     * reconfiguration is released afterwards, whatever released it.
     */
    void set_forced_state_callback(std::function<void(bool has_forced)> callback);

    // -- dialog -------------------------------------------------------------------------------

    void on_dialog_opened();
    void on_dialog_closed();
    bool dialog_open() const;

    // -- commands -----------------------------------------------------------------------------

    void set_source_selected(const std::string& uuid, bool selected);
    void add_local_repository(const boost::filesystem::path& zip_path);
    void remove_local_repository(const std::string& uuid);

    void update_vendor(const std::string& repo_id, const std::string& vendor_id);
    void update_source(const std::string& uuid);
    void update_everything();
    void update_required();

    // -- queries ------------------------------------------------------------------------------

    ControllerActivity activity() const;

    bool busy() const;

    /// A job failed, or a source could not be checked.
    bool warned() const;

    bool online_allowed() const;

    bool has_actionable_updates() const;

    /// Whether anything is left of what a forced reconfiguration requires.
    bool has_required_updates() const;

    bool forced_mode() const;

    /// Every selected source has a result, so "up to date" is a statement and not a guess.
    bool fully_checked() const;

    Biz::IObservableList<SourceRowState>& online_sources();
    Biz::IObservableList<SourceRowState>& local_sources();

    // -- IPresetUpdaterResultListener ---------------------------------------------------------

    void on_preset_updater_error(
        JobId job_id, const std::string& body, Biz::PresetUpdater::PresetUpdaterReason reason
    ) override;
    void on_preset_updater_forced_reconfigurations_list(
        JobId job_id,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;
    void on_preset_updater_reconfigurations_list(
        JobId job_id,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings,
        Biz::PresetUpdater::VerboseStyle verbose
    ) override;
    void on_preset_updater_reconfigurations_performed(
        JobId job_id, const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;
    void on_preset_updater_status(
        JobId job_id,
        const std::string& target,
        int attempt,
        unsigned delay,
        Biz::PresetUpdater::VerboseStyle verbose
    ) override;
    void on_preset_updater_repository_info_vector(
        JobId job_id,
        const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;
    void on_preset_updater_repository_selection_performed(
        JobId job_id,
        const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;
    void on_preset_updater_job_finished(JobId job_id, Biz::PresetUpdater::JobState state) override;

private:
    /**
     * @brief Every answer a listener can read off this controller.
     *
     * Compared against the set published last time, so a change is announced when one of these
     * actually moved and never because somebody remembered to raise a flag. Adding a public query
     * to this class means adding it here; nothing else has to be touched for it to be noticed.
     */
    struct Snapshot
    {
        ControllerActivity activity{ControllerActivity::None};
        bool warned{false};
        bool forced_mode{false};
        bool fully_checked{false};
        bool has_actionable{false};
        bool has_required{false};
        bool online_allowed{false};

        bool operator==(const Snapshot&) const = default;
    };

    enum class CheckReporting
    {
        Silent,
        Reported
    };

    /// Publishes and notifies once, however many changes were made inside it, however nested.
    class Transaction
    {
    public:
        explicit Transaction(PresetUpdaterController& controller);
        ~Transaction();

        Transaction(const Transaction&)            = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        PresetUpdaterController& m_controller;
    };

    JobId submit(JobKind kind, const std::function<JobId()>& operation, std::string subject = {});

    void start_listing();
    void start_check(Biz::PresetUpdater::SourceListSync source_list, CheckReporting reporting);
    void start_install(const std::vector<VendorKey>& keys);
    void commit_selection();

    void cancel_check();

    SourceStore::Locks current_locks() const;

    Snapshot snapshot() const;

    void flush();
    void report_problems();

    void notify_forced_state(bool has_forced);
    void show_dialog();

    Biz::PresetUpdater::PresetUpdaterInteractor& m_interactor;
    ControllerEnvironment m_environment;
    PresetUpdaterActivityReporter m_activity_reporter;

    SourceStore m_store;
    JobLedger m_ledger;
    ForcedReconfigurations m_forced;
    std::optional<SelectionCommitter> m_committer;

    std::vector<Biz::PresetUpdater::PresetUpdaterWarning> m_warnings;
    bool m_operation_failed{false};

    std::function<void()> m_show_dialog_callback;
    std::function<void()> m_presets_installed_callback;
    std::function<void(bool has_forced)> m_forced_state_callback;

    size_t m_open_count{0};
    size_t m_transaction_depth{0};
    std::optional<Snapshot> m_published;
    bool m_shut_down{false};
};

} // namespace Slic3r::App::PresetUpdater
