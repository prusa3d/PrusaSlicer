#pragma once

#include <memory>
#include <string>
#include <miniz.h>

namespace Slic3r::Biz::Algorithms {

// help function for RAII c++ approach for miniz Iterator over data in zipped File
struct MzIterDeleter{ void operator()(mz_zip_reader_extract_iter_state *it) const 
{ if(it!=nullptr) mz_zip_reader_extract_iter_free(it); }};
using MzIterType = std::unique_ptr<mz_zip_reader_extract_iter_state, MzIterDeleter>;

bool open_zip_reader(mz_zip_archive *zip, const std::string &fname_utf8);
bool open_zip_writer(mz_zip_archive *zip, const std::string &fname_utf8);
bool close_zip_reader(mz_zip_archive *zip);
bool close_zip_writer(mz_zip_archive *zip);

class MZ_Archive {
public:
    mz_zip_archive arch;

    MZ_Archive();
};

} // namespace Slic3r
