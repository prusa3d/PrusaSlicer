#include "Slic3r/Domain/Polyline.hpp"

namespace Slic3r::Domain {

Polyline& Polyline::operator=(const Polyline& other)
{
    this->points = other.points;
    return *this;
}

Polyline& Polyline::operator=(Polyline&& other) noexcept
{
    this->points = std::move(other.points);
    return *this;
}

bool Polyline::operator==(const Polyline& rhs) const { return this->points == rhs.points; }

bool Polyline::operator!=(const Polyline& rhs) const { return this->points != rhs.points; }

void Polyline::append(const Polyline& other) { this->append(other.points); }

void Polyline::append(Polyline&& other) { this->append(std::move(other.points)); }

const Point& Polyline::last_point() const { return this->points.back(); }

bool Polyline::is_closed() const { return this->points.front() == this->points.back(); }

double Polyline::length() const
{
    double total_length = 0.;
    for (size_t idx = 1; idx < this->points.size(); ++idx) {
        total_length += (this->points[idx] - this->points[idx - 1]).cast<double>().norm();
    }

    return total_length;
}

} // namespace Slic3r::Domain
