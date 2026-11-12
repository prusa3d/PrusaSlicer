#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"

#include <Slic3r/Log.hpp>
#include <Slic3r/Directories.hpp>

#include <boost/algorithm/string/predicate.hpp>

constexpr auto SCALED_EPSILON = Slic3r::Biz::Algorithms::Scaling::scaled(Slic3r::Domain::EPSILON);

using Slic3r::Domain::BoundingBox2crd;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::Line;
using Slic3r::Domain::Lines;
using Slic3r::Domain::Polyline;
using Slic3r::Domain::Polylines;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Image;
using Slic3r::Biz::Algorithms::ImageUtils::half_sampled;
using Slic3r::Biz::Algorithms::ImageUtils::flip_vertical;

using namespace Slic3r::Biz;

namespace Slic3r::App::Scene {

size_t BedRenderHelper::s_bed_texture_size = 2048;
std::unique_ptr<Render::TextImageGenerator> BedRenderHelper::s_text_to_image = nullptr;

std::shared_ptr<Render::Texture> BedRenderHelper::texture(const Domain::Bed& bed, Render::TextureManager& manager)
{
    const std::string& texture_filename = bed.texture_filename();
    if (texture_filename.empty())
        return nullptr;

    Render::ImageLoadOptions opts;
    opts.max_size_px = s_bed_texture_size;
    opts.flip_y = true;
    opts.gen_mipmaps = true;
    opts.compressed = true;

    std::shared_ptr<Render::Texture> tex = manager.get_or_create_image(texture_filename, opts);
    tex->set_filtering(Render::TextureMinFilter::MipMapLinearLinear, Render::TextureMagFilter::Linear);
    return tex;
}

std::shared_ptr<Render::Texture> BedRenderHelper::label_texture(const std::string& label, Render::TextureManager& manager,
    const std::optional<Domain::ColorRGB>& color)
{
    if (s_text_to_image == nullptr) {
        std::string font_path = Slic3r::resources_dir() + "/fonts/NotoSans-Regular.ttf";
        s_text_to_image = std::make_unique<Render::TextImageGenerator>(font_path, 128);
    }

    Image img = s_text_to_image->to_image(label, 2, 2, color);
    size_t width = img.width();
    size_t height = img.height();

    std::vector<Image> mipmaps;
    mipmaps.emplace_back(std::move(img));
    while (mipmaps.back().width() > 1 || mipmaps.back().height() > 1) {
        mipmaps.emplace_back(half_sampled(mipmaps.back()));
    }

    std::string texture_name = "bed_label_" + label;
    if (color.has_value()) {
        texture_name += "_" + std::to_string(color->r_uchar());
        texture_name += "_" + std::to_string(color->g_uchar());
        texture_name += "_" + std::to_string(color->b_uchar());
    }
    else
        texture_name += "_255_255_255";
    std::shared_ptr<Render::Texture> tex = manager.get_or_create_dynamic(texture_name, Domain::PixelFormat::RGBA8, width, height);
    tex->set_filtering(Render::TextureMinFilter::MipMapLinearLinear, Render::TextureMagFilter::Linear);
    for (size_t i = 0; i < mipmaps.size(); ++i) {
        Image& image = mipmaps[i];
        flip_vertical(image);
        image = Biz::Algorithms::ImageUtils::compress(image);
        tex->set_data(Domain::PixelFormat::RGBA_DXT5, int(i), image.width(), image.height(), image.pixels.data(), image.pixels.size());
    }
    return tex;
}

std::vector<Vec3f> BedRenderHelper::plate_grid(const Domain::Bed& bed)
{
    std::vector<Vec3f> ret;

    ExPolygon contour = ExPolygon(Algorithms::Polygon::scaled(bed.contour()));
    BoundingBox2crd bbox = Algorithms::Polygon::get_extents(contour.contour);
    if (!bbox.defined) {
        SPDLOG_ERROR("Invalid bed contour");
        return ret;
    }

    static constexpr int32_t STEP = scale_(10.0);
    Polylines gridlines;
    for (int32_t x = bbox.min.x(); x <= bbox.max.x(); x += STEP) {
        gridlines.push_back(Polyline({ x, bbox.min.y() }, { x, bbox.max.y() }));
    }
    for (int32_t y = bbox.min.y(); y <= bbox.max.y(); y += STEP) {
        gridlines.push_back(Polyline({ bbox.min.x(), y }, { bbox.max.x(), y }));
    }
    
    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Lines lines = Algorithms::Polyline::to_lines(Algorithms::ClipperUtils::intersection_pl(gridlines, Algorithms::ClipperUtils::offset(contour, float(SCALED_EPSILON))));
    // append bed contours
    Lines contour_lines = Algorithms::ExPolygon::to_lines(contour);
    std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(lines));

    ret.reserve(2 * lines.size());
    for (const Line& l : lines) {
        ret.emplace_back(Algorithms::Point::to_3d(Algorithms::Scaling::unscaled(l.a), 0.0).cast<float>());
        ret.emplace_back(Algorithms::Point::to_3d(Algorithms::Scaling::unscaled(l.b), 0.0).cast<float>());
    }
    return ret;
}

} // namespace Slic3r::App::Scene
