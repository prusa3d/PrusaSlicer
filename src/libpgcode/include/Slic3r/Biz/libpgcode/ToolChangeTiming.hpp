#pragma once

#include <optional>
#include <vector>

namespace Slic3r::Biz::libpgcode {

// Physical nozzle occupancy for separate load, unload and tool-change times.
// SEMM maps all filament IDs to one nozzle; MEMM retains loaded inactive tools.
class ToolChangeTiming {
public:
    bool needs_selection(unsigned filament, bool semm) const
    {
        const unsigned nozzle = semm ? 0 : filament;
        return !m_selected || *m_selected != filament || nozzle >= m_loaded.size()
            || m_loaded[nozzle] != static_cast<int>(filament);
    }

    double select(unsigned filament, bool semm, double load, double unload, double tool_change)
    {
        if (!needs_selection(filament, semm)) return 0.;
        const unsigned nozzle = semm ? 0 : filament;
        if (m_loaded.size() <= nozzle) m_loaded.resize(nozzle + 1, -1);
        double seconds = m_selected && !semm && *m_selected != filament ? tool_change : 0.;
        if (m_loaded[nozzle] != static_cast<int>(filament)) {
            if (m_loaded[nozzle] >= 0) seconds += unload;
            seconds += load;
            m_loaded[nozzle] = static_cast<int>(filament);
        }
        m_selected = filament;
        return seconds;
    }

    double unload(bool semm, double seconds)
    {
        if (!m_selected) return 0.;
        const unsigned nozzle = semm ? 0 : *m_selected;
        if (nozzle >= m_loaded.size() || m_loaded[nozzle] < 0) return 0.;
        m_loaded[nozzle] = -1;
        return seconds;
    }

private:
    std::optional<unsigned> m_selected;
    std::vector<int> m_loaded;
};

}
