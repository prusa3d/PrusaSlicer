#ifndef slic3r_SLA_SuppotstIslands_IStackFunction_hpp_
#define slic3r_SLA_SuppotstIslands_IStackFunction_hpp_

#include <stack>
#include <memory> 

namespace Slic3r::sla {

/// <summary>
/// Interface for objects inside of CallStack.
/// It is way to prevent stack overflow inside recurrent functions.
/// </summary>
class IStackFunction
{
public:
    virtual ~IStackFunction() = default;
    virtual void process(std::stack<std::unique_ptr<IStackFunction>> &call_stack) = 0;
};

using CallStack = std::stack<std::unique_ptr<IStackFunction>>;

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_IStackFunction_hpp_
