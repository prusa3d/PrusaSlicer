#include "SL1.hpp"

#include <boost/filesystem.hpp>

#include <sstream>

#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Time.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep
#include <LocalesUtils.hpp>
#include "libslic3r/SLA/RasterBase.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

using namespace Slic3r::Biz::Slicing;

namespace Slic3r {

using namespace Slic3r;
using namespace Slic3r::sla;
class Sl1Rasterizer : public ISlaRasterizer
{
    Resolution res;
    PixelDim pxdim;
    double gamma;
    RasterBase::Trafo tr;

public:
    explicit Sl1Rasterizer(const SLAPrintConfigView& cfg) {
        double w = cfg.get<double>("display_width");
        double h = cfg.get<double>("display_height");
        auto pw = size_t(cfg.get<int>("display_pixels_x"));
        auto ph = size_t(cfg.get<int>("display_pixels_y"));

        std::array<bool, 2> mirror;
        mirror[X] = cfg.get<bool>("display_mirror_x");
        mirror[Y] = cfg.get<bool>("display_mirror_y");

        auto ro = cfg.get<Domain::SLADisplayOrientation>("display_orientation");
        RasterBase::Orientation orientation = ro == Domain::SLADisplayOrientation::sladoPortrait
            ? RasterBase::roPortrait
            : RasterBase::roLandscape;

        if (orientation == RasterBase::roPortrait) {
            std::swap(w, h);
            std::swap(pw, ph);
        }

        res = Resolution{pw, ph};
        pxdim = PixelDim{w / pw, h / ph};
        gamma = cfg.get<double>("gamma_correction");
        tr = RasterBase::Trafo{orientation, mirror};
    }

    Sla::FileData create_file(const ExPolygons& slice) override {
        std::unique_ptr<sla::RasterBase> raster = create_raster_grayscale_aa(res, pxdim, gamma, tr);
        for (const ExPolygon& part : slice)
            raster->draw(part);

        sla::RasterEncoder encoder = sla::PNGRasterEncoder{};
        EncodedRaster encoded_raster = raster->encode(encoder);
        return std::move(encoded_raster.m_buffer);
    }
};

std::unique_ptr<Slic3r::ISlaRasterizer> create_sl1_rasterizer(const SLAPrintConfigView& cfg){
    return std::make_unique<Sl1Rasterizer>(cfg);
}
} // namespace Slic3r
