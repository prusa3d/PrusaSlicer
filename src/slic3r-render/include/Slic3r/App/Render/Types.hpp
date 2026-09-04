#pragma once

#include <vector>
#include <memory>

namespace Slic3r::App::Render {

class ScreenInfo;
class Texture;
using TexturePtr = std::shared_ptr<Texture>;
using TexturePtrs = std::vector<TexturePtr>;

struct RgbaF
{
    float r;
    float g;
    float b;
    float a;
};

enum class BufferTarget {
    VertexBuffer,
    IndexBuffer,
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    TextureBuffer,
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
};

enum class FramebufferTarget {
    Framebuffer,
    DrawFramebuffer,
    ReadFramebuffer,
};

enum class TextureTarget {
    Texture1D,
    Texture2D,
    Texture3D,
    Texture1DArray,
    Texture2DArray,
    TextureRectangle, 
    TextureCubeMap,
    TextureCubeMapArray,
    TextureBuffer,
    Texture2DMultisample,
    Texture2DMultisampleArray
};

enum class BufferUsage {
    StaticDraw,
    DynamicDraw,
    StreamDraw
};

enum class BufferAccess
{
    ReadOnly,
    WriteOnly,
    ReadWrite
};

enum class TextureMinFilter
{
    Nearest,
    Linear,
    MipMapNearestNearest,
    MipMapLinearNearest,
    MipMapNearestLinear,
    MipMapLinearLinear,
};

enum class TextureMagFilter
{
    Nearest,
    Linear,
};

enum class TextureWrap
{
    ClampToEdge,
    ClampToBorder,
    Repeat,
    MirroredRepeat,
    MirrorClampToEdge,
};

struct Rect
{
    int x;
    int y;
    int width;
    int height;

    static Rect from(int x, int y, const ScreenInfo& screen);
};

enum class BlendFactor
{
    Zero = 0,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha
};

struct BlendOp
{
    BlendFactor src = BlendFactor::One;
    BlendFactor dst = BlendFactor::Zero;
};

enum class BlendEquation
{
    Add = 0,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

struct Blending
{
    BlendOp rgb;
    BlendOp alpha;
    BlendEquation equation{BlendEquation::Add};
};

enum class CullFaceMode
{
    Front,
    Back,
    FrontAndBack
};

enum class PrimitiveType
{
    Points = 0,
    LineStrip,
    LineLoop,
    Lines,
    TriangleStrip,
    TriangleFan,
    Triangles
};

enum class ShaderType
{
    Vertex,
    Fragment,
    Geometry,
    TessEvaluation,
    TessControl,
    Compute,
    Count
};

enum class BlitFramebufferMask
{
    ColorBufferBit,
    DepthBufferBit,
    StencilBufferBit
};

enum class BlitFramebufferFilter
{
    Nearest,
    Linear
};

/**
 * Vertex Attribute semantic as recognized by Shader
 */
enum class VertexAttribType
{
    Vertex = 0,
    Normal,
    TexCoord0,
    Color,
    Extra,
    PaletteIndex
};


/**
 * Data type of attribute representation in memory
 */
enum class DataType
{
    Float = 0,
    Byte,
    UByte,
    Short
};

enum class IndexType
{
    UByte = 0,
    UShort,
    UInt
};

struct Shadows
{
    bool cast{ false };
    bool receive{ false };
};

template <typename I> struct IndexTypeTraits {};
template <> struct IndexTypeTraits<unsigned char> {
    static constexpr IndexType index_type = IndexType::UByte;
};
template <> struct IndexTypeTraits<unsigned short> {
    static constexpr IndexType index_type = IndexType::UShort;
};
template <> struct IndexTypeTraits<unsigned int> {
    static constexpr IndexType index_type = IndexType::UInt;
};

} // namespace Slic3r::App::Render
