#include "Slic3r/Biz/ResultExport/ResultExportDataFinalizer.hpp"
#include "Slic3r/Biz/libpgcode/LineView.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Directories.hpp"

#include <LibBGCode/convert/convert.hpp>
#include <jthread/JThread.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/system/error_code.hpp>

namespace Slic3r::Biz::ResultExport {

ResultExportDataFinalizer::ResultExportDataFinalizer(Platform::IMainThreadDispatcher& dispatcher)
    : m_dispatcher{dispatcher}
{
}

ResultExportDataFinalizer::~ResultExportDataFinalizer()
{
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );
}

namespace {

// RAII handle for FILE*
struct FileGuard {
    FILE* file;
    ~FileGuard() { if (file) std::fclose(file); }
};

void from_ascii_to_binary_wrapper(const libpgcode::LineView& input, const boost::filesystem::path& target_path) 
{
    FileGuard src_guard {std::tmpfile()};
    if (!src_guard.file) {
        throw std::runtime_error("Failed to create temporary input file");
    }
    FileGuard dst_guard {fopen(target_path.string().c_str(), "wb")};
    if (!dst_guard.file) {
        throw std::runtime_error("Failed to open file for writing: " + target_path.string());
    }

    size_t data_size = input.str().size();
    size_t written_size = std::fwrite(input.str().data(), 1, data_size, src_guard.file);
    if (written_size != data_size) {
        throw std::runtime_error("Failed to write to temporary input file");
    }
    std::fflush(src_guard.file);
    std::rewind(src_guard.file); // Reset file pointer for reading by from_ascii_to_binary()

    bgcode::binarize::BinarizerConfig binarizer_config{
        {
            bgcode::core::ECompressionType::None,            // file metadata
            bgcode::core::ECompressionType::None,            // printer metadata
            bgcode::core::ECompressionType::Deflate,         // print metadata
            bgcode::core::ECompressionType::Deflate,         // slicer metadata
            bgcode::core::ECompressionType::Heatshrink_12_4, // gcode
        },
        bgcode::core::EGCodeEncodingType::MeatPackComments,
        bgcode::core::EMetadataEncodingType::INI,
        bgcode::core::EChecksumType::CRC32
    };

    const bgcode::core::EResult res = bgcode::convert::from_ascii_to_binary(*src_guard.file, *dst_guard.file, binarizer_config);
    if (res != bgcode::core::EResult::Success) {
        throw std::runtime_error(std::string(bgcode::core::translate_result(res)));
    } 
}

void write_lineview_to_path(const libpgcode::LineView& input, const boost::filesystem::path& target_path)
{
    boost::system::error_code ec;
    ASSERT(boost::filesystem::exists(target_path.parent_path(), ec) && !ec);
    
    FileGuard file_guard {fopen(target_path.string().c_str(), "wb")};
    if (!file_guard.file) {
        throw std::runtime_error("Failed to open file for writing: " + target_path.string());
    }

    size_t data_size = input.str().size();
    size_t written_size = std::fwrite(input.str().data(), 1, data_size, file_guard.file);
    if (written_size != data_size) {
        throw std::runtime_error("Failed to write to temporary input file");
    }
}

void process_line_view(const libpgcode::LineView& input, const boost::filesystem::path& result_path, PrintHost::PrintHostExportFormat result_format)
{
    switch (result_format)
    {
    case Slic3r::Biz::PrintHost::PrintHostExportFormat::GCode:
        write_lineview_to_path(input, result_path);
        break;
    case Slic3r::Biz::PrintHost::PrintHostExportFormat::BGCode:
        from_ascii_to_binary_wrapper(input, result_path);
        break;
    case Slic3r::Biz::PrintHost::PrintHostExportFormat::Undefined:
    default:
        throw std::runtime_error(std::string("PrintHostFinalizer: Unexpected result format {}. Expected G-Code.", (int)result_format));
    }
}

void process_sla_result(const Slicing::SLAResultData& data, const boost::filesystem::path& result_path, PrintHost::PrintHostExportFormat result_format)
{
    PrintHost::Sla::store_sl1(result_path.string(), data);
}

boost::filesystem::path get_temporary_file_path(const std::string& extension)
{
    boost::filesystem::path temp_path = Slic3r::temp_dir();
    boost::uuids::random_generator generator;
    boost::uuids::uuid uuid = generator();
    return temp_path / (to_string(uuid) + extension);
}

} // namespace


void ResultExportDataFinalizer::finalize(PhysicalPrinter::PhysicalPrinterConfig&& config, PrintHost::PrintHostJobData&& data)
{
    JThread::JThread thread = JThread::JThread([this, config = std::move(config),  data = std::move(data)](JThread::StopToken stop_token) mutable {
        data.source_path = get_temporary_file_path(data.dest_path.extension().string());
        try
        {
            std::visit([&data](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::shared_ptr<const libpgcode::LineView>>) {
                    process_line_view(*arg, data.source_path, data.result_format);
                } else if constexpr (std::is_same_v<T, std::shared_ptr<const Slicing::SLAResultData>>) {
                    process_sla_result(*arg, data.source_path, data.result_format);
                } else {
                    ASSERT(false, "non-exhaustive visitor!");
                }
            }, data.data_ptr);
        }
        catch (const std::exception& e)
        {
            dispatch_fail(e.what());
            return;
        }

        dispatch_success(std::move(config), std::move(data));
    });
}

void ResultExportDataFinalizer::dispatch_success(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data)
{
    {
        std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
        bool dispatched = m_dispatcher.dispatch_on_main_thread([this, config = std::move(config),  data = std::move(data)]() mutable {
            this->invoke_listeners<IResultExportBinarizeListener>([&config, &data](auto* listener) mutable {
                listener->on_result_export_binarize_success(std::move(config), std::move(data));
            }); 
        });
    }
}

void ResultExportDataFinalizer::dispatch_fail(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
        bool dispatched = m_dispatcher.dispatch_on_main_thread([this, message]() {
            this->invoke_listeners<IResultExportBinarizeListener>([message](auto* listener) {
                listener->on_result_export_binarize_fail(message);
            });
        });
    }
}

} //namespace Slic3r::Biz::PrintHost