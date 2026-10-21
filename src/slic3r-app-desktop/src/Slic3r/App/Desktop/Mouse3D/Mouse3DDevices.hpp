#pragma once

#include <string>

namespace Slic3r::App::Desktop {

/**
 * @brief Returns true for the vendor and product id of a supported 3D mouse.
 */
bool is_mouse_3d_device(unsigned short vendor_id, unsigned short product_id);

/**
 * @brief Returns the device name used on Windows, e.g. "LOGITECH::space navigator".
 */
std::string mouse_3d_device_name(unsigned short vendor_id, unsigned short product_id);

} // namespace Slic3r::App::Desktop
