#include "Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace Slic3r::App::Desktop {

namespace {

// 3Dconnexion was a Logitech subsidiary, its older devices report the Logitech vendor id.
constexpr unsigned short LogitechVendorId  = 0x046d;
constexpr unsigned short ConnexionVendorId = 0x256f;

struct Mouse3DProduct
{
    unsigned short product_id;
    std::string_view name;
};

// Keep in sync with src/platform/unix/90-3dconnexion.rules.
// See https://github.com/FreeSpacenav/spacenavd/blob/6cb68b693fc837ba33b7a2fe682f184faf501496/src/dev.c#L63
constexpr std::array<Mouse3DProduct, 22> Mouse3DProducts{{
    {0xc603, "spacemouse plus XT"},
    {0xc605, "cadman"},
    {0xc606, "spacemouse classic"},
    {0xc621, "spaceball 5000"},
    {0xc623, "space traveller"},
    {0xc625, "space pilot"},
    {0xc626, "space navigator"},
    {0xc627, "space explorer"},
    {0xc628, "space navigator for notebooks"},
    {0xc629, "space pilot pro"},
    {0xc62b, "space mouse pro"},
    {0xc62e, "spacemouse wireless (USB cable)"},
    {0xc62f, "spacemouse wireless receiver"},
    {0xc631, "spacemouse pro wireless"},
    {0xc632, "spacemouse pro wireless receiver"},
    {0xc633, "spacemouse enterprise"},
    {0xc635, "spacemouse compact"},
    {0xc636, "spacemouse module"},
    {0xc638, "spacemouse pro wireless BT"},
    {0xc63a, "spacemouse wireless BT"},
    {0xc640, "nulooq"},
    {0xc652, "3Dconnexion universal receiver"},
}};

} // namespace

bool is_mouse_3d_device(unsigned short vendor_id, unsigned short product_id)
{
    if (vendor_id != LogitechVendorId && vendor_id != ConnexionVendorId) {
        return false;
    }
    return std::ranges::find(Mouse3DProducts, product_id, &Mouse3DProduct::product_id)
        != Mouse3DProducts.end();
}

std::string mouse_3d_device_name(unsigned short vendor_id, unsigned short product_id)
{
    std::string device_name;
    switch (vendor_id) {
    case LogitechVendorId:
        device_name = "LOGITECH";
        break;
    case ConnexionVendorId:
        device_name = "3DCONNECTION";
        break;
    default:
        device_name = "UNKNOWN";
        break;
    }

    const auto product_it =
        std::ranges::find(Mouse3DProducts, product_id, &Mouse3DProduct::product_id);
    device_name += "::";
    device_name += product_it != Mouse3DProducts.end() ? product_it->name : "UNKNOWN";

    return device_name;
}

} // namespace Slic3r::App::Desktop
