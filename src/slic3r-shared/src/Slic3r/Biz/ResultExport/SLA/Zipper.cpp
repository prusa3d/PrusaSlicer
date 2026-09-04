#include "Slic3r/Biz/ResultExport/SLA/Zipper.hpp"

#include <Slic3r/Log.hpp>
#include <cstring>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Biz/MiniZErrorTranslation.hpp"
#include "miniz.h"

#if defined(_MSC_VER) &&  _MSC_VER <= 1800 || __cplusplus < 201103L
    #define SLIC3R_NORETURN
#elif __cplusplus >= 201103L
    #define SLIC3R_NORETURN [[noreturn]]
#endif

namespace Slic3r::Biz::PrintHost::Sla {

using Algorithms::MZ_Archive;
using Algorithms::open_zip_writer;
using Algorithms::close_zip_writer;

class Zipper::Impl: public MZ_Archive {
public:
    std::string m_zipname;

    std::string formatted_errorstr() const
    {
        return fmt::format(fmt::runtime(_u8L("Error with ZIP archive {}: {}!")),
                           m_zipname,
                           translate_miniz_error(arch.m_last_error));
    }

    SLIC3R_NORETURN void blow_up() const
    {
        throw Slic3r::ExportError(formatted_errorstr());
    }

    bool is_alive()
    {
        return arch.m_zip_mode != MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED;
    }
};

Zipper::Zipper(const std::string &zipfname, e_compression compression)
{
    m_impl.reset(new Impl());

    m_compression = compression;
    m_impl->m_zipname = zipfname;

    memset(&m_impl->arch, 0, sizeof(m_impl->arch));

    if (!open_zip_writer(&m_impl->arch, zipfname)) {
        m_impl->blow_up();
    }
}

Zipper::~Zipper()
{
    if(m_impl->is_alive()) {
        // Flush the current entry if not finished yet.
        try { finish_entry(); } catch(...) {
            SPDLOG_ERROR("{}", m_impl->formatted_errorstr());
        }

        if(!mz_zip_writer_finalize_archive(&m_impl->arch))
            SPDLOG_ERROR("{}", m_impl->formatted_errorstr());
    }

    // The file should be closed no matter what...
    if(!close_zip_writer(&m_impl->arch))
        SPDLOG_ERROR("{}", m_impl->formatted_errorstr());
}

Zipper::Zipper(Zipper &&m):
    m_impl(std::move(m.m_impl)),
    m_data(std::move(m.m_data)),
    m_entry(std::move(m.m_entry)),
    m_compression(m.m_compression) {}

Zipper &Zipper::operator=(Zipper &&m) {
    m_impl = std::move(m.m_impl);
    m_data = std::move(m.m_data);
    m_entry = std::move(m.m_entry);
    m_compression = m.m_compression;
    return *this;
}

void Zipper::add_entry(const std::string &name)
{
    if(!m_impl->is_alive()) return;

    finish_entry(); // finish previous business
    m_entry = name;
}

void Zipper::add_entry(const std::string &name, const void *data, size_t l)
{
    if(!m_impl->is_alive()) return;

    finish_entry();
    mz_uint cmpr = MZ_NO_COMPRESSION;
    switch (m_compression) {
    case NO_COMPRESSION: cmpr = MZ_NO_COMPRESSION; break;
    case FAST_COMPRESSION: cmpr = MZ_BEST_SPEED; break;
    case TIGHT_COMPRESSION: cmpr = MZ_BEST_COMPRESSION; break;
    }

    if(!mz_zip_writer_add_mem(&m_impl->arch, name.c_str(), data, l, cmpr))
        m_impl->blow_up();

    m_entry.clear();
    m_data.clear();
}

void Zipper::finish_entry()
{
    if(!m_impl->is_alive()) return;

    if(!m_data.empty() && !m_entry.empty()) {
        mz_uint compression = MZ_NO_COMPRESSION;

        switch (m_compression) {
        case NO_COMPRESSION: compression = MZ_NO_COMPRESSION; break;
        case FAST_COMPRESSION: compression = MZ_BEST_SPEED; break;
        case TIGHT_COMPRESSION: compression = MZ_BEST_COMPRESSION; break;
        }

        if(!mz_zip_writer_add_mem(&m_impl->arch, m_entry.c_str(),
                                  m_data.c_str(),
                                  m_data.size(),
                                  compression)) m_impl->blow_up();
    }

    m_data.clear();
    m_entry.clear();
}

void Zipper::finalize()
{
    finish_entry();

    if(m_impl->is_alive()) if(!mz_zip_writer_finalize_archive(&m_impl->arch))
        m_impl->blow_up();
}

const std::string &Zipper::get_filename() const
{
    return m_impl->m_zipname;
}

}
