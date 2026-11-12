#include "Slic3r/Biz/WX/SingleInstanceChecker.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz::WX {
SingleInstanceChecker::SingleInstanceChecker(const std::string& name, const std::string& path) 
    : m_wxchecker() 
{
    bool created = m_wxchecker.Create(App::WX::from_u8(name), App::WX::from_u8(path));
    ASSERT(created);
}
bool SingleInstanceChecker::is_another_running() 
{
    printf("IsAnotherRunning %d\n", m_wxchecker.IsAnotherRunning());
    return m_wxchecker.IsAnotherRunning();
}
}