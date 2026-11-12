#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Biz/Preset/NameValidator.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"

namespace Slic3r::Biz::Preset {

class FakePresetInteractor : public IPresetNameProvider
{
public:
    using PresetNames = Domain::Preset::PresetNames;

    void set_presets(PresetNames presets)
    {
        m_presets = std::move(presets);
    }

    PresetNames get_all_vendor_preset_names(
        Domain::Preset::PresetKind,
        const std::optional<std::string>& vendor_id = std::nullopt
    ) const override
    {
        return m_presets;
    }

    boost::filesystem::path selected_user_preset_path(
        Domain::Preset::PresetKind kind,
        const std::string& preset_name
    ) const override
    {
        namespace fs = boost::filesystem;

        return boost::filesystem::path{
            "/very/long/prefix/" + IO::preset_file_prefix(kind) + preset_name + ".yaml"
        };
    };

private:
    PresetNames m_presets;
};

using PresetName   = Domain::Preset::PresetName;
using PresetOrigin = Domain::Preset::PresetOrigin;
using PresetKind   = Domain::Preset::PresetKind;

PresetName user_preset(std::string name)
{
    return PresetName{std::move(name), {}, {}, PresetOrigin::User};
}

PresetName system_preset(std::string name)
{
    return PresetName{std::move(name), {}, {}, PresetOrigin::System};
}

TEST_CASE("NameValidator: empty name is invalid")
{
    FakePresetInteractor interactor;
    interactor.set_presets({});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Initial", false);

    auto [type, message] = validator.validate("");

    REQUIRE(type == NameValidator::ValidationType::Invalid);
}

TEST_CASE("NameValidator: forbidden characters are rejected")
{
    FakePresetInteractor interactor;
    interactor.set_presets({});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Init", false);

    auto [type, message] = validator.validate("Bad|Name");

    REQUIRE(type == NameValidator::ValidationType::Invalid);
}

TEST_CASE("NameValidator: modified suffix is rejected")
{
    FakePresetInteractor interactor;
    interactor.set_presets({});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Init", false);

    auto [type, message] = validator.validate("MyPreset (modified)");

    REQUIRE(type == NameValidator::ValidationType::Invalid);
}

TEST_CASE("NameValidator: detects conflicts case-insensitively")
{
    FakePresetInteractor interactor;
    interactor.set_presets({user_preset("PLA Quality")});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Init", false);

    REQUIRE(validator.get_conflict_name("pla quality") == "PLA Quality");
}

TEST_CASE("NameValidator: system preset cannot be overwritten")
{
    FakePresetInteractor interactor;
    interactor.set_presets({system_preset("SystemPreset")});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Init", false);

    auto [type, message] = validator.validate("SystemPreset");

    REQUIRE(type == NameValidator::ValidationType::Invalid);
}

TEST_CASE("NameValidator: existing user preset produces warning")
{
    FakePresetInteractor interactor;
    interactor.set_presets({user_preset("MyPreset")});

    NameValidator validator(interactor, PresetKind::FdmPrint, "Other", false);

    auto [type, message] = validator.validate("MyPreset");

    REQUIRE(type == NameValidator::ValidationType::Warning);
}

TEST_CASE("NameValidator: too long user preset name is rejected")
{
    FakePresetInteractor interactor;
    interactor.set_presets({});

    NameValidator validator(interactor, PresetKind::FdmPrint, "SomeName", false);

    std::string long_name(300, 'a');
    auto [type, _] = validator.validate(long_name);

    REQUIRE(type == NameValidator::ValidationType::Invalid);
}

TEST_CASE("NameValidator: renaming to same name is valid")
{
    FakePresetInteractor interactor;
    interactor.set_presets({user_preset("MyPreset")});

    NameValidator validator(
        interactor,
        PresetKind::FdmPrint,
        "MyPreset",
        true // used_for_renaming
    );

    auto [type, message] = validator.validate("MyPreset");

    REQUIRE(type == NameValidator::ValidationType::Valid);
}

TEST_CASE("NameValidator: conflict handling")
{
    FakePresetInteractor interactor;
    interactor.set_presets({user_preset("MyPreset")});

    SECTION("Different name -> warning")
    {
        NameValidator v(interactor, PresetKind::FdmPrint, "Other", false);
        REQUIRE(v.validate("MyPreset").type == NameValidator::ValidationType::Warning);
    }

    SECTION("Same name during rename -> valid")
    {
        NameValidator v(interactor, PresetKind::FdmPrint, "MyPreset", true);
        REQUIRE(v.validate("MyPreset").type == NameValidator::ValidationType::Valid);
    }
}

} // namespace Slic3r::Biz::Preset
