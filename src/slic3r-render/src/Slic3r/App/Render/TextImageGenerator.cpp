#include "Slic3r/App/Render/TextImageGenerator.hpp"
#include "Slic3r/Domain/Image.hpp"

#include "Slic3r/Assert.hpp"

#include <boost/nowide/cstdio.hpp>

#include <array>
#include <cstring>

namespace Slic3r::App::Render {

std::vector<uint8_t> load_from_file(const std::string& filename)
{
    std::vector<uint8_t> ret;

    FILE* f = boost::nowide::fopen(filename.c_str(), "rb");
    if (f != nullptr) {
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        ret.resize(file_size);
        rewind(f);
        if (file_size > 0) {
            size_t read = fread(ret.data(), 1, file_size, f);
            if (read != static_cast<size_t>(file_size))
                ret.clear();
        }
        fclose(f);
    }

    return ret;
}

TextImageGenerator::TextImageGenerator(const std::string& font_filename, uint8_t font_height)
{
    std::vector<uint8_t> data = load_from_file(font_filename);
    std::vector<uint8_t> packed_atlas(m_atlas.width * m_atlas.height);
    m_atlas.packed_chars.resize(m_atlas.chars_count);

    stbtt_pack_context spc{};
    (void)stbtt_PackBegin(&spc, packed_atlas.data(), m_atlas.width, m_atlas.height, 0, 1, nullptr);

    static constexpr float MAGIC_NUMBER = 16.0f / 9.0f;
    float font_size = MAGIC_NUMBER * float(font_height);

    (void)stbtt_PackFontRange(&spc, data.data(), 0, font_size, m_atlas.first_char, m_atlas.chars_count, m_atlas.packed_chars.data());
    stbtt_PackEnd(&spc);

    // generate atlas bitmap
    m_atlas.bitmap.resize(4 * m_atlas.width * m_atlas.height);
    for (int r = 0; r < m_atlas.height; ++r) {
        for (int c = 0; c < m_atlas.width; ++c) {
            int px_base = r * m_atlas.width + c;
            int px_rgba = 4 * px_base;
            // default color is white
            m_atlas.bitmap[px_rgba + 0] = 255;
            m_atlas.bitmap[px_rgba + 1] = 255;
            m_atlas.bitmap[px_rgba + 2] = 255;
            m_atlas.bitmap[px_rgba + 3] = packed_atlas[px_base];
        }
    }
}

Domain::Image TextImageGenerator::to_image(const std::string& text, uint8_t padding_x, uint8_t padding_y, const std::optional<Domain::ColorRGB>& color)
{
    int w = 0;
    int h = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        uint8_t ch = uint8_t(text[i]);
        if (m_atlas.first_char <= ch && ch < m_atlas.first_char + m_atlas.chars_count) {
            const stbtt_packedchar& char_data = m_atlas.packed_chars[ch - m_atlas.first_char];
            if (i < text.length() - 1)
                w += int(char_data.xoff2);
            else
                w += int(char_data.x1 - char_data.x0);
            h = std::max<int>(h, char_data.y1 - char_data.y0);
        }
    }

    w += 2 * padding_x;
    h += 2 * padding_y;

    std::vector<uint8_t> bitmap(4 * w * h, 0);

    int x = padding_x;
    int y = padding_y;
    for (size_t i = 0; i < text.length(); ++i) {
        uint8_t ch = uint8_t(text[i]);
        if (m_atlas.first_char <= ch && ch < m_atlas.first_char + m_atlas.chars_count) {
            const stbtt_packedchar& char_data = m_atlas.packed_chars[ch - m_atlas.first_char];
            for (int r = char_data.y0; r < char_data.y1; ++r) {
                size_t size = size_t(4 * (char_data.x1 - char_data.x0));
                size_t src_offset = size_t(4 * (r * m_atlas.width + char_data.x0));
                size_t dst_offset = size_t(4 * ((y + r - char_data.y0) * w + x));
                memcpy(bitmap.data() + dst_offset, m_atlas.bitmap.data() + src_offset, size);
                if (color.has_value()) {
                    // Replace color with the given one
                    for (int c = 0; c < char_data.x1 - char_data.x0; ++c) {
                        size_t px_offset = dst_offset + 4 * c;
                        bitmap[px_offset + 0] = color->r_uchar();
                        bitmap[px_offset + 1] = color->g_uchar();
                        bitmap[px_offset + 2] = color->b_uchar();
                    }
                }
            }
            if (i < text.length() - 1)
                x += int(char_data.xoff2);
            else
                x += int(char_data.x1 - char_data.x0);
        }
    }

    return Domain::Image(Domain::PixelFormat::RGBA8, w, h, std::move(bitmap));
}

} // namespace Slic3r::App::Render
