#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostDuet : public IPrintHost {

public:
    PrintHostDuet(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostDuet(const PrintHostDuet&) = delete;
    PrintHostDuet& operator=(const PrintHostDuet&) = delete;
    PrintHostDuet(PrintHostDuet&& other) noexcept = default;
    PrintHostDuet& operator=(PrintHostDuet&& other) noexcept = default;
   
    ~PrintHostDuet() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "Duet"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

private:
    enum class ConnectionType { rrf, dsf, error }; // rrf = RepRapFirmware, dsf = DuetSoftwareFramework
    std::string host;
	std::string password;

	std::string get_upload_url(const std::string &filename, ConnectionType connectionType) const;
	std::string get_connect_url(const bool dsfUrl) const;
	std::string get_base_url() const;
	std::string timestamp_str() const;
	ConnectionType connect(std::string &msg, RetryFn retry_fn) const;
	void disconnect(ConnectionType connectionType, RetryFn retry_fn) const;
	bool start_print(std::string &msg, const std::string &filename, ConnectionType connectionType, bool simulationMode, RetryFn retry_fn) const;
	int get_err_code_from_body(const std::string &body) const;

};
} // namespace Slic3r::Biz::PrintHost