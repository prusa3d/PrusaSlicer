#include "Slic3r/Biz/MatDb/MatDbInteractor.hpp"
#include "Slic3r/Directories.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::MatDb {

MatDbInteractor::MatDbInteractor(Preset::PresetInteractor& preset_interactor) :
    m_preset_interactor(preset_interactor)
{}

void MatDbInteractor::init_client()
{
    namespace fs = boost::filesystem;
    for (const auto& parent_dir : {data_dir(), resources_dir()}) {
        auto p = fs::path{parent_dir} / "matdb" / "matdb.zip";
        if (m_client.open(p.string())) {
            break;
        }
    }
}

void MatDbInteractor::query_by_name(size_t tool_index, const std::string& q)
{
    auto result = m_client.query_by_name(q, 64, 0);
    std::vector<MatDbItem> items;
    std::ranges::copy(
        result | std::views::transform(
            [](const Domain::MatDb::Material& mat) { return MatDbItem{mat}; }
        ),
        std::back_inserter(items)
    );
    m_result.reset(std::move(items));
}

void MatDbInteractor::query_by_type(size_t tool_index, const std::string& type)
{
    // TODO: implement
}

void MatDbInteractor::query_first(size_t tool_index)
{
    // TODO: implement
}

void MatDbInteractor::select_material_preset(
    size_t tool_index,
    const Domain::MatDb::Material& mat,
    const std::string& preset_id
)
{
    // TODO: implement
}

} // namespace Slic3r::Biz::MatDb
