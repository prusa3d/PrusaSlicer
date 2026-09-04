#include "Slic3r/App/PresetUpdater/PresetUpdaterController.hpp"

#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Slic3r::App::PresetUpdater {

namespace {

constexpr std::chrono::milliseconds k_selection_commit_debounce{300};

using Activity = PresetUpdaterActivityReporter::Activity;
using Biz::PresetUpdater::k_invalid_job_id;
using Biz::PresetUpdater::PresetUpdaterReconfigurationList;
using Biz::PresetUpdater::PresetUpdaterWarning;
using Biz::PresetUpdater::VendorReconfiguration;

std::vector<const std::vector<VendorReconfiguration>*> all_buckets(
    const PresetUpdaterReconfigurationList& list
)
{
    return {
        &list.regular_updates(),
        &list.forced_updates(),
        &list.forced_downgrades(),
        &list.not_in_index(),
        &list.new_vendors(),
        &list.removals()
    };
}

std::vector<VendorKey> forced_keys(const PresetUpdaterReconfigurationList& list)
{
    std::vector<VendorKey> keys;
    for (const std::vector<VendorReconfiguration>* bucket :
         {&list.forced_updates(), &list.forced_downgrades(), &list.removals()})
    {
        for (const VendorReconfiguration& reconfiguration : *bucket) {
            keys.push_back(VendorKey{reconfiguration.vendor_repo_id, reconfiguration.vendor_id});
        }
    }
    return keys;
}

void log_reconfigurations(const PresetUpdaterReconfigurationList& list)
{
    const std::pair<const char*, const std::vector<VendorReconfiguration>*> buckets[]{
        {"update", &list.regular_updates()},
        {"new vendor", &list.new_vendors()},
        {"not in index", &list.not_in_index()},
        {"forced update", &list.forced_updates()},
        {"forced downgrade", &list.forced_downgrades()},
        {"removal", &list.removals()},
    };
    for (const auto& [name, bucket] : buckets) {
        for (const VendorReconfiguration& reconfiguration : *bucket) {
            SPDLOG_INFO("{}: {}/{}", name, reconfiguration.vendor_repo_id, reconfiguration.vendor_id);
        }
    }
}

void log_warnings(const std::vector<PresetUpdaterWarning>& warnings)
{
    for (const PresetUpdaterWarning& warning : warnings) {
        SPDLOG_WARN("Preset updater warning: {}", warning.string());
    }
}

const char* job_kind_name(JobKind kind)
{
    switch (kind) {
    case JobKind::ForcedCheck:
        return "forced check";
    case JobKind::Listing:
        return "source listing";
    case JobKind::Check:
        return "update check";
    case JobKind::Selection:
        return "source selection";
    case JobKind::Install:
        return "install";
    case JobKind::AddSource:
        return "add source";
    case JobKind::RemoveSource:
        return "remove source";
    }
    return "unknown";
}

} // namespace

// =============================================================================================
// JobLedger
// =============================================================================================

bool JobLedger::track(JobId job_id, JobKind kind, std::string subject)
{
    if (job_id == k_invalid_job_id) {
        return false;
    }
    Record record;
    record.kind      = kind;
    record.subject   = std::move(subject);
    m_records[job_id] = std::move(record);
    m_newest[kind]    = job_id;
    return true;
}

void JobLedger::set_install_keys(JobId job_id, std::vector<VendorKey> keys)
{
    const auto record = m_records.find(job_id);
    if (record == m_records.end()) {
        return;
    }
    record->second.keys = std::move(keys);
}

const JobLedger::Record* JobLedger::find(JobId job_id) const
{
    const auto record = m_records.find(job_id);
    return record == m_records.end() ? nullptr : &record->second;
}

JobId JobLedger::newest(JobKind kind) const
{
    const auto newest = m_newest.find(kind);
    return newest == m_newest.end() ? k_invalid_job_id : newest->second;
}

bool JobLedger::is_newest(JobId job_id) const
{
    const Record* record = find(job_id);
    return record != nullptr && newest(record->kind) == job_id;
}

bool JobLedger::any_pending(JobKind kind) const
{
    return std::any_of(
        m_records.begin(),
        m_records.end(),
        [kind](const auto& entry) { return entry.second.kind == kind; }
    );
}

void JobLedger::forget(JobId job_id)
{
    const auto record = m_records.find(job_id);
    if (record == m_records.end()) {
        return;
    }
    const auto newest = m_newest.find(record->second.kind);
    if (newest != m_newest.end() && newest->second == job_id) {
        m_newest.erase(newest);
    }
    m_records.erase(record);
}

void JobLedger::clear()
{
    m_records.clear();
    m_newest.clear();
}

// =============================================================================================
// ForcedReconfigurations
// =============================================================================================

void ForcedReconfigurations::reset(const PresetUpdaterReconfigurationList& reconfigurations)
{
    m_required = forced_keys(reconfigurations);
}

void ForcedReconfigurations::reconcile(
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::set<std::string>& evaluated_repos,
    const std::vector<VendorKey>& unjudged
)
{
    const std::vector<VendorKey> reported = forced_keys(reconfigurations);
    const auto is_reported                = [&reported](const VendorKey& key)
    { return std::find(reported.begin(), reported.end(), key) != reported.end(); };
    const auto is_unjudged                = [&unjudged](const VendorKey& key)
    { return std::find(unjudged.begin(), unjudged.end(), key) != unjudged.end(); };

    std::erase_if(
        m_required,
        [&evaluated_repos, &is_reported, &is_unjudged](const VendorKey& key)
        {
            return evaluated_repos.contains(key.repo_id) && !is_reported(key)
                && !is_unjudged(key);
        }
    );

    for (const VendorKey& key : reported) {
        if (std::find(m_required.begin(), m_required.end(), key) == m_required.end()) {
            m_required.push_back(key);
        }
    }
}

void ForcedReconfigurations::mark_satisfied(const VendorKey& key)
{
    std::erase(m_required, key);
}

void ForcedReconfigurations::clear()
{
    m_required.clear();
}

bool ForcedReconfigurations::active() const
{
    return !m_required.empty();
}

bool ForcedReconfigurations::requires_vendor(const VendorKey& key) const
{
    return std::find(m_required.begin(), m_required.end(), key) != m_required.end();
}

const std::vector<VendorKey>& ForcedReconfigurations::required() const
{
    return m_required;
}

std::set<std::string> ForcedReconfigurations::required_repo_ids() const
{
    std::set<std::string> repo_ids;
    for (const VendorKey& key : m_required) {
        repo_ids.insert(key.repo_id);
    }
    return repo_ids;
}

// =============================================================================================
// SelectionCommitter
// =============================================================================================

SelectionCommitter::SelectionCommitter(
    Biz::Platform::TimerQueue& timer_queue, CommitFn commit
) :
    m_timer_queue(timer_queue),
    m_commit(std::move(commit))
{}

SelectionCommitter::~SelectionCommitter() = default;

void SelectionCommitter::touch()
{
    m_dirty                   = true;
    m_armed                   = true;
    const uint64_t generation = ++m_generation;

    m_timer_queue.set_timer(
        k_selection_commit_debounce,
        [this, generation, life = std::weak_ptr<int>(m_life)]()
        {
            if (life.expired() || generation != m_generation) {
                return;
            }
            m_armed = false;
            fire();
        }
    );
}

void SelectionCommitter::flush()
{
    if (!m_dirty) {
        return;
    }
    ++m_generation;
    m_armed = false;
    fire();
}

void SelectionCommitter::on_commit_finished()
{
    m_running = false;
    if (m_dirty) {
        fire();
    }
}

void SelectionCommitter::set_commit_running(bool running)
{
    m_running = running;
}

void SelectionCommitter::cancel()
{
    ++m_generation;
    m_armed = false;
    m_dirty = false;
}

void SelectionCommitter::mark_unwritten()
{
    m_dirty = true;
}

bool SelectionCommitter::pending() const
{
    return m_dirty;
}

void SelectionCommitter::fire()
{
    if (!m_dirty || m_running) {
        return;
    }
    m_dirty = false;
    m_commit();
}

// =============================================================================================
// SourceStore
// =============================================================================================

SourceStore::CheckOutcome SourceStore::CheckOutcome::from(
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    CheckOutcome outcome;

    for (const std::vector<VendorReconfiguration>* bucket : all_buckets(reconfigurations)) {
        for (const VendorReconfiguration& reconfiguration : *bucket) {
            outcome.by_repo[reconfiguration.vendor_repo_id].push_back(
                VendorReport{
                    reconfiguration.vendor_id,
                    reconfiguration.comment,
                    reconfiguration.state,
                    reconfiguration.current_version,
                    reconfiguration.recommended_version,
                    false
                }
            );
        }
    }

    for (const PresetUpdaterWarning& warning : warnings) {
        if (warning.repo.empty()) {
            continue;
        }
        if (warning.vendor.empty()) {
            outcome.failed_repos.insert(warning.repo);
            continue;
        }
        outcome.skipped_by_repo[warning.repo].insert(warning.vendor);
    }

    for (const auto& [repo_id, vendor_ids] : outcome.skipped_by_repo) {
        for (const std::string& vendor_id : vendor_ids) {
            VendorReport report;
            report.vendor_id = vendor_id;
            report.skipped   = true;
            outcome.by_repo[repo_id].push_back(std::move(report));
        }
    }

    return outcome;
}

std::vector<VendorKey> SourceStore::CheckOutcome::unjudged_keys() const
{
    std::vector<VendorKey> keys;
    for (const auto& [repo_id, vendor_ids] : skipped_by_repo) {
        for (const std::string& vendor_id : vendor_ids) {
            keys.push_back(VendorKey{repo_id, vendor_id});
        }
    }
    return keys;
}

bool SourceStore::listed() const
{
    return m_listed;
}

SourceStore::Slot* SourceStore::find_slot(const std::string& uuid)
{
    const auto slot = std::find_if(
        m_entries.begin(),
        m_entries.end(),
        [&uuid](const Slot& candidate) { return candidate.read().descriptor.uuid == uuid; }
    );
    return slot == m_entries.end() ? nullptr : &*slot;
}

const SourceStore::Slot* SourceStore::find_slot(const std::string& uuid) const
{
    return const_cast<SourceStore*>(this)->find_slot(uuid);
}

SourceStore::Slot* SourceStore::find_active_slot_by_repo(const std::string& repo_id)
{
    const auto slot = std::find_if(
        m_entries.begin(),
        m_entries.end(),
        [&repo_id](const Slot& candidate)
        {
            return active(candidate.read()) && candidate.read().descriptor.id == repo_id;
        }
    );
    return slot == m_entries.end() ? nullptr : &*slot;
}

const SourceStore::Slot* SourceStore::find_active_slot_by_repo(const std::string& repo_id) const
{
    return const_cast<SourceStore*>(this)->find_active_slot_by_repo(repo_id);
}

SourceStore::VendorEntry* SourceStore::find_vendor_in(
    SourceEntry& entry, const std::string& vendor_id
)
{
    const auto vendor = std::find_if(
        entry.vendors.begin(),
        entry.vendors.end(),
        [&vendor_id](const VendorEntry& candidate) { return candidate.vendor_id == vendor_id; }
    );
    return vendor == entry.vendors.end() ? nullptr : &*vendor;
}

const SourceStore::VendorEntry* SourceStore::find_vendor_in(
    const SourceEntry& entry, const std::string& vendor_id
)
{
    const auto vendor = std::find_if(
        entry.vendors.begin(),
        entry.vendors.end(),
        [&vendor_id](const VendorEntry& candidate) { return candidate.vendor_id == vendor_id; }
    );
    return vendor == entry.vendors.end() ? nullptr : &*vendor;
}

SourceStore::VendorEntry* SourceStore::find_vendor(const VendorKey& key)
{
    Slot* slot = find_active_slot_by_repo(key.repo_id);
    if (slot == nullptr || find_vendor_in(slot->read(), key.vendor_id) == nullptr) {
        return nullptr;
    }
    return find_vendor_in(slot->write(), key.vendor_id);
}

const SourceStore::VendorEntry* SourceStore::find_vendor(const VendorKey& key) const
{
    const Slot* slot = find_active_slot_by_repo(key.repo_id);
    return slot == nullptr ? nullptr : find_vendor_in(slot->read(), key.vendor_id);
}

bool SourceStore::has_selected_with_id(const std::string& id) const
{
    return std::any_of(
        m_entries.begin(),
        m_entries.end(),
        [&id](const Slot& slot)
        { return slot.read().selected && slot.read().descriptor.id == id; }
    );
}

bool SourceStore::needs_check() const
{
    return std::any_of(
        m_entries.begin(),
        m_entries.end(),
        [](const Slot& slot)
        {
            const SourceEntry& entry = slot.read();
            return active(entry)
                && (entry.phase == CheckPhase::NotChecked || entry.phase == CheckPhase::Waiting);
        }
    );
}

bool SourceStore::every_active_checked() const
{
    return std::none_of(
        m_entries.begin(),
        m_entries.end(),
        [](const Slot& slot)
        {
            const SourceEntry& entry = slot.read();
            return active(entry)
                && (entry.phase == CheckPhase::NotChecked || entry.phase == CheckPhase::Waiting
                    || entry.phase == CheckPhase::Checking);
        }
    );
}

bool SourceStore::any_check_failed() const
{
    return std::any_of(
        m_entries.begin(),
        m_entries.end(),
        [](const Slot& slot)
        { return active(slot.read()) && slot.read().phase == CheckPhase::Failed; }
    );
}

std::set<std::string> SourceStore::installing_ids() const
{
    std::set<std::string> ids;
    for (const Slot& slot : m_entries) {
        if (installing(slot.read())) {
            ids.insert(slot.read().descriptor.id);
        }
    }
    return ids;
}

bool SourceStore::selection_locked(const std::string& uuid) const
{
    const Slot* slot = find_slot(uuid);
    return slot != nullptr && installing_ids().contains(slot->read().descriptor.id);
}

std::string SourceStore::name_of(const std::string& uuid) const
{
    const Slot* slot = find_slot(uuid);
    return slot == nullptr ? std::string{} : slot->read().descriptor.name;
}

SourceStore::RepoLookup SourceStore::lookup_repo(const std::string& repo_id) const
{
    RepoLookup lookup;
    lookup.name = repo_id;
    for (const Slot& slot : m_entries) {
        const SourceEntry& entry = slot.read();
        if (entry.descriptor.id != repo_id) {
            continue;
        }
        // An active source is the one the warning is about; a deselected namesake is not.
        if (active(entry) || lookup.visibility == RepoVisibility::Unknown) {
            lookup.name = entry.descriptor.name;
        }
        if (entry.selected) {
            lookup.visibility = RepoVisibility::Selected;
            return lookup;
        }
        if (entry.required) {
            lookup.visibility = RepoVisibility::Required;
            return lookup;
        }
        lookup.visibility = RepoVisibility::Deselected;
    }
    return lookup;
}

std::vector<VendorKey> SourceStore::actionable_of(const SourceEntry& entry, bool required_only)
{
    std::vector<VendorKey> keys;
    if (!active(entry)) {
        return keys;
    }
    for (const VendorEntry& vendor : entry.vendors) {
        if (vendor.skipped) {
            continue;
        }
        if (required_only && vendor.state != VendorReconfigurationState::ForcedUpdate
            && vendor.state != VendorReconfigurationState::ForcedDowngrade
            && vendor.state != VendorReconfigurationState::RemoveVendor)
        {
            continue;
        }
        if (vendor.install_state != InstallState::Idle
            && vendor.install_state != InstallState::Failed)
        {
            continue;
        }
        keys.push_back(VendorKey{entry.descriptor.id, vendor.vendor_id});
    }
    return keys;
}

std::vector<VendorKey> SourceStore::actionable_keys(bool required_only) const
{
    std::vector<VendorKey> keys;
    for (const Slot& slot : m_entries) {
        const std::vector<VendorKey> entry_keys = actionable_of(slot.read(), required_only);
        keys.insert(keys.end(), entry_keys.begin(), entry_keys.end());
    }
    return keys;
}

std::vector<VendorKey> SourceStore::actionable_keys_of_source(
    const std::string& uuid, bool required_only
) const
{
    const Slot* slot = find_slot(uuid);
    return slot == nullptr ? std::vector<VendorKey>{} : actionable_of(slot->read(), required_only);
}

bool SourceStore::any_actionable(bool required_only) const
{
    return std::any_of(
        m_entries.begin(),
        m_entries.end(),
        [required_only](const Slot& slot)
        { return !actionable_of(slot.read(), required_only).empty(); }
    );
}

void SourceStore::reset_from(
    const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
    bool keep_selection_edits
)
{
    std::vector<Slot> entries;
    entries.reserve(descriptor.size());

    for (const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfo& info : descriptor) {
        SourceEntry entry;
        entry.descriptor = info.descriptor;
        entry.selected   = info.selected;

        const Slot* slot = find_slot(info.descriptor.uuid);
        if (slot != nullptr) {
            const SourceEntry& previous = slot->read();
            entry.vendor_rows           = previous.vendor_rows;
            if (keep_selection_edits) {
                entry.selected = previous.selected;
            }
            entry.required        = previous.required;
            entry.phase           = previous.phase;
            entry.vendors         = previous.vendors;
            entry.skipped_vendors = previous.skipped_vendors;
        }

        entries.emplace_back(std::move(entry));
    }

    m_entries = std::move(entries);
    m_listed  = true;

    apply_required_flags();

    for (Slot& slot : m_entries) {
        SourceEntry& entry = slot.write();
        if (!active(entry)) {
            entry.phase = CheckPhase::NotChecked;
            entry.vendors.clear();
            entry.skipped_vendors.clear();
            continue;
        }
        if (entry.phase == CheckPhase::NotChecked) {
            entry.phase = CheckPhase::Waiting;
        }
    }
}

bool SourceStore::set_selected(const std::string& uuid, bool selected)
{
    if (selection_locked(uuid)) {
        return false;
    }

    Slot* slot = find_slot(uuid);
    if (slot == nullptr) {
        return false;
    }

    const std::string repo_id = slot->read().descriptor.id;
    const bool changed        = slot->read().selected != selected;
    if (changed) {
        slot->write().selected = selected;
    }

    if (selected) {
        // Only one source per id may be selected; the interactor treats a clash as fatal.
        for (Slot& other : m_entries) {
            if (other.read().descriptor.uuid == uuid || other.read().descriptor.id != repo_id
                || !other.read().selected)
            {
                continue;
            }
            other.write().selected = false;
        }
    }

    apply_required_flags();

    if (changed && active(slot->read())) {
        SourceEntry& toggled = slot->write();
        toggled.phase        = CheckPhase::Waiting;
        toggled.vendors.clear();
        toggled.skipped_vendors.clear();
    }

    for (Slot& candidate : m_entries) {
        const SourceEntry& entry = candidate.read();
        if (active(entry)) {
            if (entry.phase == CheckPhase::NotChecked) {
                candidate.write().phase = CheckPhase::Waiting;
            }
            continue;
        }
        if (entry.phase == CheckPhase::NotChecked && entry.vendors.empty()
            && entry.skipped_vendors.empty())
        {
            continue;
        }
        SourceEntry& cleared = candidate.write();
        cleared.phase        = CheckPhase::NotChecked;
        cleared.vendors.clear();
        cleared.skipped_vendors.clear();
    }

    return true;
}

void SourceStore::set_required_repos(const std::set<std::string>& repo_ids)
{
    if (m_required_repos == repo_ids) {
        return;
    }
    m_required_repos = repo_ids;
    apply_required_flags();

    for (Slot& slot : m_entries) {
        const SourceEntry& entry = slot.read();
        if (active(entry) || entry.phase == CheckPhase::NotChecked) {
            continue;
        }
        SourceEntry& cleared = slot.write();
        cleared.phase        = CheckPhase::NotChecked;
        cleared.vendors.clear();
        cleared.skipped_vendors.clear();
    }
}

void SourceStore::forget_results()
{
    for (Slot& slot : m_entries) {
        SourceEntry& entry = slot.write();

        std::vector<VendorEntry> in_flight;
        for (VendorEntry& vendor : entry.vendors) {
            if (vendor.install_state == InstallState::Queued
                || vendor.install_state == InstallState::Running)
            {
                in_flight.push_back(std::move(vendor));
            }
        }

        entry.vendors = std::move(in_flight);
        entry.skipped_vendors.clear();
        entry.phase = active(entry) ? CheckPhase::Waiting : CheckPhase::NotChecked;
    }
}

SourceStore::CheckPhase SourceStore::next_check_phase(CheckPhase current)
{
    switch (current) {
    case CheckPhase::NotChecked:
    case CheckPhase::Waiting:
    case CheckPhase::Checking:
        return CheckPhase::Checking;
    case CheckPhase::Checked:
    case CheckPhase::Failed:
    case CheckPhase::Rechecking:
        break;
    }
    return CheckPhase::Rechecking;
}

void SourceStore::begin_check()
{
    for (Slot& slot : m_entries) {
        const SourceEntry& entry = slot.read();

        if (!active(entry)) {
            if (entry.phase == CheckPhase::NotChecked && entry.vendors.empty()
                && entry.skipped_vendors.empty())
            {
                continue;
            }
            SourceEntry& cleared = slot.write();
            cleared.phase        = CheckPhase::NotChecked;
            cleared.vendors.clear();
            cleared.skipped_vendors.clear();
            continue;
        }

        const CheckPhase next = next_check_phase(entry.phase);
        if (next != entry.phase) {
            slot.write().phase = next;
        }
    }
}

std::set<std::string> SourceStore::apply_check(const CheckOutcome& outcome)
{
    static const std::vector<VendorReport> k_nothing_offered;

    std::set<std::string> evaluated;

    for (Slot& slot : m_entries) {
        if (!active(slot.read())) {
            const SourceEntry& entry = slot.read();
            if (entry.phase == CheckPhase::NotChecked && entry.vendors.empty()
                && entry.skipped_vendors.empty())
            {
                continue;
            }
            SourceEntry& cleared = slot.write();
            cleared.phase        = CheckPhase::NotChecked;
            cleared.vendors.clear();
            cleared.skipped_vendors.clear();
            continue;
        }
        if (slot.read().phase != CheckPhase::Checking
            && slot.read().phase != CheckPhase::Rechecking)
        {
            continue;
        }

        SourceEntry& entry         = slot.write();
        const std::string& repo_id = entry.descriptor.id;

        const auto reported = outcome.by_repo.find(repo_id);
        merge_vendors(entry, reported != outcome.by_repo.end() ? reported->second : k_nothing_offered);

        const auto skipped = outcome.skipped_by_repo.find(repo_id);
        if (skipped != outcome.skipped_by_repo.end()) {
            entry.skipped_vendors.assign(skipped->second.begin(), skipped->second.end());
        } else {
            entry.skipped_vendors.clear();
        }

        if (outcome.failed_repos.contains(repo_id)) {
            entry.phase = CheckPhase::Failed;
            continue;
        }

        entry.phase = CheckPhase::Checked;
        evaluated.insert(repo_id);
    }

    return evaluated;
}

void SourceStore::abandon_check()
{
    for (Slot& slot : m_entries) {
        switch (slot.read().phase) {
        case CheckPhase::Waiting:
        case CheckPhase::Checking:
            slot.write().phase = CheckPhase::NotChecked;
            break;
        case CheckPhase::Rechecking:
            slot.write().phase = CheckPhase::Checked;
            break;
        default:
            break;
        }
    }
}

void SourceStore::merge_vendors(SourceEntry& entry, const std::vector<VendorReport>& incoming)
{
    std::map<std::string, VendorEntry> previous;
    for (const VendorEntry& vendor : entry.vendors) {
        previous.emplace(vendor.vendor_id, vendor);
    }

    std::vector<VendorEntry> merged;
    merged.reserve(incoming.size() + previous.size());

    for (const VendorReport& report : incoming) {
        VendorEntry vendor;
        vendor.vendor_id           = report.vendor_id;
        vendor.comment             = report.comment;
        vendor.state               = report.state;
        vendor.current_version     = report.current_version;
        vendor.recommended_version = report.recommended_version;
        vendor.skipped             = report.skipped;

        const auto previous_vendor = previous.find(report.vendor_id);
        if (previous_vendor != previous.end()
            && previous_vendor->second.install_state != InstallState::Done)
        {
            // Work in flight, or a failure the user has not dealt with, outlives a re-check.
            vendor.install_state = previous_vendor->second.install_state;
            vendor.error_text    = previous_vendor->second.error_text;
        }

        merged.push_back(std::move(vendor));
        previous.erase(report.vendor_id);
    }

    // What is left is no longer offered. Keeping the installed ones leaves their confirmation up.
    for (auto& [vendor_id, vendor] : previous) {
        if (vendor.install_state == InstallState::Done) {
            merged.push_back(std::move(vendor));
        }
    }

    entry.vendors = std::move(merged);
}

SourceStore::InstallRequest SourceStore::make_install_request(
    const std::vector<VendorKey>& keys
) const
{
    InstallRequest request;
    for (const VendorKey& key : keys) {
        const VendorEntry* vendor = find_vendor(key);
        if (vendor == nullptr || vendor->skipped) {
            continue;
        }
        request.list.emplace_back(
            vendor->state,
            vendor->vendor_id,
            key.repo_id,
            vendor->current_version,
            vendor->recommended_version,
            vendor->comment
        );
        request.keys.push_back(key);
    }
    return request;
}

std::vector<PresetUpdaterActivityReporter::InstalledVendor> SourceStore::installed_vendors_for(
    const std::vector<VendorKey>& keys
) const
{
    std::vector<PresetUpdaterActivityReporter::InstalledVendor> installed;
    for (const VendorKey& key : keys) {
        const VendorEntry* vendor = find_vendor(key);
        if (vendor == nullptr) {
            continue;
        }
        installed.push_back(
            PresetUpdaterActivityReporter::InstalledVendor{
                vendor->vendor_id,
                vendor->current_version,
                vendor->recommended_version,
                vendor->state
            }
        );
    }
    return installed;
}

void SourceStore::set_install_state(const std::vector<VendorKey>& keys, InstallState state)
{
    for (const VendorKey& key : keys) {
        VendorEntry* vendor = find_vendor(key);
        if (vendor == nullptr) {
            continue;
        }
        vendor->install_state = state;
        vendor->error_text.clear();
    }
}

void SourceStore::set_install_failed(const VendorKey& key, const std::string& error_text)
{
    VendorEntry* vendor = find_vendor(key);
    if (vendor == nullptr) {
        return;
    }
    vendor->install_state = InstallState::Failed;
    vendor->error_text    = error_text;
}

void SourceStore::set_install_done(const VendorKey& key)
{
    VendorEntry* vendor = find_vendor(key);
    if (vendor == nullptr) {
        return;
    }
    vendor->install_state = InstallState::Done;
    vendor->error_text.clear();
}

void SourceStore::abandon_install(const std::vector<VendorKey>& keys, bool canceled)
{
    for (const VendorKey& key : keys) {
        VendorEntry* vendor = find_vendor(key);
        if (vendor == nullptr || vendor->install_state == InstallState::Done
            || vendor->install_state == InstallState::Failed)
        {
            continue;
        }
        vendor->install_state = canceled ? InstallState::Idle : InstallState::Failed;
    }
}

void SourceStore::promote_queued(const std::vector<VendorKey>& keys)
{
    for (const VendorKey& key : keys) {
        VendorEntry* vendor = find_vendor(key);
        if (vendor != nullptr && vendor->install_state == InstallState::Queued) {
            vendor->install_state = InstallState::Running;
        }
    }
}

Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector SourceStore::selection() const
{
    Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector selection;
    selection.reserve(m_entries.size());
    for (const Slot& slot : m_entries) {
        selection.push_back(
            Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfo{
                slot.read().descriptor, slot.read().selected
            }
        );
    }
    return selection;
}

bool SourceStore::active(const SourceEntry& entry)
{
    return entry.selected || entry.required;
}

void SourceStore::apply_required_flags()
{
    std::map<std::string, std::string> serving;
    const auto claim = [this, &serving](auto&& accept)
    {
        for (const Slot& slot : m_entries) {
            const SourceEntry& entry = slot.read();
            if (!m_required_repos.contains(entry.descriptor.id)
                || serving.contains(entry.descriptor.id))
            {
                continue;
            }
            if (accept(entry)) {
                serving.emplace(entry.descriptor.id, entry.descriptor.uuid);
            }
        }
    };

    claim([](const SourceEntry& entry) { return entry.selected; });
    claim([](const SourceEntry& entry) { return entry.required; });
    claim([](const SourceEntry&) { return true; });

    for (Slot& slot : m_entries) {
        const SourceEntry& entry = slot.read();
        const auto served        = serving.find(entry.descriptor.id);
        const bool required =
            served != serving.end() && served->second == entry.descriptor.uuid;
        if (entry.required != required) {
            slot.write().required = required;
        }
    }
}

SourceCounts SourceStore::counts_of(const SourceEntry& entry)
{
    SourceCounts counts;
    for (const VendorEntry& vendor : entry.vendors) {
        if (vendor.skipped || vendor.install_state == InstallState::Done) {
            continue;
        }
        switch (vendor.state) {
        case VendorReconfigurationState::Update:
            ++counts.updates;
            break;
        case VendorReconfigurationState::NewVendor:
            ++counts.new_vendors;
            break;
        case VendorReconfigurationState::ForcedUpdate:
        case VendorReconfigurationState::ForcedDowngrade:
        case VendorReconfigurationState::RemoveVendor:
            ++counts.required;
            break;
        case VendorReconfigurationState::NotInIndex:
            ++counts.unknown_versions;
            break;
        }
    }
    return counts;
}

bool SourceStore::installing(const SourceEntry& entry)
{
    return std::any_of(
        entry.vendors.begin(),
        entry.vendors.end(),
        [](const VendorEntry& vendor)
        {
            return vendor.install_state == InstallState::Queued
                || vendor.install_state == InstallState::Running;
        }
    );
}

SourceRowState::UpdateState SourceStore::display_state_of(const SourceEntry& entry)
{
    if (!active(entry)) {
        return SourceRowState::UpdateState::NotChecked;
    }
    if (installing(entry)) {
        return SourceRowState::UpdateState::Installing;
    }
    switch (entry.phase) {
    case CheckPhase::NotChecked:
        return SourceRowState::UpdateState::NotChecked;
    case CheckPhase::Waiting:
        return SourceRowState::UpdateState::Waiting;
    case CheckPhase::Checking:
        return SourceRowState::UpdateState::Checking;
    case CheckPhase::Checked:
    case CheckPhase::Failed:
    case CheckPhase::Rechecking:
        break;
    }
    return counts_of(entry).pending() > 0 ? SourceRowState::UpdateState::HasUpdates :
                                            SourceRowState::UpdateState::UpToDate;
}

SourceRowState SourceStore::project(
    const SourceEntry& entry, bool install_locked, bool selection_locked
)
{
    SourceRowState row;
    row.uuid        = entry.descriptor.uuid;
    row.id          = entry.descriptor.id;
    row.name        = entry.descriptor.name;
    row.description = entry.descriptor.description;
    row.visibility  = entry.descriptor.visibility;
    row.zip_path    = entry.descriptor.zip_path;

    row.selected         = entry.selected;
    row.required         = entry.required;
    row.selection_locked = selection_locked;
    row.install_locked   = install_locked;

    row.update_state   = display_state_of(entry);
    row.counts         = counts_of(entry);
    row.skipped_vendors = entry.skipped_vendors;
    row.check_failed   = active(entry) && entry.phase == CheckPhase::Failed;
    row.vendors        = entry.vendor_rows;

    return row;
}

bool SourceStore::publish_vendors(const SourceEntry& entry, bool install_locked)
{
    std::vector<VendorRowState> rows;
    rows.reserve(entry.vendors.size());
    for (const VendorEntry& vendor : entry.vendors) {
        VendorRowState row;
        row.repo_id             = entry.descriptor.id;
        row.vendor_id           = vendor.vendor_id;
        row.comment             = vendor.comment;
        row.state               = vendor.state;
        row.current_version     = vendor.current_version;
        row.recommended_version = vendor.recommended_version;
        row.install_state       = vendor.install_state;
        row.error_text          = vendor.error_text;
        row.install_locked      = install_locked;
        row.skipped             = vendor.skipped;
        rows.push_back(std::move(row));
    }

    VendorRowList& list = *entry.vendor_rows;
    if (list.size() != rows.size()) {
        list.reset(std::move(rows));
        return true;
    }

    bool changed = false;
    for (size_t index = 0; index < rows.size(); ++index) {
        if (list.at(index) == rows.at(index)) {
            continue;
        }
        list.set(rows.at(index), index);
        changed = true;
    }
    return changed;
}

bool SourceStore::publish_rows(
    SourceRowList& list, const std::vector<const SourceRowState*>& rows
)
{
    if (list.size() != rows.size()) {
        std::vector<SourceRowState> values;
        values.reserve(rows.size());
        for (const SourceRowState* row : rows) {
            values.push_back(*row);
        }
        list.reset(std::move(values));
        return true;
    }

    bool changed = false;
    for (size_t index = 0; index < rows.size(); ++index) {
        if (list.at(index) == *rows.at(index)) {
            continue;
        }
        list.set(*rows.at(index), index);
        changed = true;
    }
    return changed;
}

bool SourceStore::publish(const Locks& locks)
{
    bool changed = false;

    std::vector<const SourceRowState*> online;
    std::vector<const SourceRowState*> local;
    online.reserve(m_entries.size());
    local.reserve(m_entries.size());

    for (Slot& slot : m_entries) {
        const bool selection_locked =
            locks.locked_selection_ids.contains(slot.read().descriptor.id);

        if (slot.needs_projection(locks.install_locked, selection_locked)) {
            changed = publish_vendors(slot.read(), locks.install_locked) || changed;
            slot.store_projection(
                project(slot.read(), locks.install_locked, selection_locked),
                locks.install_locked,
                selection_locked
            );
        }

        if (slot.row().zip_path.empty()) {
            online.push_back(&slot.row());
        } else {
            local.push_back(&slot.row());
        }
    }

    changed = publish_rows(m_online_rows, online) || changed;
    changed = publish_rows(m_local_rows, local) || changed;

    return changed;
}

Biz::IObservableList<SourceRowState>& SourceStore::online_rows()
{
    return m_online_rows;
}

Biz::IObservableList<SourceRowState>& SourceStore::local_rows()
{
    return m_local_rows;
}

// =============================================================================================
// PresetUpdaterController::Transaction
// =============================================================================================

PresetUpdaterController::Transaction::Transaction(PresetUpdaterController& controller) :
    m_controller(controller)
{
    ++m_controller.m_transaction_depth;
}

PresetUpdaterController::Transaction::~Transaction()
{
    if (--m_controller.m_transaction_depth == 0) {
        m_controller.flush();
    }
}

// =============================================================================================
// PresetUpdaterController
// =============================================================================================

PresetUpdaterController::PresetUpdaterController(
    Biz::PresetUpdater::PresetUpdaterInteractor& interactor, ControllerEnvironment environment
) :
    m_interactor(interactor),
    m_environment(std::move(environment)),
    m_activity_reporter(interactor)
{
    if (m_environment.timer_queue != nullptr) {
        m_committer.emplace(*m_environment.timer_queue, [this]() { commit_selection(); });
    }
    m_interactor.add_listener<Biz::PresetUpdater::IPresetUpdaterResultListener>(this);
}

PresetUpdaterController::~PresetUpdaterController()
{
    shutdown();
}

void PresetUpdaterController::shutdown()
{
    if (m_shut_down) {
        return;
    }
    m_shut_down = true;

    if (m_committer.has_value()) {
        m_committer->cancel();
    }

    m_activity_reporter.reset();
    m_show_dialog_callback       = nullptr;
    m_presets_installed_callback = nullptr;
    m_forced_state_callback      = nullptr;
    m_forced.clear();
    m_ledger.clear();

    m_interactor.remove_listener<Biz::PresetUpdater::IPresetUpdaterResultListener>(this);
}

JobId PresetUpdaterController::submit(
    JobKind kind, const std::function<JobId()>& operation, std::string subject
)
{
    if (m_shut_down) {
        return k_invalid_job_id;
    }

    const JobId job_id = operation();
    if (!m_ledger.track(job_id, kind, std::move(subject))) {
        SPDLOG_ERROR("Preset updater refused a {} job.", job_kind_name(kind));
        m_operation_failed = true;
        return k_invalid_job_id;
    }
    return job_id;
}

// -- launch -----------------------------------------------------------------------------------

void PresetUpdaterController::start()
{
    Transaction transaction(*this);

    const JobId job_id = submit(
        JobKind::ForcedCheck, [this]() { return m_interactor.check_forced_reconfigurations(); }
    );

    // Nothing downstream may wait for an answer that cannot come. Nothing was proven forced.
    if (job_id == k_invalid_job_id) {
        notify_forced_state(false);
    }
}

void PresetUpdaterController::start_background_check()
{
    if (m_shut_down) {
        return;
    }

    Transaction transaction(*this);
    start_check(Biz::PresetUpdater::SourceListSync::Fetch, CheckReporting::Silent);
}

void PresetUpdaterController::set_show_dialog_callback(std::function<void()> callback)
{
    m_show_dialog_callback = callback;
    m_activity_reporter.set_show_dialog_callback(std::move(callback));
}

void PresetUpdaterController::set_presets_installed_callback(std::function<void()> callback)
{
    m_presets_installed_callback = std::move(callback);
}

void PresetUpdaterController::set_forced_state_callback(
    std::function<void(bool has_forced)> callback
)
{
    m_forced_state_callback = std::move(callback);
}

void PresetUpdaterController::notify_forced_state(bool has_forced)
{
    if (m_forced_state_callback) {
        m_forced_state_callback(has_forced);
    }
}

void PresetUpdaterController::show_dialog()
{
    if (m_shut_down || dialog_open() || !m_show_dialog_callback) {
        return;
    }
    m_show_dialog_callback();
}

// -- dialog -----------------------------------------------------------------------------------

void PresetUpdaterController::on_dialog_opened()
{
    if (m_shut_down) {
        return;
    }
    ++m_open_count;
    if (m_open_count != 1) {
        return;
    }

    Transaction transaction(*this);
    m_activity_reporter.set_dialog_open(true);
    m_warnings.clear();
    m_store.forget_results();
    start_listing();
}

void PresetUpdaterController::on_dialog_closed()
{
    if (m_shut_down || m_open_count == 0) {
        return;
    }
    --m_open_count;
    if (m_open_count > 0) {
        return;
    }

    Transaction transaction(*this);
    m_activity_reporter.set_dialog_open(false);
    if (m_committer.has_value()) {
        m_committer->flush();
    }
}

bool PresetUpdaterController::dialog_open() const
{
    return m_open_count > 0;
}

// -- operations -------------------------------------------------------------------------------

void PresetUpdaterController::start_listing()
{
    cancel_check();
    m_operation_failed = false;

    const JobId job_id = submit(
        JobKind::Listing, [this]() { return m_interactor.list_repositories(online_allowed()); }
    );
    if (job_id != k_invalid_job_id) {
        m_activity_reporter.begin_activity(job_id, Activity::ListingSources);
    }
}

void PresetUpdaterController::start_check(
    Biz::PresetUpdater::SourceListSync source_list, CheckReporting reporting
)
{
    cancel_check();
    m_operation_failed = false;

    const std::set<std::string> required = m_forced.required_repo_ids();
    m_store.set_required_repos(required);
    m_store.begin_check();

    const bool online  = online_allowed();
    const JobId job_id = submit(
        JobKind::Check,
        [this, online, source_list, &required]()
        {
            return m_interactor.build_update_sync_and_reconfiguration_check(
                online, Biz::PresetUpdater::VerboseStyle::NoProgress, false, source_list, required
            );
        }
    );

    if (job_id == k_invalid_job_id) {
        m_store.abandon_check();
        return;
    }

    if (reporting == CheckReporting::Silent) {
        m_activity_reporter.begin_silent_check();
        return;
    }
    m_activity_reporter.begin_activity(job_id, Activity::Checking);
}

void PresetUpdaterController::start_install(const std::vector<VendorKey>& keys)
{
    if (keys.empty()) {
        return;
    }

    Transaction transaction(*this);

    const SourceStore::InstallRequest request = m_store.make_install_request(keys);
    if (request.keys.empty()) {
        return;
    }

    m_operation_failed = false;

    // Submitted before the rows are touched: a refused job must not leave a row running for good.
    const JobId job_id = submit(
        JobKind::Install,
        [this, &request]()
        {
            return m_interactor.perform_reconfigurations(
                request.list, Biz::PresetUpdater::ReconfigurationType::All
            );
        }
    );
    if (job_id == k_invalid_job_id) {
        return;
    }

    m_ledger.set_install_keys(job_id, request.keys);

    // The interactor runs one job at a time, so anything behind another one has not started yet.
    m_store.set_install_state(
        request.keys,
        m_interactor.pending_job_count() > 1 ? InstallState::Queued : InstallState::Running
    );

    m_activity_reporter.begin_activity(job_id, Activity::Installing);
}

void PresetUpdaterController::commit_selection()
{
    if (m_shut_down) {
        return;
    }

    Transaction transaction(*this);
    m_operation_failed = false;

    const JobId job_id = submit(
        JobKind::Selection,
        [this]() { return m_interactor.apply_repository_selection(m_store.selection()); }
    );
    if (m_committer.has_value()) {
        if (job_id == k_invalid_job_id) {
            // The store now says something the manifest does not. Keep owing the write.
            m_committer->mark_unwritten();
        } else {
            m_committer->set_commit_running(true);
        }
    }
    if (job_id == k_invalid_job_id) {
        return;
    }

    m_activity_reporter.begin_activity(job_id, Activity::ApplyingSelection);
}

void PresetUpdaterController::cancel_check()
{
    const JobId job_id = m_ledger.newest(JobKind::Check);
    if (job_id != k_invalid_job_id && m_interactor.is_job_pending(job_id)) {
        m_interactor.cancel_job(job_id);
    }
}

// -- commands ---------------------------------------------------------------------------------

void PresetUpdaterController::set_source_selected(const std::string& uuid, bool selected)
{
    if (m_shut_down) {
        return;
    }

    Transaction transaction(*this);
    if (!m_store.set_selected(uuid, selected)) {
        return;
    }

    cancel_check();
    if (m_committer.has_value()) {
        m_committer->touch();
        return;
    }
    // Without a timer there is nothing to debounce with, and an edit that is never written is the
    // one outcome that must not happen quietly.
    commit_selection();
}

void PresetUpdaterController::add_local_repository(const boost::filesystem::path& zip_path)
{
    if (m_shut_down || forced_mode()) {
        return;
    }

    Transaction transaction(*this);
    cancel_check();
    m_operation_failed = false;

    const JobId job_id = submit(
        JobKind::AddSource,
        [this, &zip_path]() { return m_interactor.add_local_repository(zip_path); },
        zip_path.filename().string()
    );
    if (job_id != k_invalid_job_id) {
        m_activity_reporter.begin_activity(job_id, Activity::AddingSource);
    }
}

void PresetUpdaterController::remove_local_repository(const std::string& uuid)
{
    if (m_shut_down || forced_mode()) {
        return;
    }

    Transaction transaction(*this);
    cancel_check();
    m_operation_failed = false;

    const JobId job_id = submit(
        JobKind::RemoveSource,
        [this, &uuid]() { return m_interactor.remove_local_repository(uuid); },
        m_store.name_of(uuid)
    );
    if (job_id != k_invalid_job_id) {
        m_activity_reporter.begin_activity(job_id, Activity::RemovingSource);
    }
}

void PresetUpdaterController::update_vendor(
    const std::string& repo_id, const std::string& vendor_id
)
{
    const VendorKey key{repo_id, vendor_id};
    if (forced_mode() && !m_forced.requires_vendor(key)) {
        return;
    }
    start_install({key});
}

void PresetUpdaterController::update_source(const std::string& uuid)
{
    start_install(m_store.actionable_keys_of_source(uuid, forced_mode()));
}

void PresetUpdaterController::update_everything()
{
    start_install(m_store.actionable_keys(forced_mode()));
}

void PresetUpdaterController::update_required()
{
    start_install(m_store.actionable_keys(true));
}

// -- queries ----------------------------------------------------------------------------------

ControllerActivity PresetUpdaterController::activity() const
{
    if (m_ledger.any_pending(JobKind::Install)) {
        return ControllerActivity::Installing;
    }
    if (m_ledger.any_pending(JobKind::Check) || m_ledger.any_pending(JobKind::ForcedCheck)) {
        return ControllerActivity::Checking;
    }
    if (m_ledger.any_pending(JobKind::Listing) || m_ledger.any_pending(JobKind::Selection)
        || m_ledger.any_pending(JobKind::AddSource) || m_ledger.any_pending(JobKind::RemoveSource))
    {
        return ControllerActivity::ListingSources;
    }
    return ControllerActivity::None;
}

bool PresetUpdaterController::busy() const
{
    return activity() != ControllerActivity::None;
}

bool PresetUpdaterController::warned() const
{
    return m_operation_failed || m_store.any_check_failed();
}

bool PresetUpdaterController::online_allowed() const
{
    return !m_environment.online_allowed || m_environment.online_allowed();
}

bool PresetUpdaterController::has_actionable_updates() const
{
    return m_store.any_actionable(false);
}

bool PresetUpdaterController::has_required_updates() const
{
    return m_store.any_actionable(true);
}

bool PresetUpdaterController::forced_mode() const
{
    return m_forced.active();
}

bool PresetUpdaterController::fully_checked() const
{
    return m_store.every_active_checked();
}

Biz::IObservableList<SourceRowState>& PresetUpdaterController::online_sources()
{
    return m_store.online_rows();
}

Biz::IObservableList<SourceRowState>& PresetUpdaterController::local_sources()
{
    return m_store.local_rows();
}

// -- publishing -------------------------------------------------------------------------------

SourceStore::Locks PresetUpdaterController::current_locks() const
{
    SourceStore::Locks locks;
    locks.install_locked       = forced_mode();
    locks.locked_selection_ids = m_store.installing_ids();
    return locks;
}

void PresetUpdaterController::report_problems()
{
    std::vector<PresetUpdaterActivityReporter::Problem> problems;
    for (const PresetUpdaterWarning& warning : m_warnings) {
        const SourceStore::RepoLookup lookup = m_store.lookup_repo(warning.repo);
        if (lookup.visibility == SourceStore::RepoVisibility::Deselected) {
            continue;
        }
        problems.push_back(
            PresetUpdaterActivityReporter::Problem{warning.reason, lookup.name, warning.vendor}
        );
    }
    m_activity_reporter.report_problems(std::move(problems));
}

PresetUpdaterController::Snapshot PresetUpdaterController::snapshot() const
{
    return Snapshot{
        activity(),
        warned(),
        forced_mode(),
        fully_checked(),
        has_actionable_updates(),
        has_required_updates(),
        online_allowed()
    };
}

void PresetUpdaterController::flush()
{
    if (m_shut_down) {
        return;
    }

    m_store.set_required_repos(m_forced.required_repo_ids());

    const bool rows_changed = m_store.publish(current_locks());
    report_problems();

    const Snapshot current     = snapshot();
    const bool answers_changed = !m_published.has_value() || *m_published != current;

    // Never on the first publish: that would report "nothing is forced" before anything checked.
    const bool forced_released =
        m_published.has_value() && m_published->forced_mode && !current.forced_mode;

    m_published = current;

    if (!rows_changed && !answers_changed) {
        return;
    }

    invoke_listeners<IPresetUpdaterControllerListener>(
        [](IPresetUpdaterControllerListener* listener) { listener->on_preset_updater_changed(); }
    );

    if (m_environment.request_render) {
        m_environment.request_render();
    }

    // Last: the listeners above take the forced dialog down, and the release builds on top of that.
    if (forced_released) {
        notify_forced_state(false);
    }
}

// -- IPresetUpdaterResultListener ---------------------------------------------------------------

void PresetUpdaterController::on_preset_updater_error(
    JobId job_id, const std::string& body, Biz::PresetUpdater::PresetUpdaterReason reason
)
{
    Transaction transaction(*this);

    SPDLOG_ERROR("Preset updater job {} failed: {}", std::to_string(job_id), body);

    const JobLedger::Record* record = m_ledger.find(job_id);
    m_activity_reporter.report_error(reason, record == nullptr ? std::string{} : record->subject);

    if (record != nullptr && record->kind == JobKind::Install) {
        for (const VendorKey& key : record->keys) {
            m_store.set_install_failed(key, body);
        }
    }

    m_operation_failed = true;
}

void PresetUpdaterController::on_preset_updater_forced_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    Transaction transaction(*this);

    log_reconfigurations(reconfigurations);
    log_warnings(warnings);

    if (!m_ledger.is_newest(job_id)) {
        return;
    }

    m_warnings = warnings;
    m_forced.reset(reconfigurations);

    if (m_forced.active()) {
        // Opening the dialog starts the listing and the check these vendors need.
        show_dialog();
    }
}

void PresetUpdaterController::on_preset_updater_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings,
    Biz::PresetUpdater::VerboseStyle verbose
)
{
    Transaction transaction(*this);

    log_reconfigurations(reconfigurations);
    log_warnings(warnings);

    // A superseded check reports over fresher state, so its answer is not the current answer.
    if (!m_ledger.is_newest(job_id)) {
        return;
    }

    m_warnings = warnings;

    const SourceStore::CheckOutcome outcome =
        SourceStore::CheckOutcome::from(reconfigurations, warnings);
    const std::set<std::string> evaluated = m_store.apply_check(outcome);
    m_forced.reconcile(reconfigurations, evaluated, outcome.unjudged_keys());

    if (forced_mode()) {
        show_dialog();
        return;
    }

    // Only what is on offer: presets of a version the source does not list cannot be installed.
    m_activity_reporter.report_check_finished(
        reconfigurations.regular_updates().size() + reconfigurations.new_vendors().size()
    );
}

void PresetUpdaterController::on_preset_updater_reconfigurations_performed(
    JobId job_id, const std::vector<PresetUpdaterWarning>& warnings
)
{
    Transaction transaction(*this);

    log_warnings(warnings);

    // Above the lookup below: presets are on disk whoever asked for the install.
    if (m_presets_installed_callback) {
        m_presets_installed_callback();
    }

    const JobLedger::Record* record = m_ledger.find(job_id);
    if (record == nullptr || record->kind != JobKind::Install) {
        return;
    }

    m_warnings = warnings;

    // A warning naming a vendor is that one's install having failed; the others went through.
    const auto failure_of = [&warnings](const VendorKey& key)
    {
        return std::find_if(
            warnings.begin(),
            warnings.end(),
            [&key](const PresetUpdaterWarning& warning) {
                return warning.repo == key.repo_id && warning.vendor == key.vendor_id;
            }
        );
    };

    std::vector<VendorKey> succeeded;
    for (const VendorKey& key : record->keys) {
        const auto failure = failure_of(key);
        if (failure != warnings.end()) {
            m_store.set_install_failed(key, failure->text);
            continue;
        }
        succeeded.push_back(key);
    }

    m_activity_reporter.report_install_finished(m_store.installed_vendors_for(succeeded));

    for (const VendorKey& key : succeeded) {
        m_store.set_install_done(key);
        m_forced.mark_satisfied(key);
    }
}

void PresetUpdaterController::on_preset_updater_status(
    JobId job_id,
    const std::string& target,
    int attempt,
    unsigned delay,
    Biz::PresetUpdater::VerboseStyle verbose
)
{
    Transaction transaction(*this);

    SPDLOG_INFO("Preset updater status: target:{} attempt:{} delay:{}", target, attempt, delay);

    m_activity_reporter.report_progress(job_id, target, attempt);

    const JobLedger::Record* record = m_ledger.find(job_id);
    if (record == nullptr || record->kind != JobKind::Install) {
        return;
    }

    m_store.promote_queued(record->keys);
}

void PresetUpdaterController::on_preset_updater_repository_info_vector(
    JobId job_id,
    const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    Transaction transaction(*this);

    log_warnings(warnings);

    if (!m_ledger.is_newest(job_id)) {
        return;
    }

    m_warnings = warnings;
    m_store.reset_from(descriptor, m_committer.has_value() && m_committer->pending());

    if (m_store.needs_check()) {
        start_check(Biz::PresetUpdater::SourceListSync::UseStored, CheckReporting::Reported);
    }
}

void PresetUpdaterController::on_preset_updater_repository_selection_performed(
    JobId job_id,
    const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    Transaction transaction(*this);

    log_warnings(warnings);

    if (!m_ledger.is_newest(job_id)) {
        return;
    }

    m_warnings = warnings;

    const bool edited_again = m_committer.has_value() && m_committer->pending();
    m_store.reset_from(descriptor, edited_again);

    // The follow-up commit is issued when this job finishes; check once it has settled.
    if (edited_again) {
        return;
    }

    if (m_store.needs_check()) {
        start_check(Biz::PresetUpdater::SourceListSync::UseStored, CheckReporting::Reported);
    }
}

void PresetUpdaterController::on_preset_updater_job_finished(
    JobId job_id, Biz::PresetUpdater::JobState state
)
{
    Transaction transaction(*this);

    m_activity_reporter.end_activity(job_id);

    const JobLedger::Record* record = m_ledger.find(job_id);
    if (record == nullptr) {
        return;
    }

    const JobKind kind    = record->kind;
    const bool was_newest = m_ledger.is_newest(job_id);

    switch (kind) {
    case JobKind::Install:
        if (state != Biz::PresetUpdater::JobState::Succeeded) {
            m_store.abandon_install(
                record->keys, state == Biz::PresetUpdater::JobState::Canceled
            );
        }
        break;
    case JobKind::Check:
        // A cancelled check is left alone: a replacement is already on its way.
        if (was_newest && state == Biz::PresetUpdater::JobState::Failed) {
            m_store.abandon_check();
        }
        break;
    default:
        break;
    }

    m_ledger.forget(job_id);

    if (kind == JobKind::Selection && m_committer.has_value()) {
        m_committer->on_commit_finished();
    }

    if (kind == JobKind::ForcedCheck && was_newest) {
        notify_forced_state(forced_mode());
    }
}

} // namespace Slic3r::App::PresetUpdater
