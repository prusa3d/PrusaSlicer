#ifndef CATCH_MAIN
#define CATCH_MAIN

#define CATCH_CONFIG_EXTERNAL_INTERFACES
#define CATCH_CONFIG_MAIN
// #define CATCH_CONFIG_DEFAULT_REPORTER "verboseconsole"
#include <catch2/reporters/catch_reporter_streaming_base.hpp>

#include <catch2/catch_all.hpp>

namespace Catch {
struct VerboseConsoleReporter : public StreamingReporterBase {
    double duration = 0.;
    using StreamingReporterBase::StreamingReporterBase;

    static std::string getDescription() {
        return "Verbose Console Reporter";
    }

    
    void testCaseStarting(TestCaseInfo const& _testInfo) override
    {
        //Colour::use(Colour::Cyan);
        m_stream << "Testing ";
        //Colour::use(Colour::None);
        m_stream << _testInfo.name << std::endl;
        StreamingReporterBase::testCaseStarting(_testInfo);
    }
    
    void sectionStarting(const SectionInfo &_sectionInfo) override
    {
        if (_sectionInfo.name != currentTestCaseInfo->name)
            m_stream << _sectionInfo.name << std::endl;
        
        StreamingReporterBase::sectionStarting(_sectionInfo);
    }
    
    void sectionEnded(const SectionStats &_sectionStats) override {
        duration += _sectionStats.durationInSeconds;
        StreamingReporterBase::sectionEnded(_sectionStats);
    } 
    
    void testCaseEnded(TestCaseStats const& stats) override
    {
        if (stats.totals.assertions.allOk()) {
            //Colour::use(Colour::BrightGreen);
            m_stream << "Passed";
            //Colour::use(Colour::None);
            m_stream << " in " << duration << " [seconds]\n" << std::endl;
        }
        
        duration = 0.;            
        StreamingReporterBase::testCaseEnded(stats);
    }
};

CATCH_REGISTER_REPORTER( "verboseconsole", VerboseConsoleReporter )

} // namespace Catch

#endif // CATCH_MAIN
