#include "Slic3r/Biz/PresetUpdater/TestPresetUpdaterListener.hpp"

#include <nlohmann/json.hpp>
#include "Slic3r/Assert.hpp"
#include <catch2/catch_test_macros.hpp>

namespace Slic3r::Biz::PresetUpdater {
void to_json(nlohmann::json& j, const PresetUpdaterWarning& w) {
    j = nlohmann::json{
        {"text", w.text},
        {"repo", w.repo},
        {"vendor", w.vendor}
    };
}

void from_json(const nlohmann::json& j, PresetUpdaterWarning& w) {
    j.at("text").get_to(w.text);
    j.at("repo").get_to(w.repo);
    j.at("vendor").get_to(w.vendor);
}
}

namespace Slic3r::Biz::PresetUpdater {
namespace {

std::string reason_name(PresetUpdaterReason reason)
{
    switch (reason) {
    case PresetUpdaterReason::NoUsableVersion:       return "NoUsableVersion";
    case PresetUpdaterReason::IndexUnreadable:       return "IndexUnreadable";
    case PresetUpdaterReason::DataUnreadable:        return "DataUnreadable";
    case PresetUpdaterReason::DataCorrupted:         return "DataCorrupted";
    case PresetUpdaterReason::DataInconsistent:      return "DataInconsistent";
    case PresetUpdaterReason::LocalStorageFailed:    return "LocalStorageFailed";
    case PresetUpdaterReason::InstallFailed:         return "InstallFailed";
    case PresetUpdaterReason::SourceUnreachable:     return "SourceUnreachable";
    case PresetUpdaterReason::SourceListUnavailable: return "SourceListUnavailable";
    case PresetUpdaterReason::SourceDropped:         return "SourceDropped";
    case PresetUpdaterReason::ArchiveInvalid:        return "ArchiveInvalid";
    case PresetUpdaterReason::SourceNotFound:        return "SourceNotFound";
    case PresetUpdaterReason::ManifestUnusable:      return "ManifestUnusable";
    case PresetUpdaterReason::DataDirUnusable:       return "DataDirUnusable";
    case PresetUpdaterReason::Internal:              return "Internal";
    }
    return "<unnamed reason>";
}

std::string reason_list(const std::set<PresetUpdaterReason>& reasons)
{
    if (reasons.empty()) {
        return "<none>";
    }
    std::string ret;
    for (const PresetUpdaterReason reason : reasons) {
        if (!ret.empty()) {
            ret += ", ";
        }
        ret += reason_name(reason);
    }
    return ret;
}

} // namespace

TestPresetUpdaterListener::TestPresetUpdaterListener(
    PresetUpdaterInteractor& preset_updater_interactor
) :
    m_preset_updater_interactor(preset_updater_interactor)
{
    m_preset_updater_interactor.add_listener<IPresetUpdaterResultListener>(
        this
    );
}

TestPresetUpdaterListener::~TestPresetUpdaterListener()
{
    m_preset_updater_interactor.remove_listener<IPresetUpdaterResultListener>(this);
}

void TestPresetUpdaterListener::start(ExpectedReconfiguration expected) 
{
    m_has_result = false;
    m_expected = std::move(expected);
    m_preset_updater_interactor.build_update_sync_and_reconfiguration_check(true, Biz::PresetUpdater::VerboseStyle::NoProgress, true);
}

void TestPresetUpdaterListener::check_reasons(const std::vector<PresetUpdaterWarning>& warnings)
{
    std::set<PresetUpdaterReason> reported;
    for (const PresetUpdaterWarning& warning : warnings) {
        reported.insert(warning.reason);
    }
    CHECK(reason_list(reported) == reason_list(m_expected.reasons));
}

void TestPresetUpdaterListener::on_preset_updater_error(
    JobId job_id, const std::string& body, PresetUpdaterReason reason
)
{
    if (m_expected.state != ReconfigurationResult::Error) {
        FAIL("Unexpected " << reason_name(reason) << " error: " << body);
    }
    CHECK(reason_list({reason}) == reason_list(m_expected.reasons));
    m_has_result = true;
}

void TestPresetUpdaterListener::on_preset_updater_forced_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    on_preset_updater_reconfigurations_list(job_id, reconfigurations, warnings,  Biz::PresetUpdater::VerboseStyle::NoProgress);
}

void TestPresetUpdaterListener::on_preset_updater_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings,
    Biz::PresetUpdater::VerboseStyle verbose
)
{
    
    if (!warnings.empty()) {
        // This is being commented out due to too much noise in the channel.
        // It may potentinally hide some problems, but it should be generally ok to hide.
        //nlohmann::json j = warnings;
        //printf("%s\n",j.dump(4).c_str());
    }
    check_reasons(warnings);
    switch (m_expected.state)
    {
    case ReconfigurationResult::None:
        CHECK(reconfigurations.forced_downgrades().empty());
        CHECK(reconfigurations.forced_updates().empty());
        CHECK(reconfigurations.new_vendors().empty());
        CHECK(reconfigurations.not_in_index().empty());
        CHECK(reconfigurations.regular_updates().empty());
        break;
    case ReconfigurationResult::Update:
        CHECK(reconfigurations.forced_downgrades().empty());
        CHECK(reconfigurations.forced_updates().empty());
        CHECK(reconfigurations.new_vendors().empty());
        CHECK(reconfigurations.not_in_index().empty());

        REQUIRE(reconfigurations.regular_updates().size() == 1);
        CHECK(reconfigurations.regular_updates().front().recommended_version == m_expected.version);
        break;

    case ReconfigurationResult::ForcedUpdate:
        CHECK(reconfigurations.forced_downgrades().empty());
        CHECK(reconfigurations.regular_updates().empty());
        CHECK(reconfigurations.new_vendors().empty());
        CHECK(reconfigurations.not_in_index().empty());

        REQUIRE(reconfigurations.forced_updates().size() == 1);
        CHECK(reconfigurations.forced_updates().front().recommended_version == m_expected.version);
        break;

    case ReconfigurationResult::ForcedDowngrade:
        REQUIRE(reconfigurations.regular_updates().empty());
        REQUIRE(reconfigurations.forced_updates().empty());
        REQUIRE(reconfigurations.new_vendors().empty());
        REQUIRE(reconfigurations.not_in_index().empty());

        REQUIRE(reconfigurations.forced_downgrades().size() == 1);
        CHECK(reconfigurations.forced_downgrades().front().recommended_version == m_expected.version);
        break;
    
    case ReconfigurationResult::NotInIndex:
        CHECK(reconfigurations.regular_updates().empty());
        CHECK(reconfigurations.forced_updates().empty());
        CHECK(reconfigurations.new_vendors().empty());
        CHECK(reconfigurations.forced_downgrades().empty());

        REQUIRE(reconfigurations.not_in_index().size() == 1);
        CHECK(reconfigurations.not_in_index().front().recommended_version == m_expected.version);
        break;

    case ReconfigurationResult::NewVendor:
        CHECK(reconfigurations.regular_updates().empty());
        CHECK(reconfigurations.forced_updates().empty());
        CHECK(reconfigurations.not_in_index().empty());
        CHECK(reconfigurations.forced_downgrades().empty());

        REQUIRE(reconfigurations.new_vendors().size() == 1);
        CHECK(reconfigurations.new_vendors().front().recommended_version == m_expected.version);
        break;
    default:
        FAIL("Expected a job error, got a reconfigurations list.");
        break;
    }
    m_has_result = true;
}

void TestPresetUpdaterListener::on_preset_updater_reconfigurations_performed(
    JobId job_id,
    const std::vector<PresetUpdaterWarning>& warnings
)
{}

void TestPresetUpdaterListener::on_preset_updater_status(
    JobId job_id,
    const std::string& target,
    int attempt,
    unsigned delay,
    Biz::PresetUpdater::VerboseStyle verbose
)
{}

void TestPresetUpdaterListener::on_preset_updater_repository_info_vector(
    JobId job_id,
    const SharedPresetUpdaterRepositoryInfoVector& descriptor,
    const std::vector<PresetUpdaterWarning>& warnings
)
{}

} // namespace Slic3r::Biz::PresetUpdater
