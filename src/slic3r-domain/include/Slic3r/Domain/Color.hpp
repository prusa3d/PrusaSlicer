#pragma once

#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <cassert>

namespace Slic3r::Domain {

class ColorRGB
{
	std::array<float, 3> m_data{1.0f, 1.0f, 1.0f};

public:
	ColorRGB() = default;
	ColorRGB(float r, float g, float b);
	ColorRGB(unsigned char r, unsigned char g, unsigned char b);
	ColorRGB(const ColorRGB& other) = default;

	ColorRGB& operator = (const ColorRGB& other) = default;

	bool operator == (const ColorRGB& other) const;
	bool operator != (const ColorRGB& other) const;
	bool operator < (const ColorRGB& other) const;
	bool operator > (const ColorRGB& other) const;

	ColorRGB operator + (const ColorRGB& other) const;
	ColorRGB operator * (float value) const;

	const float* data() const { return m_data.data(); }

	float r() const { return m_data[0]; }
	float g() const { return m_data[1]; }
	float b() const { return m_data[2]; }

	void r(float r) { m_data[0] = std::clamp(r, 0.0f, 1.0f); }
	void g(float g) { m_data[1] = std::clamp(g, 0.0f, 1.0f); }
	void b(float b) { m_data[2] = std::clamp(b, 0.0f, 1.0f); }

	void set(unsigned int comp, float value);

	unsigned char r_uchar() const { return static_cast<unsigned char>(m_data[0] * 255.0f); }
	unsigned char g_uchar() const { return static_cast<unsigned char>(m_data[1] * 255.0f); }
	unsigned char b_uchar() const { return static_cast<unsigned char>(m_data[2] * 255.0f); }

	static const ColorRGB BLACK()       { return { 0.0f, 0.0f, 0.0f }; }
	static const ColorRGB BLUE()        { return { 0.0f, 0.0f, 1.0f }; }
	static const ColorRGB BLUEISH()     { return { 0.5f, 0.5f, 1.0f }; }
	static const ColorRGB CYAN()        { return { 0.0f, 1.0f, 1.0f }; }
	static const ColorRGB DARK_GRAY()   { return { 0.25f, 0.25f, 0.25f }; }
	static const ColorRGB DARK_YELLOW() { return { 0.5f, 0.5f, 0.0f }; }
	static const ColorRGB GRAY()        { return { 0.5f, 0.5f, 0.5f }; }
	static const ColorRGB GREEN()       { return { 0.0f, 1.0f, 0.0f }; }
	static const ColorRGB GREENISH()    { return { 0.5f, 1.0f, 0.5f }; }
	static const ColorRGB LIGHT_GRAY()  { return { 0.75f, 0.75f, 0.75f }; }
	static const ColorRGB MAGENTA()     { return { 1.0f, 0.0f, 1.0f }; }
	static const ColorRGB ORANGE()      { return { 0.92f, 0.50f, 0.26f }; }
	static const ColorRGB RED()         { return { 1.0f, 0.0f, 0.0f }; }
	static const ColorRGB REDISH()      { return { 1.0f, 0.5f, 0.5f }; }
	static const ColorRGB YELLOW()      { return { 1.0f, 1.0f, 0.0f }; }
	static const ColorRGB WHITE()       { return { 1.0f, 1.0f, 1.0f }; }

	static const ColorRGB X()           { return { 1.0f, 0.2f, 0.322f }; }
	static const ColorRGB Y()           { return { 0.545f, 0.863f, 0.0f }; }
	static const ColorRGB Z()           { return { 0.157f, 0.565f, 1.0f }; }
};

class ColorRGBA
{
	std::array<float, 4> m_data{ 1.0f, 1.0f, 1.0f, 1.0f };

public:
	ColorRGBA() = default;

    constexpr ColorRGBA(float r, float g, float b, float a) :
        m_data(
            {std::clamp(r, 0.0f, 1.0f),
             std::clamp(g, 0.0f, 1.0f),
             std::clamp(b, 0.0f, 1.0f),
             std::clamp(a, 0.0f, 1.0f)}
        )
    {}
	ColorRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	ColorRGBA(const ColorRGBA& other) = default;

	ColorRGBA& operator = (const ColorRGBA& other) = default;

	bool operator == (const ColorRGBA& other) const;
	bool operator != (const ColorRGBA& other) const;
	bool operator < (const ColorRGBA& other) const;
	bool operator > (const ColorRGBA& other) const;

	ColorRGBA operator + (const ColorRGBA& other) const;
	ColorRGBA operator * (float value) const;

	const float* data() const { return m_data.data(); }

	float r() const { return m_data[0]; }
	float g() const { return m_data[1]; }
	float b() const { return m_data[2]; }
	float a() const { return m_data[3]; }

	void r(float r) { m_data[0] = std::clamp(r, 0.0f, 1.0f); }
	void g(float g) { m_data[1] = std::clamp(g, 0.0f, 1.0f); }
	void b(float b) { m_data[2] = std::clamp(b, 0.0f, 1.0f); }
	void a(float a) { m_data[3] = std::clamp(a, 0.0f, 1.0f); }

	void set(unsigned int comp, float value);

	unsigned char r_uchar() const { return static_cast<unsigned char>(m_data[0] * 255.0f); }
	unsigned char g_uchar() const { return static_cast<unsigned char>(m_data[1] * 255.0f); }
	unsigned char b_uchar() const { return static_cast<unsigned char>(m_data[2] * 255.0f); }
	unsigned char a_uchar() const { return static_cast<unsigned char>(m_data[3] * 255.0f); }

	bool is_transparent() const { return m_data[3] < 1.0f; }

    static constexpr ColorRGBA BLACK()
	{
	    return {0.0f, 0.0f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA BLUE()
	{
	    return {0.0f, 0.0f, 1.0f, 1.0f};
	}

    static constexpr ColorRGBA BLUEISH()
	{
	    return {0.5f, 0.5f, 1.0f, 1.0f};
	}

    static constexpr ColorRGBA CYAN()
	{
	    return {0.0f, 1.0f, 1.0f, 1.0f};
	}

    static constexpr ColorRGBA DARK_GRAY()
	{
	    return {0.25f, 0.25f, 0.25f, 1.0f};
	}

    static constexpr ColorRGBA DARK_YELLOW()
	{
	    return {0.5f, 0.5f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA GRAY()
	{
	    return {0.5f, 0.5f, 0.5f, 1.0f};
	}

    static constexpr ColorRGBA GREEN()
	{
	    return {0.0f, 1.0f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA GREENISH()
	{
	    return {0.5f, 1.0f, 0.5f, 1.0f};
	}

    static constexpr ColorRGBA LIGHT_GRAY()
	{
	    return {0.75f, 0.75f, 0.75f, 1.0f};
	}

    static constexpr ColorRGBA MAGENTA()
	{
	    return {1.0f, 0.0f, 1.0f, 1.0f};
	}

    static constexpr ColorRGBA ORANGE()
	{
	    return {0.923f, 0.504f, 0.264f, 1.0f};
	}

    static constexpr ColorRGBA RED()
	{
	    return {1.0f, 0.0f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA REDISH()
	{
	    return {1.0f, 0.5f, 0.5f, 1.0f};
	}

    static constexpr ColorRGBA YELLOW()
	{
	    return {1.0f, 1.0f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA WHITE()
	{
	    return {1.0f, 1.0f, 1.0f, 1.0f};
	}

    static constexpr ColorRGBA X()
	{
	    return {1.0f, 0.2f, 0.322f, 1.0f};
	}

    static constexpr ColorRGBA Y()
	{
	    return {0.545f, 0.863f, 0.0f, 1.0f};
	}

    static constexpr ColorRGBA Z()
	{
	    return {0.157f, 0.565f, 1.0f, 1.0f};
	}
};

ColorRGB operator*(float value, const ColorRGB& other);
ColorRGBA operator*(float value, const ColorRGBA& other);

} // namespace Slic3r::Domain
