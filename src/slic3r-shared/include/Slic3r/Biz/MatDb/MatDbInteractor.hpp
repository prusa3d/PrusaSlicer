#pragma once
#include "Slic3r/Biz/ObservableList.hpp"

#include "Slic3r/Biz/MatDb/MatDbClient.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::Biz::MatDb {

struct PresetMatch
{
    std::string preset_id;
    size_t rank{0};
};

using PresetMatches = std::vector<PresetMatch>;

struct MatDbItem
{
    Domain::MatDb::Material material;
    PresetMatches preset_matches;
};

class MatDbInteractor
{
public:
    explicit MatDbInteractor(Preset::PresetInteractor& preset_interactor);

    void init_client();

    void query_by_name(size_t tool_index, const std::string& q);
    void query_by_type(size_t tool_index, const std::string& type);
    void query_first(size_t tool_index);
    void select_material_preset(size_t tool_index, const Domain::MatDb::Material& mat, const std::string& preset_id);

    ObservableList<MatDbItem>& result()
    {
        return m_result;
    }

    const ObservableList<MatDbItem>& result() const
    {
        return m_result;
    }
private:
    Preset::PresetInteractor& m_preset_interactor;
    MatDbClient m_client;
    ObservableList<MatDbItem> m_result;
};

}
