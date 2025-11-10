///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_SecretStore_hpp_
#define slic3r_GUI_SecretStore_hpp_

#include <string>

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

namespace SecretStore {

/**
 * Check if the system secret store is available and working.
 *
 * @param errmsg Optional pointer to receive error message if not supported
 * @return true if the secret store is available, false otherwise
 */
bool is_supported(wxString* errmsg = nullptr);

/**
 * Load credentials from the system secret store.
 *
 * @param service_prefix The service prefix (e.g., "PhysicalPrinter"
 *                       or "PrusaAccount")
 * @param id The identifier (e.g., printer name) - can be empty for
 *           PrusaAccount
 * @param opt The option name (e.g., "printhost_password",
 *            "printhost_apikey")
 * @param usr Output parameter for username (or dummy value for apikey)
 * @param psswd Output parameter for password/apikey value
 * @return true if credentials were loaded successfully, false otherwise
 */
bool load_secret(const std::string& service_prefix,
                 const std::string& id,
                 const std::string& opt,
                 std::string& usr,
                 std::string& psswd);

/**
 * Save credentials to the system secret store.
 *
 * @param service_prefix The service prefix (e.g., "PhysicalPrinter"
 *                       or "PrusaAccount")
 * @param id The identifier (e.g., printer name) - can be empty for
 *           PrusaAccount
 * @param opt The option name (e.g., "printhost_password",
 *            "printhost_apikey")
 * @param usr The username (or dummy value for apikey)
 * @param psswd The password/apikey value
 * @return true if credentials were saved successfully, false otherwise
 */
bool save_secret(const std::string& service_prefix,
                 const std::string& id,
                 const std::string& opt,
                 const std::string& usr,
                 const std::string& psswd);

/**
 * Load printer credentials from secret store if they are marked as
 * "stored". Updates the config in-place with the loaded credentials.
 *
 * @param printer_name The physical printer name
 * @param config The printer configuration to update
 */
void load_printer_credentials(const std::string& printer_name,
                              DynamicPrintConfig* config);

/**
 * Save printer credentials to secret store and mark them as "stored"
 * in the config. Updates the config in-place to replace actual
 * credentials with "stored" marker.
 *
 * @param printer_name The physical printer name
 * @param config The printer configuration to save and update
 */
void save_printer_credentials(const std::string& printer_name,
                              DynamicPrintConfig* config);

} // namespace SecretStore
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_SecretStore_hpp_
