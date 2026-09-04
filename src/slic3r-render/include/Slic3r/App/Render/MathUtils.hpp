#pragma once

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Render/Types.hpp"

namespace Slic3r::App::Render {

Domain::SquareMatrix4f ortho(float left, float right, float bottom, float top, float near_z, float far_z);
Domain::SquareMatrix4d ortho(double left, double right, double bottom, double top, double near_z, double far_z);

Domain::SquareMatrix4f frustum(float left, float right, float bottom, float top, float near_z, float far_z);
Domain::SquareMatrix4d frustum(double left, double right, double bottom, double top, double near_z, double far_z);

Domain::SquareMatrix4f perspective(float fovy, float aspect, float near_z, float far_z);
Domain::SquareMatrix4d perspective(double fovy, double aspect, double near_z, double far_z);

Domain::SquareMatrix4f look_at(const Domain::Vec3f& eye, const Domain::Vec3f& center, const Domain::Vec3f& up);
Domain::SquareMatrix4d look_at(const Domain::Vec3d& eye, const Domain::Vec3d& center, const Domain::Vec3d& up);

Domain::Vec2f viewport_transform(const Rect& viewport, const Domain::Vec3f& ndc_pos);
Domain::Vec2d viewport_transform(const Rect& viewport, const Domain::Vec3d& ndc_pos);

} // namespace Slic3r::App::Render
