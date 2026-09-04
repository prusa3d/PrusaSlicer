#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Bed.hpp"

namespace Slic3r::Domain {

std::unique_ptr<ConfigContainer> ConfigContainer::copy(
    const std::unordered_map<const Bed*, const Bed*>& bed_translation_table,
    const std::unordered_map<ModelInstance*, ModelInstance*>& model_instance_translation_table
) const
{
    std::unique_ptr<ConfigContainer> cc = std::make_unique<ConfigContainer>();

    cc->m_preset           = m_preset;
    cc->m_project_settings = m_project_settings;
    cc->m_bed              = bed_translation_table.at(m_bed);
    ASSERT(cc->m_bed);

    cc->m_bed_instances.reserve(m_bed_instances.size());
    std::ranges::transform(
        m_bed_instances,
        std::back_inserter(cc->m_bed_instances),
        [&](const std::unique_ptr<BedInstance>& bed_instance)
        { return bed_instance->copy(bed_translation_table, model_instance_translation_table); }
    );

    return cc;
}

ConfigPack ConfigContainer::build_print_config() const
{
    ConfigPack pack = m_preset.config();
    if (auto* fdm = std::get_if<ConfigPackFDM>(&pack)) {
        fdm->project           = m_project_settings;
        fdm->virtual_extruders = m_virtual_extruders;
    }
    return pack;
}

BedInstance& ConfigContainer::add_bed_instance()
{
    ASSERT(m_bed != nullptr, "ConfigContainer's Bed is null");
    m_bed_instances.emplace_back(std::make_unique<BedInstance>(*m_bed));
    return *m_bed_instances.back();
}

ConfigContainer::BedInstanceList::const_iterator ConfigContainer::remove_bed_instance_by_id(
    size_t id
)
{
    auto it = std::find_if(
        m_bed_instances.begin(),
        m_bed_instances.end(),
        [id](const auto& i) { return i->id().id == id; }
    );
    if (it != m_bed_instances.end())
        return m_bed_instances.erase(it);
    return m_bed_instances.end();
}

} // namespace Slic3r::Domain
