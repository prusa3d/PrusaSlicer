///|/ Copyright (c) Prusa Research 2018 - 2022 David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2020 Manuel Coenen
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Duet_hpp_
#define slic3r_Duet_hpp_

#include <cstdint>
#include <optional>
#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class Http;

class Duet : public PrintHost
{
public:
    explicit Duet(DynamicPrintConfig *config);
	~Duet() override = default;

	const char* get_name() const override;

	bool test(wxString &curl_msg) const override;
	wxString get_test_ok_msg() const override;
	wxString get_test_failed_msg(wxString &msg) const override;
	bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
	bool has_auto_discovery() const override { return false; }
	bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint | PrintHostPostUploadAction::StartSimulation; }
	std::string get_host() const override { return host; }
   
private:
	enum class ConnectionType { rrf, dsf, error }; // rrf = RepRapFirmware, dsf = DuetSoftwareFramework
	std::string host;
	std::string password;

	// crc32 is appended to the RepRapFirmware rr_upload URL when set, so that the firmware can
	// verify the data it received. It is ignored for the DuetSoftwareFramework endpoint, which
	// has no equivalent parameter.
	std::string get_upload_url(const std::string &filename, ConnectionType connectionType, const std::optional<uint32_t> &crc32) const;
	std::string get_connect_url(const bool dsfUrl) const;
	std::string get_base_url() const;
	std::string timestamp_str() const;
	ConnectionType connect(wxString &msg) const;
	void disconnect(ConnectionType connectionType) const;
	bool start_print(wxString &msg, const std::string &filename, ConnectionType connectionType, bool simulationMode) const;
	int get_err_code_from_body(const std::string &body) const;
};

}

#endif
