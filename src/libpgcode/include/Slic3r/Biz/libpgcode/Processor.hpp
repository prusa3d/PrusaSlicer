#pragma once

#include "Slic3r/Biz/libpgcode/ProcessorConfig.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include "Slic3r/Biz/libpgcode/PostProcessorConfig.hpp"

#include <memory>

namespace Slic3r::Biz::libpgcode {

class ProcessorImpl;

class Processor
{
public:
    explicit Processor(ProcessorConfig&& config);
    ~Processor();
    Processor(Processor&& other) = delete;
    Processor& operator = (Processor&& other) = delete;

    void process_buffer(std::string&& buffer, std::function<void(float)> progress_callback = nullptr);
    [[nodiscard]] ProcessorResult finalize();

    [[nodiscard]] PostProcessorConfig post_processor_config();

private:
    std::unique_ptr<ProcessorImpl> m_impl;
};

GCodeProducer detect_producer(const std::string_view comment);
GCodeProducer detect_producer(const std::string& gcode);

} // namespace Slic3r::Biz::libpgcode
