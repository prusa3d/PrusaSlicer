#include "Slic3r/Domain/CutConnector.hpp"

#include <random>


namespace Slic3r::Domain {

bool CutConnectorAttributes::operator<(const CutConnectorAttributes& other) const
{
    return this->type <  other.type || (this->type  == other.type  && this->style < other.style) ||
          (this->type == other.type &&  this->style == other.style && this->shape < other.shape);
}

bool CutConnectorAttributes::operator==(const CutConnectorAttributes& other) const
{
    return this->type == other.type && this->style == other.style && this->shape == other.shape;
}

bool CutId::operator<(const CutId& rhs) const { return this->m_unique_id < rhs.m_unique_id; }

CutId& CutId::operator=(const CutId& rhs)
{
    this->m_unique_id = rhs.id();
    this->m_check_sum = rhs.check_sum();
    this->m_connectors_cnt = rhs.connectors_cnt();
    return *this;
}

void CutId::invalidate()
{
    m_unique_id = 0;
    m_check_sum = 1;
    m_connectors_cnt = 0;
}

void CutId::init()
{
    std::random_device rd;
    std::mt19937_64 mt(rd() + time(NULL));
    std::uniform_int_distribution<size_t> dist(1, std::numeric_limits<size_t>::max());
    m_unique_id = dist(mt);
}

bool CutId::has_same_id(const CutId& rhs) const { return this->id() == rhs.id(); }

bool CutId::is_equal(const CutId& rhs) const
{
    return this->id() == rhs.id() && this->check_sum() == rhs.check_sum() && this->connectors_cnt() == rhs.connectors_cnt();
}

size_t CutId::id() const { return m_unique_id; }

bool CutId::valid() const { return m_unique_id != 0; }

size_t CutId::check_sum() const { return m_check_sum; }

void CutId::increase_check_sum(const size_t cnt) { m_check_sum += cnt; }

size_t CutId::connectors_cnt() const { return m_connectors_cnt; }

void CutId::increase_connectors_cnt(const size_t connectors_cnt)
{
    m_connectors_cnt += connectors_cnt;
}

} // namespace Slic3r::Domain
