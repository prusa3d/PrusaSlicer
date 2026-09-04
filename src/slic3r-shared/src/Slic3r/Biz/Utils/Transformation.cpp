#include "Slic3r/Biz/Utils/Transformation.hpp"
#include "Slic3r/Domain/Constants.hpp"

namespace Slic3r::Biz {
bool changes_z_rotation_or_position(const Domain::SquareMatrix4d& xform)
{
    // Extract the third column of the linear part (rotation/scaling).
    // This vector shows where the original Z-axis (0,0,1) points after transformation.
    Domain::Vec3d transformed_z = xform.topLeftCorner<3, 3>().col(2);

    // Handle the edge case where the Z-axis is scaled to zero length.
    // Such a transform is highly distorting and should be flagged.
    if (transformed_z.norm() < 1e-9) {
        return true;
    }

    // Normalize the vector to get its direction, ignoring any scaling.
    transformed_z.normalize();

    double dz = xform(2, 3);

    // Check if the direction is still parallel to the original Z-axis and if there is a delta z.
    // We use isApprox() for safe floating-point comparison. A dot product
    // check like `abs(transformed_z.dot(Eigen::Vector3d::UnitZ()))` could also work.
    return !transformed_z.isApprox(Domain::Vec3d::UnitZ()) || std::abs(dz) > Domain::EPSILON;
}
} // namespace Slic3r::Biz
