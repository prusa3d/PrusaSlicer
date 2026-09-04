#pragma once
#include <vector>
#include <memory>
#include <Slic3r/Assert.hpp>

namespace Slic3r::Domain {

/// <summary>
/// keep information from file about font
/// (store file data itself)
/// + cache data readed from buffer
/// </summary>
struct FontFile
{
    // loaded data from font file
    // must store data size for imgui rasterization
    // To not store data on heap and To prevent unneccesary copy
    // data are stored inside unique_ptr
    std::unique_ptr<std::vector<unsigned char>> data;

    struct Info
    {
        // vertical position is "scale*(ascent - descent + lineGap)"
        int ascent, descent, linegap;

        // for convert font units to pixel
        int unit_per_em;
    };

    // info for each font in data
    std::vector<Info> infos;

    FontFile(std::unique_ptr<std::vector<unsigned char>> data, std::vector<Info>&& infos) :
        data(std::move(data)),
        infos(std::move(infos))
    {
        ASSERT(this->data != nullptr);
        ASSERT(!this->data->empty());
    }

    bool operator==(const FontFile& other) const
    {
        if (data->size() != other.data->size())
            return false;
        // if(*data != *other.data) return false;
        for (size_t i = 0; i < infos.size(); i++)
            if (infos[i].ascent != other.infos[i].ascent
                || infos[i].descent == other.infos[i].descent
                || infos[i].linegap == other.infos[i].linegap)
                return false;
        return true;
    }
};

} // namespace Slic3r::Domain
