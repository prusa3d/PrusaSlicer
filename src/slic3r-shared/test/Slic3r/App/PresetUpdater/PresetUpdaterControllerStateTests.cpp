#include "Slic3r/App/PresetUpdater/PresetUpdaterController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace Slic3r::App::PresetUpdater;

using Slic3r::Semver;
using Slic3r::App::PresetUpdaterActivityReporter;
using Slic3r::Biz::PresetUpdater::k_invalid_job_id;
using Slic3r::Biz::PresetUpdater::PresetUpdaterReconfigurationList;
using Slic3r::Biz::PresetUpdater::PresetUpdaterRepositoryDescriptor;
using Slic3r::Biz::PresetUpdater::PresetUpdaterWarning;
using Slic3r::Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfo;
using Slic3r::Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector;
using Slic3r::Biz::PresetUpdater::VendorReconfigurationState;

namespace {

const std::string k_repo   = "test_repo";
const std::string k_uuid   = "uuid-test_repo";
const std::string k_vendor = "TestVendor";

SharedPresetUpdaterRepositoryInfo source(
    const std::string& id,
    const std::string& uuid,
    bool selected,
    const boost::filesystem::path& zip_path = {}
)
{
    PresetUpdaterRepositoryDescriptor descriptor;
    descriptor.id       = id;
    descriptor.uuid     = uuid;
    descriptor.name     = id + " (name)";
    descriptor.zip_path = zip_path;
    return SharedPresetUpdaterRepositoryInfo{descriptor, selected};
}

SharedPresetUpdaterRepositoryInfoVector one_selected_source()
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source(k_repo, k_uuid, true));
    return sources;
}

void offer(
    PresetUpdaterReconfigurationList& list,
    VendorReconfigurationState state,
    const std::string& repo_id,
    const std::string& vendor_id,
    const Semver& from = Semver{1, 0, 0},
    const Semver& to   = Semver{1, 0, 1}
)
{
    list.emplace_back(state, vendor_id, repo_id, from, to, std::string{});
}

PresetUpdaterWarning warning(const std::string& repo_id, const std::string& vendor_id)
{
    PresetUpdaterWarning value;
    value.text   = "something went wrong";
    value.repo   = repo_id;
    value.vendor = vendor_id;
    return value;
}

SourceStore::CheckOutcome outcome_of(
    const PresetUpdaterReconfigurationList& list,
    const std::vector<PresetUpdaterWarning>& warnings = {}
)
{
    return SourceStore::CheckOutcome::from(list, warnings);
}

/// Puts a store through one full listing-and-check round for a single selected online source.
void run_check(SourceStore& store, const PresetUpdaterReconfigurationList& list)
{
    store.reset_from(one_selected_source(), false);
    store.begin_check();
    store.apply_check(outcome_of(list));
}

const VendorRowState& vendor_row(SourceStore& store, size_t index)
{
    return store.online_rows().at(0).vendors->at(index);
}

} // namespace

// =================================================================================================
// JobLedger
// =================================================================================================

TEST_CASE("JobLedger refuses a job the interactor would not take", "[preset_updater][controller]")
{
    JobLedger ledger;

    CHECK_FALSE(ledger.track(k_invalid_job_id, JobKind::Check));
    CHECK(ledger.find(k_invalid_job_id) == nullptr);
    CHECK_FALSE(ledger.any_pending(JobKind::Check));
    CHECK(ledger.newest(JobKind::Check) == k_invalid_job_id);
}

TEST_CASE("JobLedger treats only the last job of a kind as current", "[preset_updater][controller]")
{
    JobLedger ledger;

    REQUIRE(ledger.track(1, JobKind::Check));
    REQUIRE(ledger.track(2, JobKind::Check));
    REQUIRE(ledger.track(3, JobKind::Listing));

    CHECK_FALSE(ledger.is_newest(1));
    CHECK(ledger.is_newest(2));
    CHECK(ledger.is_newest(3));
    CHECK(ledger.newest(JobKind::Check) == 2);
    CHECK(ledger.newest(JobKind::Listing) == 3);
    CHECK(ledger.newest(JobKind::Install) == k_invalid_job_id);
    CHECK_FALSE(ledger.is_newest(99));
}

TEST_CASE("JobLedger stops reporting a kind pending once it is forgotten", "[preset_updater][controller]")
{
    JobLedger ledger;

    REQUIRE(ledger.track(1, JobKind::Check));
    REQUIRE(ledger.track(2, JobKind::Check));
    CHECK(ledger.any_pending(JobKind::Check));

    ledger.forget(2);
    CHECK(ledger.any_pending(JobKind::Check));
    CHECK(ledger.newest(JobKind::Check) == k_invalid_job_id);

    ledger.forget(1);
    CHECK_FALSE(ledger.any_pending(JobKind::Check));
    CHECK(ledger.find(1) == nullptr);
}

TEST_CASE("JobLedger remembers what an install was for", "[preset_updater][controller]")
{
    JobLedger ledger;

    REQUIRE(ledger.track(1, JobKind::Install, "subject"));
    ledger.set_install_keys(1, {VendorKey{k_repo, k_vendor}});
    ledger.set_install_keys(2, {VendorKey{k_repo, "Untracked"}});

    const JobLedger::Record* record = ledger.find(1);
    REQUIRE(record != nullptr);
    CHECK(record->subject == "subject");
    REQUIRE(record->keys.size() == 1);
    CHECK(record->keys.front() == VendorKey{k_repo, k_vendor});
    CHECK(ledger.find(2) == nullptr);
}

TEST_CASE("JobLedger clears everything at once", "[preset_updater][controller]")
{
    JobLedger ledger;

    REQUIRE(ledger.track(1, JobKind::Check));
    REQUIRE(ledger.track(2, JobKind::Install));
    ledger.clear();

    CHECK_FALSE(ledger.any_pending(JobKind::Check));
    CHECK_FALSE(ledger.any_pending(JobKind::Install));
    CHECK(ledger.newest(JobKind::Install) == k_invalid_job_id);
}

// =================================================================================================
// ForcedReconfigurations
// =================================================================================================

TEST_CASE("ForcedReconfigurations takes only what blocks the application", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, "Regular");
    offer(list, VendorReconfigurationState::NewVendor, k_repo, "Fresh");
    offer(list, VendorReconfigurationState::NotInIndex, k_repo, "Unlisted");
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, "TooOld");
    offer(list, VendorReconfigurationState::ForcedDowngrade, k_repo, "TooNew");

    ForcedReconfigurations forced;
    forced.reset(list);

    CHECK(forced.active());
    CHECK(forced.required().size() == 2);
    CHECK(forced.requires_vendor(VendorKey{k_repo, "TooOld"}));
    CHECK(forced.requires_vendor(VendorKey{k_repo, "TooNew"}));
    CHECK_FALSE(forced.requires_vendor(VendorKey{k_repo, "Unlisted"}));
    CHECK_FALSE(forced.requires_vendor(VendorKey{k_repo, "Regular"}));
}

TEST_CASE("ForcedReconfigurations releases a source only on evidence", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList initial;
    offer(initial, VendorReconfigurationState::ForcedUpdate, "repo_a", "VendorA");
    offer(initial, VendorReconfigurationState::ForcedUpdate, "repo_b", "VendorB");

    ForcedReconfigurations forced;
    forced.reset(initial);

    SECTION("a source the check evaluated and did not report is resolved")
    {
        forced.reconcile({}, {"repo_a"}, {});
        CHECK_FALSE(forced.requires_vendor(VendorKey{"repo_a", "VendorA"}));
        CHECK(forced.requires_vendor(VendorKey{"repo_b", "VendorB"}));
    }

    SECTION("a check that evaluated nothing releases nothing")
    {
        forced.reconcile({}, {}, {});
        CHECK(forced.required().size() == 2);
    }

    SECTION("a source the check still reports stays required")
    {
        PresetUpdaterReconfigurationList again;
        offer(again, VendorReconfigurationState::ForcedUpdate, "repo_a", "VendorA");
        forced.reconcile(again, {"repo_a", "repo_b"}, {});
        CHECK(forced.requires_vendor(VendorKey{"repo_a", "VendorA"}));
        CHECK_FALSE(forced.requires_vendor(VendorKey{"repo_b", "VendorB"}));
    }

    SECTION("a vendor the check could not judge is kept whatever the rest says")
    {
        forced.reconcile({}, {"repo_a", "repo_b"}, {VendorKey{"repo_a", "VendorA"}});
        CHECK(forced.requires_vendor(VendorKey{"repo_a", "VendorA"}));
        CHECK_FALSE(forced.requires_vendor(VendorKey{"repo_b", "VendorB"}));
    }

    SECTION("a newly reported vendor is added and not duplicated")
    {
        PresetUpdaterReconfigurationList again;
        offer(again, VendorReconfigurationState::ForcedUpdate, "repo_a", "VendorA");
        offer(again, VendorReconfigurationState::ForcedDowngrade, "repo_c", "VendorC");
        forced.reconcile(again, {"repo_a"}, {});
        forced.reconcile(again, {"repo_a"}, {});
        CHECK(forced.required().size() == 3);
        CHECK(forced.requires_vendor(VendorKey{"repo_c", "VendorC"}));
    }
}

TEST_CASE("ForcedReconfigurations holds the application whatever the sources say", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, k_vendor);

    ForcedReconfigurations forced;
    forced.reset(list);

    CHECK(forced.active());
    CHECK(forced.required_repo_ids() == std::set<std::string>{k_repo});

    forced.mark_satisfied(VendorKey{k_repo, k_vendor});
    CHECK_FALSE(forced.active());
    CHECK(forced.required_repo_ids().empty());
}

TEST_CASE("ForcedReconfigurations counts a removal as blocking", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::RemoveVendor, k_repo, k_vendor);

    ForcedReconfigurations forced;
    forced.reset(list);

    CHECK(forced.active());
    CHECK(forced.requires_vendor(VendorKey{k_repo, k_vendor}));
}

TEST_CASE("SourceStore offers a removal as a required action", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::RemoveVendor, k_repo, k_vendor);

    SourceStore store;
    run_check(store, list);
    store.publish(SourceStore::Locks{});

    CHECK(store.online_rows().at(0).counts.required == 1);
    CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::HasUpdates);
    CHECK(store.actionable_keys(true) == std::vector<VendorKey>{VendorKey{k_repo, k_vendor}});
    CHECK(store.make_install_request({VendorKey{k_repo, k_vendor}}).list.removals().size() == 1);
}

TEST_CASE("ForcedReconfigurations names every source it needs once", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::ForcedUpdate, "repo_a", "VendorA");
    offer(list, VendorReconfigurationState::ForcedDowngrade, "repo_a", "VendorB");
    offer(list, VendorReconfigurationState::ForcedUpdate, "repo_b", "VendorC");

    ForcedReconfigurations forced;
    forced.reset(list);

    CHECK(forced.required_repo_ids() == std::set<std::string>{"repo_a", "repo_b"});
}

// =================================================================================================
// SourceStore
// =================================================================================================

TEST_CASE("SourceStore splits sources by where they come from", "[preset_updater][controller]")
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source("online_repo", "uuid-online", true));
    sources.push_back(source("local_repo", "uuid-local", false, "somewhere/local.zip"));

    SourceStore store;
    CHECK_FALSE(store.listed());
    store.reset_from(sources, false);
    CHECK(store.listed());

    CHECK(store.publish(SourceStore::Locks{}));
    REQUIRE(store.online_rows().size() == 1);
    REQUIRE(store.local_rows().size() == 1);
    CHECK(store.online_rows().at(0).id == "online_repo");
    CHECK(store.online_rows().at(0).selected);
    CHECK(store.local_rows().at(0).id == "local_repo");
    CHECK_FALSE(store.local_rows().at(0).selected);
    CHECK(store.name_of("uuid-local") == "local_repo (name)");
}

TEST_CASE("SourceStore publishes nothing when nothing moved", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);

    CHECK(store.publish(SourceStore::Locks{}));
    CHECK_FALSE(store.publish(SourceStore::Locks{}));

    SourceStore::Locks locked;
    locked.install_locked = true;
    CHECK(store.publish(locked));
    CHECK_FALSE(store.publish(locked));
    CHECK(store.online_rows().at(0).install_locked);
}

TEST_CASE("SourceStore lets an uncommitted selection edit survive a listing", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);
    REQUIRE(store.set_selected(k_uuid, false));

    SECTION("the edit wins while it is still owed a write")
    {
        store.reset_from(one_selected_source(), true);
        store.publish(SourceStore::Locks{});
        CHECK_FALSE(store.online_rows().at(0).selected);
    }

    SECTION("the manifest wins once nothing is owed")
    {
        store.reset_from(one_selected_source(), false);
        store.publish(SourceStore::Locks{});
        CHECK(store.online_rows().at(0).selected);
    }
}

TEST_CASE("SourceStore owes a check for a source that has no result", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);

    CHECK(store.needs_check());
    CHECK_FALSE(store.every_active_checked());

    store.begin_check();
    CHECK_FALSE(store.needs_check());
    CHECK_FALSE(store.every_active_checked());
    store.publish(SourceStore::Locks{});
    CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::Checking);

    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::NewVendor, k_repo, k_vendor);
    const std::set<std::string> evaluated = store.apply_check(outcome_of(list));

    CHECK(evaluated == std::set<std::string>{k_repo});
    CHECK(store.every_active_checked());
    CHECK_FALSE(store.needs_check());
}

TEST_CASE("SourceStore puts a dropped check back where it can be retried", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);
    store.begin_check();

    SECTION("a source with no result yet is owed one again")
    {
        store.abandon_check();
        CHECK(store.needs_check());
    }

    SECTION("a source that had a result keeps it")
    {
        PresetUpdaterReconfigurationList list;
        offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
        store.apply_check(outcome_of(list));

        store.begin_check();
        store.abandon_check();

        CHECK_FALSE(store.needs_check());
        CHECK(store.every_active_checked());
        store.publish(SourceStore::Locks{});
        CHECK(store.online_rows().at(0).counts.updates == 1);
    }
}

TEST_CASE("SourceStore counts what a check reported", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, "Regular");
    offer(list, VendorReconfigurationState::NewVendor, k_repo, "Fresh");
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, "TooOld");
    offer(list, VendorReconfigurationState::NotInIndex, k_repo, "Unlisted");

    SourceStore store;
    run_check(store, list);
    store.publish(SourceStore::Locks{});

    const SourceRowState& row = store.online_rows().at(0);
    CHECK(row.update_state == SourceRowState::UpdateState::HasUpdates);
    CHECK(row.counts.updates == 1);
    CHECK(row.counts.new_vendors == 1);
    CHECK(row.counts.required == 1);
    CHECK(row.counts.unknown_versions == 1);
    CHECK(row.counts.pending() == 4);
    CHECK(row.counts.attention() == 2);
    CHECK_FALSE(row.check_failed);

    REQUIRE(row.vendors != nullptr);
    CHECK(row.vendors->size() == 4);
    CHECK(store.any_actionable(false));
    CHECK(store.any_actionable(true));
    CHECK(store.actionable_keys(false).size() == 4);
    CHECK(store.actionable_keys(true).size() == 1);
    CHECK(store.actionable_keys_of_source(k_uuid, true).size() == 1);
    CHECK(store.actionable_keys_of_source("nonexistent", false).empty());
}

TEST_CASE("SourceStore reports a source with nothing to do as up to date", "[preset_updater][controller]")
{
    SourceStore store;
    run_check(store, {});
    store.publish(SourceStore::Locks{});

    CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::UpToDate);
    CHECK_FALSE(store.any_actionable(false));
    CHECK_FALSE(store.any_check_failed());
}

TEST_CASE("SourceStore tells a failed source from a skipped vendor", "[preset_updater][controller]")
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source("broken_repo", "uuid-broken", true));
    sources.push_back(source("partial_repo", "uuid-partial", true));

    SourceStore store;
    store.reset_from(sources, false);
    store.begin_check();

    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, "partial_repo", "Good");

    const std::set<std::string> evaluated = store.apply_check(
        outcome_of(list, {warning("broken_repo", {}), warning("partial_repo", "Bad")})
    );

    CHECK(evaluated == std::set<std::string>{"partial_repo"});
    CHECK(store.any_check_failed());
    CHECK(store.every_active_checked());

    store.publish(SourceStore::Locks{});
    REQUIRE(store.online_rows().size() == 2);

    const SourceRowState& broken = store.online_rows().at(0);
    CHECK(broken.id == "broken_repo");
    CHECK(broken.check_failed);

    const SourceRowState& partial = store.online_rows().at(1);
    CHECK_FALSE(partial.check_failed);
    CHECK(partial.skipped_vendors == std::vector<std::string>{"Bad"});
    CHECK(partial.counts.updates == 1);
    REQUIRE(partial.vendors->size() == 2);
    CHECK(store.actionable_keys(false).size() == 1);
}

TEST_CASE("SourceStore forgets the results of a source once it is deselected", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);

    SourceStore store;
    run_check(store, list);
    store.publish(SourceStore::Locks{});
    REQUIRE(store.online_rows().at(0).counts.updates == 1);

    REQUIRE(store.set_selected(k_uuid, false));
    CHECK(store.publish(SourceStore::Locks{}));

    const SourceRowState& row = store.online_rows().at(0);
    CHECK_FALSE(row.selected);
    CHECK(row.update_state == SourceRowState::UpdateState::NotChecked);
    CHECK(row.counts.pending() == 0);
    CHECK(row.vendors->size() == 0);
    CHECK_FALSE(store.needs_check());
    CHECK_FALSE(store.has_selected_with_id(k_repo));
}

TEST_CASE("SourceStore checks a deselected source a forced reconfiguration needs", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, k_vendor);

    SourceStore store;
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source(k_repo, k_uuid, false));
    store.reset_from(sources, false);

    REQUIRE_FALSE(store.needs_check());

    store.set_required_repos({k_repo});
    CHECK(store.needs_check());
    CHECK_FALSE(store.every_active_checked());

    store.begin_check();
    const std::set<std::string> evaluated = store.apply_check(outcome_of(list));
    CHECK(evaluated == std::set<std::string>{k_repo});
    CHECK(store.every_active_checked());

    store.publish(SourceStore::Locks{});
    const SourceRowState& row = store.online_rows().at(0);
    CHECK_FALSE(row.selected);
    CHECK(row.required);
    CHECK(row.update_state == SourceRowState::UpdateState::HasUpdates);
    CHECK(row.counts.required == 1);

    CHECK(store.actionable_keys(true) == std::vector<VendorKey>{VendorKey{k_repo, k_vendor}});
    CHECK(store.make_install_request({VendorKey{k_repo, k_vendor}}).keys.size() == 1);

    // Required is not selected, and the commit must not tick it.
    CHECK_FALSE(store.has_selected_with_id(k_repo));
    REQUIRE(store.selection().size() == 1);
    CHECK_FALSE(store.selection().front().selected);
    CHECK(store.lookup_repo(k_repo).visibility == SourceStore::RepoVisibility::Required);
}

TEST_CASE("SourceStore forgets a source once it is no longer required", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, k_vendor);

    SourceStore store;
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source(k_repo, k_uuid, false));
    store.reset_from(sources, false);
    store.set_required_repos({k_repo});
    store.begin_check();
    store.apply_check(outcome_of(list));
    store.publish(SourceStore::Locks{});
    REQUIRE(store.online_rows().at(0).counts.required == 1);

    store.set_required_repos({});
    CHECK(store.publish(SourceStore::Locks{}));

    const SourceRowState& row = store.online_rows().at(0);
    CHECK(row.update_state == SourceRowState::UpdateState::NotChecked);
    CHECK(row.counts.pending() == 0);
    CHECK(store.actionable_keys(true).empty());
    CHECK(store.lookup_repo(k_repo).visibility == SourceStore::RepoVisibility::Deselected);
}

TEST_CASE("SourceStore marks one source per required id", "[preset_updater][controller]")
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source(k_repo, "uuid-online", false));
    sources.push_back(source(k_repo, "uuid-local", false, "somewhere/local.zip"));

    SourceStore store;

    SECTION("with none selected, the first one stands for the id")
    {
        store.reset_from(sources, false);
        store.set_required_repos({k_repo});
        store.begin_check();
        store.publish(SourceStore::Locks{});
        CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::Checking);
        CHECK(store.local_rows().at(0).update_state == SourceRowState::UpdateState::NotChecked);
    }

    SECTION("a selected namesake is the one that stands for the id")
    {
        sources[1].selected = true;
        store.reset_from(sources, false);
        store.set_required_repos({k_repo});
        store.begin_check();
        store.publish(SourceStore::Locks{});
        CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::NotChecked);
        CHECK(store.local_rows().at(0).update_state == SourceRowState::UpdateState::Checking);
    }

    SECTION("the one standing for the id keeps it when it is switched off")
    {
        sources[1].selected = true;
        store.reset_from(sources, false);
        store.set_required_repos({k_repo});
        REQUIRE(store.set_selected("uuid-local", false));

        store.publish(SourceStore::Locks{});
        CHECK_FALSE(store.online_rows().at(0).required);
        CHECK(store.local_rows().at(0).required);
    }

    SECTION("a listing does not hand the id to the other one either")
    {
        sources[1].selected = true;
        store.reset_from(sources, false);
        store.set_required_repos({k_repo});
        REQUIRE(store.set_selected("uuid-local", false));

        sources[1].selected = false;
        store.reset_from(sources, false);

        store.publish(SourceStore::Locks{});
        CHECK_FALSE(store.online_rows().at(0).required);
        CHECK(store.local_rows().at(0).required);
    }
}

TEST_CASE("SourceStore owes a fresh check once a source it keeps is switched", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::ForcedUpdate, k_repo, k_vendor);

    SourceStore store;
    store.reset_from(one_selected_source(), false);
    store.set_required_repos({k_repo});
    store.begin_check();
    store.apply_check(outcome_of(list));
    REQUIRE(store.every_active_checked());
    REQUIRE(store.actionable_keys(true).size() == 1);

    REQUIRE(store.set_selected(k_uuid, false));

    CHECK(store.needs_check());
    CHECK_FALSE(store.every_active_checked());

    store.publish(SourceStore::Locks{});
    const SourceRowState& row = store.online_rows().at(0);
    CHECK_FALSE(row.selected);
    CHECK(row.required);
    CHECK(row.update_state == SourceRowState::UpdateState::Waiting);
    CHECK(row.vendors->size() == 0);
    CHECK(store.actionable_keys(true).empty());
}

TEST_CASE("CheckOutcome names the vendors it could not judge", "[preset_updater][controller]")
{
    const PresetUpdaterReconfigurationList list;
    const SourceStore::CheckOutcome outcome =
        outcome_of(list, {warning(k_repo, k_vendor), warning(k_repo, {}), warning({}, {})});
    CHECK(outcome.unjudged_keys() == std::vector<VendorKey>{VendorKey{k_repo, k_vendor}});
}

TEST_CASE("SourceStore keeps one selected source per id", "[preset_updater][controller]")
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source(k_repo, "uuid-online", true));
    sources.push_back(source(k_repo, "uuid-local", false, "somewhere/local.zip"));

    SourceStore store;
    store.reset_from(sources, false);
    REQUIRE(store.set_selected("uuid-local", true));

    store.publish(SourceStore::Locks{});
    CHECK_FALSE(store.online_rows().at(0).selected);
    CHECK(store.local_rows().at(0).selected);
    CHECK(store.has_selected_with_id(k_repo));

    const SourceStore::RepoLookup lookup = store.lookup_repo(k_repo);
    CHECK(lookup.visibility == SourceStore::RepoVisibility::Selected);
    CHECK(lookup.name == k_repo + " (name)");
}

TEST_CASE("SourceStore cannot judge a source nothing has listed", "[preset_updater][controller]")
{
    SourceStore store;

    const SourceStore::RepoLookup lookup = store.lookup_repo("unknown_repo");
    CHECK(lookup.visibility == SourceStore::RepoVisibility::Unknown);
    CHECK(lookup.name == "unknown_repo");
}

TEST_CASE("SourceStore refuses to change a selection an install is holding", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);

    SourceStore store;
    run_check(store, list);
    store.set_install_state({VendorKey{k_repo, k_vendor}}, InstallState::Running);

    CHECK(store.installing_ids() == std::set<std::string>{k_repo});
    CHECK(store.selection_locked(k_uuid));
    CHECK_FALSE(store.set_selected(k_uuid, false));

    SourceStore::Locks locks;
    locks.locked_selection_ids = store.installing_ids();
    store.publish(locks);

    CHECK(store.online_rows().at(0).selected);
    CHECK(store.online_rows().at(0).selection_locked);
    CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::Installing);
    CHECK(store.actionable_keys(false).empty());
}

TEST_CASE("SourceStore carries an install through to its verdict", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
    const VendorKey key{k_repo, k_vendor};

    SourceStore store;
    run_check(store, list);
    store.set_install_state({key}, InstallState::Queued);
    store.publish(SourceStore::Locks{});
    CHECK(vendor_row(store, 0).install_state == InstallState::Queued);

    SECTION("a status report promotes it to running")
    {
        store.promote_queued({key});
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Running);
    }

    SECTION("success clears it from the counts and leaves the row behind")
    {
        store.set_install_done(key);
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Done);
        CHECK(store.online_rows().at(0).counts.pending() == 0);
        CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::UpToDate);
        CHECK(store.actionable_keys(false).empty());
    }

    SECTION("failure keeps the vendor offered and names the reason")
    {
        store.set_install_failed(key, "disk full");
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Failed);
        CHECK(vendor_row(store, 0).error_text == "disk full");
        CHECK(store.actionable_keys(false).size() == 1);
    }

    SECTION("a canceled job returns it to idle")
    {
        store.abandon_install({key}, true);
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Idle);
    }

    SECTION("a job that ended without a verdict counts as failed")
    {
        store.abandon_install({key}, false);
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Failed);
    }
}

TEST_CASE("SourceStore does not undo a verdict an install already reached", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
    const VendorKey key{k_repo, k_vendor};

    SourceStore store;
    run_check(store, list);
    store.set_install_done(key);
    store.abandon_install({key}, true);
    store.publish(SourceStore::Locks{});

    CHECK(vendor_row(store, 0).install_state == InstallState::Done);
}

TEST_CASE("SourceStore lets a re-check outlive what an install left behind", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
    const VendorKey key{k_repo, k_vendor};

    SourceStore store;
    run_check(store, list);

    SECTION("a failure the user has not dealt with survives")
    {
        store.set_install_failed(key, "disk full");
        store.begin_check();
        store.apply_check(outcome_of(list));
        store.publish(SourceStore::Locks{});

        CHECK(vendor_row(store, 0).install_state == InstallState::Failed);
        CHECK(vendor_row(store, 0).error_text == "disk full");
    }

    SECTION("a vendor no longer offered keeps its confirmation")
    {
        store.set_install_done(key);
        store.begin_check();
        store.apply_check(outcome_of({}));
        store.publish(SourceStore::Locks{});

        REQUIRE(store.online_rows().at(0).vendors->size() == 1);
        CHECK(vendor_row(store, 0).install_state == InstallState::Done);
        CHECK(store.online_rows().at(0).counts.pending() == 0);
    }

    SECTION("a finished install is offered again when the source offers more")
    {
        store.set_install_done(key);
        store.begin_check();

        PresetUpdaterReconfigurationList newer;
        offer(newer, VendorReconfigurationState::Update, k_repo, k_vendor, Semver{1, 0, 1}, Semver{1, 0, 2});
        store.apply_check(outcome_of(newer));
        store.publish(SourceStore::Locks{});

        CHECK(vendor_row(store, 0).install_state == InstallState::Idle);
        CHECK(vendor_row(store, 0).recommended_version == Semver{1, 0, 2});
        CHECK(store.online_rows().at(0).counts.updates == 1);
    }
}

TEST_CASE("SourceStore forgets a finished round but not a running one", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
    const VendorKey key{k_repo, k_vendor};

    SourceStore store;

    SECTION("everything an earlier check said is dropped")
    {
        run_check(store, list);
        store.set_install_done(key);
        store.forget_results();
        store.publish(SourceStore::Locks{});

        CHECK(store.online_rows().at(0).vendors->size() == 0);
        CHECK(store.online_rows().at(0).counts.pending() == 0);
        CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::Waiting);
        CHECK(store.needs_check());
        CHECK_FALSE(store.every_active_checked());
    }

    SECTION("a failure the user has not dealt with goes with it")
    {
        run_check(store, list);
        store.set_install_failed(key, "disk full");
        store.forget_results();
        store.publish(SourceStore::Locks{});

        CHECK(store.online_rows().at(0).vendors->size() == 0);
        CHECK(store.actionable_keys(false).empty());
    }

    SECTION("a check that failed is owed another one")
    {
        store.reset_from(one_selected_source(), false);
        store.begin_check();
        store.apply_check(outcome_of({}, {warning(k_repo, {})}));
        REQUIRE(store.any_check_failed());

        store.forget_results();
        CHECK_FALSE(store.any_check_failed());
        CHECK(store.needs_check());
    }

    SECTION("an install still running is kept")
    {
        run_check(store, list);
        store.set_install_state({key}, InstallState::Running);
        store.forget_results();
        store.publish(SourceStore::Locks{});

        REQUIRE(store.online_rows().at(0).vendors->size() == 1);
        CHECK(vendor_row(store, 0).install_state == InstallState::Running);
        CHECK(store.online_rows().at(0).update_state == SourceRowState::UpdateState::Installing);
        CHECK(store.installing_ids().contains(k_repo));

        store.set_install_done(key);
        store.publish(SourceStore::Locks{});
        CHECK(vendor_row(store, 0).install_state == InstallState::Done);
    }
}

TEST_CASE("SourceStore builds an install request only from what it knows", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);
    offer(list, VendorReconfigurationState::NewVendor, k_repo, "Fresh", Semver{}, Semver{2, 0, 0});

    SourceStore store;
    run_check(store, list);

    const SourceStore::InstallRequest request = store.make_install_request({
        VendorKey{k_repo, k_vendor},
        VendorKey{k_repo, "Unknown"},
        VendorKey{"other_repo", k_vendor},
        VendorKey{k_repo, "Fresh"}
    });

    REQUIRE(request.keys.size() == 2);
    CHECK(request.keys.at(0) == VendorKey{k_repo, k_vendor});
    CHECK(request.keys.at(1) == VendorKey{k_repo, "Fresh"});
    REQUIRE(request.list.regular_updates().size() == 1);
    REQUIRE(request.list.new_vendors().size() == 1);
    CHECK(request.list.regular_updates().front().vendor_repo_id == k_repo);
    CHECK(request.list.new_vendors().front().recommended_version == Semver{2, 0, 0});

    const std::vector<PresetUpdaterActivityReporter::InstalledVendor> installed =
        store.installed_vendors_for(request.keys);
    REQUIRE(installed.size() == 2);
    CHECK(installed.front().vendor_id == k_vendor);
    CHECK(installed.front().to == Semver{1, 0, 1});
}

TEST_CASE("SourceStore leaves a skipped vendor out of everything actionable", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);
    store.begin_check();
    store.apply_check(outcome_of({}, {warning(k_repo, "Bad")}));
    store.publish(SourceStore::Locks{});

    REQUIRE(store.online_rows().at(0).vendors->size() == 1);
    CHECK(vendor_row(store, 0).skipped);
    CHECK(store.actionable_keys(false).empty());
    CHECK(store.make_install_request({VendorKey{k_repo, "Bad"}}).keys.empty());
    CHECK(store.online_rows().at(0).counts.pending() == 0);
}

TEST_CASE("SourceStore hands back the selection the manifest is to be written with", "[preset_updater][controller]")
{
    SharedPresetUpdaterRepositoryInfoVector sources;
    sources.push_back(source("online_repo", "uuid-online", true));
    sources.push_back(source("local_repo", "uuid-local", false, "somewhere/local.zip"));

    SourceStore store;
    store.reset_from(sources, false);
    REQUIRE(store.set_selected("uuid-local", true));

    const SharedPresetUpdaterRepositoryInfoVector selection = store.selection();
    REQUIRE(selection.size() == 2);
    CHECK(selection.at(0).descriptor.uuid == "uuid-online");
    CHECK(selection.at(0).selected);
    CHECK(selection.at(1).descriptor.uuid == "uuid-local");
    CHECK(selection.at(1).selected);
}

TEST_CASE("SourceStore refuses a selection change to a source it does not have", "[preset_updater][controller]")
{
    SourceStore store;
    store.reset_from(one_selected_source(), false);

    CHECK_FALSE(store.set_selected("uuid-nonexistent", true));
}

TEST_CASE("SourceStore keeps the vendor list of a source across a listing", "[preset_updater][controller]")
{
    PresetUpdaterReconfigurationList list;
    offer(list, VendorReconfigurationState::Update, k_repo, k_vendor);

    SourceStore store;
    run_check(store, list);
    store.publish(SourceStore::Locks{});

    const std::shared_ptr<VendorRowList> vendors = store.online_rows().at(0).vendors;
    REQUIRE(vendors != nullptr);

    store.reset_from(one_selected_source(), false);
    store.publish(SourceStore::Locks{});

    CHECK(store.online_rows().at(0).vendors == vendors);
    CHECK(store.online_rows().at(0).counts.updates == 1);
}
