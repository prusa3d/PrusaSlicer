
#include "Slic3r/Biz/libpgcode/Processor.hpp"
#include "ProcessorImpl.hpp"

namespace Slic3r::Biz::libpgcode {

using GCodeReader::GCodeReader;

Processor::Processor(ProcessorConfig&& config)
: m_impl(new ProcessorImpl(std::move(config)))
{
}

Processor::~Processor() = default;

void Processor::process_buffer(std::string&& buffer, std::function<void(float)> progress_callback)
{
    m_impl->process_buffer(std::move(buffer), progress_callback);
}

ProcessorResult Processor::finalize()
{
    return m_impl->finalize();
}

PostProcessorConfig Processor::post_processor_config()
{
    return m_impl->post_processor_config();
}

GCodeProducer detect_producer(const std::string_view comment)
{
    for (const auto& [id, search_string] : PRODUCERS) {
        if (comment.find(search_string) != comment.npos)
            return id;
    }
    return GCodeProducer::Unknown;
}

GCodeProducer detect_producer(const std::string& gcode)
{
    GCodeProducer ret = GCodeProducer::Unknown;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &parser](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
        GCodeProducer producer = detect_producer(std::string_view(line.raw()));
        if (producer != GCodeProducer::Unknown) {
            ret = producer;
            parser.quit_parsing();
        }
    });
    return ret;
}

} // namespace Slic3r::Biz::libpgcode
