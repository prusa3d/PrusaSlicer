#pragma once


#include <memory>
#include <random>
#include <string>
#include "Slic3r/Biz/Parser/IO.hpp"

namespace Slic3r::Biz::Parser {

class PlaceholderParser
{
public:
    // Context to be shared during multiple executions of the PlaceholderParser.
    // The context is kept external to the PlaceholderParser, so that the same PlaceholderParser
    // may be called safely from multiple threads.
    // In the future, the context may hold variables created and modified by the PlaceholderParser
    // and shared between the PlaceholderParser::process() invocations.
    struct ContextData {
        std::mt19937                    rng;
        // If defined, then this dictionary is used by the scripts to define user variables and persist them
        // between PlaceholderParser evaluations.
        std::unique_ptr<IO::Config>  global_config;
    };

    PlaceholderParser() = default;
    PlaceholderParser(const IO::Config& external_config);

    void apply_config(const IO::Config &config);

    template<typename T>
    void set(const std::string& key, const T& value) {
        m_config.set(key, value);
    }

	const IO::Config& config() const { return m_config; }

    // Fill in the template using a macro processing language.
    // Throws Slic3r::PlaceholderParserError on syntax or runtime error.
    std::string process(const std::string &templ, unsigned int current_extruder_id, const IO::Config *config_override, IO::Config *config_outputs, ContextData *context) const;
    std::string process(const std::string &templ, unsigned int current_extruder_id = 0, const IO::Config *config_override = nullptr, ContextData *context = nullptr) const
        { return this->process(templ, current_extruder_id, config_override, nullptr /* config_outputs */, context); }

    // Evaluate a boolean expression using the full expressive power of the PlaceholderParser boolean expression syntax.
    // Throws Slic3r::PlaceholderParserError on syntax or runtime error.
    static bool evaluate_boolean_expression(const std::string &templ, const IO::Config &config, const IO::Config *config_override = nullptr);

    // Update timestamp, year, month, day, hour, minute, second variables at m_config.
    static void update_timestamp(IO::Config &config);
    void update_timestamp();

private:
	// config has a higher priority than external_config when looking up a symbol.
    IO::Config m_config;
    IO::Config m_external_config;
};

}
