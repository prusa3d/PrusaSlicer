#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"

namespace Slic3r::Biz::Preset {
class IPresetNameProvider;

class NameValidator
{
public:
    enum class ValidationType
    {
        Undef,
        Valid,
        Invalid,
        Warning
    };

    struct ValidationResult
    {
        ValidationType type;
        std::string message;
    };

    NameValidator() {}

    NameValidator(
        const IPresetNameProvider& preset_interactor,
        Domain::Preset::PresetKind kind,
        const std::string& name,
        bool used_for_renaming
    );

    const Domain::Preset::PresetNames& preset_names() const;

    ValidationResult validate(const std::string& name) const;
    std::string get_conflict_name(const std::string& preset_name) const;

    void set_reserved_preset_names(const std::vector<std::string>& reserved_preset_names);

private:
    const Domain::Preset::PresetName* get_existing_preset(const std::string& name) const;

private:
    struct PresetNameCaseIMapper
    {
        std::string casei_name;
        std::string name;

        bool operator<(const PresetNameCaseIMapper& other) const
        {
            return casei_name < other.casei_name;
        }
    };

    std::string m_init_name;
    Domain::Preset::PresetKind m_kind;
    const IPresetNameProvider* m_preset_interactor{nullptr};
    bool m_used_for_renaming{false};
    Domain::Preset::PresetNames m_preset_names;
    std::vector<PresetNameCaseIMapper> m_casei_preset_names;

    std::vector<std::string> m_casei_reserved_preset_names;
};

} // namespace Slic3r::Biz::Preset
