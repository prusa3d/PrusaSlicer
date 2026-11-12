#pragma once

#include "Types.hpp"
#include "WithInternal.hpp"

#include <array>
#include <string>
#include <string_view>

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Domain {
class ColorRGB;
class ColorRGBA;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {

class Device;

class Shader : public WithInternal
{
public:
    typedef std::array<std::string, static_cast<size_t>(ShaderType::Count)> ShaderFilenames;
    typedef std::array<std::string, static_cast<size_t>(ShaderType::Count)> ShaderSources;

    explicit Shader(Device& device);
    ~Shader() override;

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&&) = default;

    bool init_from_files(const std::string& name, const ShaderFilenames& filenames, const std::initializer_list<std::string_view> &defines = {});
    bool init_from_texts(const std::string& name, const ShaderSources& sources);

    const std::string& get_name() const { return m_name; }

    void set_uniform(const char* name, int value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, bool value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, float value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, double value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 2>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 3>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 2>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 3>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<double, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const float* value, size_t size) const { set_uniform(get_uniform_location(name), value, size); }
    void set_uniform(const char* name, const Domain::Transform3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Transform3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::SquareMatrix3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::SquareMatrix3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::SquareMatrix4f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::SquareMatrix4d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Vec2f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Vec2d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Vec3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Vec4f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::Vec3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::ColorRGB& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Domain::ColorRGBA& value) const { set_uniform(get_uniform_location(name), value); }

    void set_uniform(int id, int value) const;
    void set_uniform(int id, bool value) const;
    void set_uniform(int id, float value) const;
    void set_uniform(int id, double value) const;
    void set_uniform(int id, const std::array<int, 2>& value) const;
    void set_uniform(int id, const std::array<int, 3>& value) const;
    void set_uniform(int id, const std::array<int, 4>& value) const;
    void set_uniform(int id, const std::array<float, 2>& value) const;
    void set_uniform(int id, const std::array<float, 3>& value) const;
    void set_uniform(int id, const std::array<float, 4>& value) const;
    void set_uniform(int id, const std::array<double, 4>& value) const;
    void set_uniform(int id, const float* value, size_t size) const;
    void set_uniform(int id, const Domain::Transform3f& value) const;
    void set_uniform(int id, const Domain::Transform3d& value) const;
    void set_uniform(int id, const Domain::SquareMatrix3f& value) const;
    void set_uniform(int id, const Domain::SquareMatrix3d& value) const;
    void set_uniform(int id, const Domain::SquareMatrix4f& value) const;
    void set_uniform(int id, const Domain::SquareMatrix4d& value) const;
    void set_uniform(int id, const Domain::Vec2f& value) const;
    void set_uniform(int id, const Domain::Vec2d& value) const;
    void set_uniform(int id, const Domain::Vec3f& value) const;
    void set_uniform(int id, const Domain::Vec4f& value) const;
    void set_uniform(int id, const Domain::Vec3d& value) const;
    void set_uniform(int id, const Domain::ColorRGB& value) const;
    void set_uniform(int id, const Domain::ColorRGBA& value) const;

    // returns -1 if not found
    int get_attrib_location(const char* name) const;
    // returns -1 if not found
    int get_uniform_location(const char* name) const;
private:
    friend class Geometry;
    Device& m_device;

    std::string m_name;
    mutable std::vector<std::pair<std::string, int>> m_attrib_location_cache;
    mutable std::vector<std::pair<std::string, int>> m_uniform_location_cache;
};


}
