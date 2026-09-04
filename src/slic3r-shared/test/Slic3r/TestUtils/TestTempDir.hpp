#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace Tests {

// A simple RAII helper for a temporary test directory
class TestTempDir {
public:
    TestTempDir() {
        auto temp_path = boost::filesystem::temp_directory_path();
        std::string dir_name = "catch2_test_" + boost::uuids::to_string(boost::uuids::random_generator()());
        m_path = temp_path / dir_name;
        boost::filesystem::create_directory(m_path);
    }

    ~TestTempDir() {
        try {
            boost::filesystem::remove_all(m_path);
        } catch (const boost::filesystem::filesystem_error& ex) {
            SPDLOG_ERROR("Unable to clean temporary dir {} (dir name: {})", ex.what(), m_path.string());
        }
    }

    const boost::filesystem::path& path() const {
        return m_path;
    }

private:
    boost::filesystem::path m_path;
};

} // namespace Tests
