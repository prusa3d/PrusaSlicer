
#include "Slic3r/Biz/Preset/NameValidator.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <map>
#include <fmt/format.h>
#include <boost/algorithm/string/case_conv.hpp>

namespace Slic3r::Biz::Preset {

NameValidator::NameValidator(
    const IPresetNameProvider& preset_interactor,
    Domain::Preset::PresetKind kind,
    const std::string& name,
    bool used_for_renaming
) :
    m_init_name(name),
    m_kind(kind),
    m_preset_interactor(&preset_interactor),
    m_used_for_renaming(used_for_renaming),
    m_preset_names(preset_interactor.get_all_vendor_preset_names(kind))
{
    // Filter out runtime presets
    std::erase_if(
        m_preset_names,
        [](const auto& pn) { return pn.origin == Domain::Preset::PresetOrigin::Runtime; }
    );
    // Sort preset names to enable binary search
    std::sort(
        m_preset_names.begin(),
        m_preset_names.end(),
        [](const Domain::Preset::PresetName& pn1, const Domain::Preset::PresetName& pn2)
        { return pn1.name < pn2.name; }
    );

    // Initialize case-insensitive preset names

    m_casei_preset_names.clear();

    for (const Domain::Preset::PresetName& preset_name : m_preset_names) {
        if (preset_name.origin ==Domain::Preset::PresetOrigin::Runtime) {
            continue;
        }
        m_casei_preset_names.emplace_back(PresetNameCaseIMapper(
            {boost::to_lower_copy<std::string>(preset_name.name), preset_name.name}
        ));
    }

    std::sort(m_casei_preset_names.begin(), m_casei_preset_names.end());
}

void NameValidator::set_reserved_preset_names(const std::vector<std::string>& reserved_preset_names)
{
    m_casei_reserved_preset_names.clear();
    for (const std::string& preset_name : reserved_preset_names) {
        m_casei_reserved_preset_names.emplace_back(boost::to_lower_copy<std::string>(preset_name));
    }
}

const Domain::Preset::PresetNames& NameValidator::preset_names() const
{
    return m_preset_names;
}

std::string NameValidator::get_conflict_name(const std::string& preset_name) const
{
    if (!m_casei_preset_names.empty()) {
        const std::string lower_name = boost::to_lower_copy<std::string>(preset_name);

        auto it = std::lower_bound(
            m_casei_preset_names.begin(),
            m_casei_preset_names.end(),
            NameValidator::PresetNameCaseIMapper(lower_name)
        );
        if (it != m_casei_preset_names.end() && it->casei_name == lower_name)
            return it->name;
    }
    return std::string();
}

NameValidator::ValidationResult NameValidator::validate(const std::string& preset_name) const
{
    ValidationType valid_type = ValidationType::Valid;

    std::string info_line;

    if (preset_name.find_first_of(' ') == 0) {
        info_line  = _u8L("The name cannot start with space character.");
        valid_type = ValidationType::Invalid;
    }

    if (valid_type == ValidationType::Valid
        && preset_name.find_last_of(' ') == preset_name.length() - 1)
    {
        info_line  = _u8L("The name cannot end with space character.");
        valid_type = ValidationType::Invalid;
    }

    constexpr std::string_view illegal_chars = "<>[]:/\\|?*\"@";
    if (valid_type == ValidationType::Valid
        && preset_name.find_first_of(illegal_chars) != std::string::npos)
    {
        info_line = _u8L("The following characters are not allowed in the name")
            + ": "
            + illegal_chars.data();
        valid_type = ValidationType::Invalid;
    }

    const std::string illegal_suffix = Biz::_u8L("(modified)");
    if (valid_type == ValidationType::Valid
        && preset_name.find(illegal_suffix) != std::string::npos)
    {
        info_line =
            _u8L("The following suffix is not allowed in the name") + ":\n\t" + illegal_suffix;
        valid_type = ValidationType::Invalid;
    }

    if (valid_type == ValidationType::Valid && preset_name == "- default -") {
        info_line  = _u8L("This name is reserved, use another.");
        valid_type = ValidationType::Invalid;
    }

    if (std::find(
            m_casei_reserved_preset_names.cbegin(),
            m_casei_reserved_preset_names.cend(),
            boost::to_lower_copy<std::string>(preset_name)
        )
        != m_casei_reserved_preset_names.cend())
    {
        info_line = _u8L(
            "One of the presets above of the same type already uses this name."
        );
        valid_type = ValidationType::Invalid;
    }

    const Domain::Preset::PresetName* existing = get_existing_preset(preset_name);

    if (valid_type == ValidationType::Valid
        && existing
        && (existing->origin == Domain::Preset::PresetOrigin::System))
    {
        info_line = m_used_for_renaming ?
            _u8L("This name is used for a system profile name, use another.") :
            _u8L("Cannot overwrite a system profile.");
        info_line += fmt::format("\n({})", existing->name);
        valid_type = ValidationType::Invalid;
    }

    // if (valid_type == ValidationType::Valid && existing && (existing->is_external)) {
    // info_line    = m_used_for_renaming ?
    // _u8L("This name is used for an external profile name, use another.") :
    // _u8L("Cannot overwrite an external profile.");
    // valid_type = ValidationType::Invalid;
    //}

    if (valid_type == ValidationType::Valid && existing) {
        if (preset_name == m_init_name) {
            info_line  = _u8L("Save preset modifications to existing user profile");
            valid_type = ValidationType::Valid;
        } else {
            // if (existing->is_compatible)
            info_line = fmt::format(
                fmt::runtime(Biz::_u8L("Preset with name \"{}\" already exists.")),
                existing->name
            );
            // else
            // info_line = from_u8(
            // fmt::format(
            // fmt::runtime(
            // Biz::_u8L(
            // "Preset with name \"{}\" already exists and is incompatible with selected printer."
            // )
            // ),
            // existing->name
            // )
            // );
            info_line += "\n"
                + (m_used_for_renaming ?
                       _u8L("Note: This preset will be replaced after renaming") :
                       _u8L("Note: Preset modifications will be saved exactly into this preset"));
            valid_type = ValidationType::Warning;
        }
    }

    if (valid_type == ValidationType::Valid && preset_name.empty()) {
        info_line  = _u8L("The name cannot be empty.");
        valid_type = ValidationType::Invalid;
    }

#ifdef __WXMSW__
    const int max_path_length = MAX_PATH;
#else
    const int max_path_length = 255;
#endif

    if (valid_type == ValidationType::Valid
        && m_preset_interactor->selected_user_preset_path(m_kind, preset_name).string().length()
            >= max_path_length)
    {
        info_line  = _u8L("The name is too long.");
        valid_type = ValidationType::Invalid;
    }

    // if (valid_type == ValidationType::Valid
    // && m_presets
    // && m_presets->get_preset_name_by_alias(preset_name) != preset_name)
    //{
    // info_line    = _u8L("The name cannot be the same as a preset alias name.");
    // valid_type = ValidationType::Invalid;
    //}

    return {valid_type, info_line};
}

const Domain::Preset::PresetName* NameValidator::get_existing_preset(const std::string& name) const
{
    std::string existed_preset_name = get_conflict_name(name);
    if (existed_preset_name.empty()) {
        // Preset was not found in the sorted list of presets.
        return nullptr;
    }

    auto it = std::lower_bound(
        m_preset_names.begin(),
        m_preset_names.end(),
        Domain::Preset::PresetName({.name = existed_preset_name}),
        [](const Domain::Preset::PresetName& pn, const Domain::Preset::PresetName& aim)
        { return pn.name < aim.name; }
    );

    if (it != m_preset_names.end() && it->name == existed_preset_name) {
        return &m_preset_names[it - m_preset_names.begin()];
    }
    return nullptr;
}

} // namespace Slic3r::Biz::Preset
