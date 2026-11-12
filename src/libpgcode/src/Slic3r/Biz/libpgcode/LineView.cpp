#include "Slic3r/Biz/libpgcode/LineView.hpp"

#include <cassert>

namespace Slic3r::Biz::libpgcode {

LineViewIterator::LineViewIterator(const LineView* parent, size_t index)
    : m_parent(parent)
    , m_index(index)
{}

std::string_view LineViewIterator::operator*() const
{
    return (*m_parent)[m_index];
}

LineViewIterator& LineViewIterator::operator++()
{
    ++m_index;
    return *this;
}

bool LineViewIterator::operator==(const LineViewIterator& other) const
{
    return m_index == other.m_index;
}

bool LineViewIterator::operator!=(const LineViewIterator& other) const
{
    return !(*this == other);
}

std::size_t LineViewIterator::line_index() const {
    return m_index;
}

LineViewIterator LineViewIterator::operator+(size_t n) const
{
    if (m_index + n > m_parent->size())
        throw std::out_of_range("Iterator out of range");
    return LineViewIterator(m_parent, m_index + n);
}

std::size_t LineViewIterator::operator-(const LineViewIterator& other) const
{
    const int result{static_cast<int>(m_index) - static_cast<int>(other.m_index)};
    if (result < 0)
        throw std::out_of_range("Iterator out of range");
    return result;
}

LineViewReverseIterator::LineViewReverseIterator(const LineView* parent, size_t index)
    : m_parent(parent)
    , m_index(index)
{}

LineViewReverseIterator& LineViewReverseIterator::operator++()
{
    --m_index;
    return *this;
}

std::string_view LineViewReverseIterator::operator*() const
{
    return (*m_parent)[m_index];
}

bool LineViewReverseIterator::operator==(const LineViewReverseIterator& other) const
{
    return m_index == other.m_index;
}

bool LineViewReverseIterator::operator!=(const LineViewReverseIterator& other) const
{
    return !(*this == other);
}

LineViewReverseIterator LineViewReverseIterator::operator+(size_t n) const
{
    if (int(m_index - n) < 0)
        throw std::out_of_range("Iterator out of range");
    return LineViewReverseIterator(m_parent, m_index - n);
}

std::size_t LineViewReverseIterator::operator-(const LineViewReverseIterator& other) const
{
    const int result{static_cast<int>(other.m_index) - static_cast<int>(m_index)};
    if (result < 0)
        throw std::out_of_range("Iterator out of range");
    return result;
}

LineViewIterator LineViewReverseIterator::base() const
{
    return LineViewIterator(m_parent, m_index + 1);
}

std::size_t LineViewReverseIterator::line_index() const {
    return m_index;
}

LineView::LineShift::LineShift(size_t line_idx)
    : orig_line_idx{ uint32_t(line_idx) }
{}

LineView::LineView(std::string&& lines)
{
    if (lines.empty() || lines.back() != '\n')
        throw std::runtime_error("LineView internal error: line has to end with '\\n'");

    m_content = std::move(lines);
    m_line_indices.emplace_back(0);
    for (uint32_t i = 1; i < m_content.size(); ++i) {
        if (m_content[i-1] == '\n')
            m_line_indices.emplace_back(i);
    }
}

bool LineView::empty() const
{
    return m_line_indices.size() == 0;
}

size_t LineView::size() const
{
    return m_line_indices.size();
}

size_t LineView::size_in_bytes() const
{
    size_t ret = m_line_indices.capacity() * sizeof(uint32_t);
    ret += m_content.capacity();
    return ret;
}

const std::string& LineView::str() const {
    return m_content;
}

void LineView::check_edits(const std::vector<EditingItem>& edits) const
{
    for (size_t i = 0; i < edits.size(); ++i) {
        if (i != 0 && edits[i-1].gcode_line_id >= edits[i].gcode_line_id)
            throw std::runtime_error("LineView internal error: Edits need to be ordered and unique.");
        if (edits[i].gcode_line_id >= m_line_indices.size())
            throw std::runtime_error("LineView internal error: Trying to edit a line that does not exist.");
        if (std::any_of(edits[i].lines.begin(), edits[i].lines.end(), [](const std::string& s) { return s.empty() || s.back() != '\n'; }))
            throw std::runtime_error("LineView internal error: Lines to insert must end with a '\\n'");
    }
}

void LineView::create_ins_and_del(const std::vector<EditingItem>& edits, std::vector<LineShift>& insertions, std::vector<LineShift>& deletions) const
{
    insertions.clear();
    deletions.clear();
    for (const EditingItem& edit : edits) {
        if (edit.gcode_line_id >= m_line_indices.size())
            throw std::out_of_range("LineView does not support insertion at the end.");
        if (edit.type == EditingType::Deletion)
            deletions.emplace_back(LineShift{edit.gcode_line_id});
        else if (edit.type == EditingType::Insertion) {
            if (insertions.empty() || insertions.back().orig_line_idx != edit.gcode_line_id)
                insertions.emplace_back(LineShift{edit.gcode_line_id});
            else {                
                // Previous line was replaced by something, which created an insertion on this
                // position. Merge the lines from this one to it.
            }
            for (const std::string& s : edit.lines)
                insertions.back().lines.emplace_back(&s);
        }
        else if (edit.type == EditingType::Replacement) {
            deletions.emplace_back(LineShift{edit.gcode_line_id});
            insertions.emplace_back(LineShift{edit.gcode_line_id + 1});
            for (const std::string& s : edit.lines)
                    insertions.back().lines.emplace_back(&s);
        }
    }
}
void LineView::calculate_insertions_shifts(std::vector<LineShift>& insertions) const
{
    for (int i=int(insertions.size()) - 1; i>=0; --i) {
        // Calculate how all lines after line_idx (including) would shift
        // if this was the only insertion.
        int line_idx_shift = 0;
        int content_shift = 0;

        for (const std::string* s : insertions[i].lines) {
            ++line_idx_shift;
            content_shift += int(s->size());
        }

        // Now apply this shift to all lines which were already processed.
        for (int j=i; j<int(insertions.size()); ++j) {
            insertions[j].line_idx_shift += line_idx_shift;
            insertions[j].content_shift  += content_shift;
        }
    }
}

void LineView::apply_insertions(const std::vector<LineShift>& insertions)
{
    if (insertions.empty())
        return;
    // Iterate over the list of changes from the back and process them.
    // We will modify the content in-place.
    std::vector<uint32_t> line_indices_new;
    line_indices_new.resize(m_line_indices.size() + insertions.back().line_idx_shift);

    int first_fixed_idx = int(m_line_indices.size());
    int first_fixed_content = int(m_content.size());
    m_content.resize(m_content.size() + std::max(0, insertions.back().content_shift), '_');

    for (int edit_idx = int(insertions.size())-1; edit_idx >=0; --edit_idx) {
        const LineShift& ls = insertions[edit_idx];

        for (int i=int(first_fixed_idx)-1; i>=int(ls.orig_line_idx); --i) {
            line_indices_new[i+ls.line_idx_shift] = m_line_indices[i] + ls.content_shift;

            int content_beg = m_line_indices[i];
            int content_end = first_fixed_content;
            for (int j=content_end-1; j>=content_beg; --j)
                m_content[j+ls.content_shift] = m_content[j];
            first_fixed_content = content_beg;
        }
        first_fixed_idx = ls.orig_line_idx;

        // Now insert the new strings.
        int offset = 0;
        for (int new_line_idx = int(ls.lines.size())-1; new_line_idx >= 0; --new_line_idx) {
            const std::string& s = *ls.lines[new_line_idx];
            int content_pos = m_line_indices[ls.orig_line_idx] + ls.content_shift - int(s.size()) - offset;
            std::copy(s.begin(), s.end(), m_content.begin() + content_pos);
            offset += int(s.size());
            line_indices_new[ls.orig_line_idx + ls.line_idx_shift -(ls.lines.size()-new_line_idx)] = content_pos;
        }
    }
    for (int i=int(first_fixed_idx)-1; i>=0; i--)
        line_indices_new[i] = m_line_indices[i];
    m_line_indices = line_indices_new;
}

void LineView::apply_deletions(const std::vector<LineShift>& deletions)
{
    if (deletions.empty())
        return;
    uint32_t old_line_idx = 0;
    uint32_t new_line_idx = 0;
    int new_content_idx = 0;
    int next_del_idx = 0;

    while (old_line_idx < m_line_indices.size()) {
        if (next_del_idx < int(deletions.size()) && deletions[next_del_idx].orig_line_idx <= old_line_idx) {
            ++next_del_idx;
        } else {
            m_line_indices[new_line_idx] = new_content_idx;
            for (size_t j = m_line_indices[old_line_idx]; j < (old_line_idx+1 < m_line_indices.size() ? m_line_indices[old_line_idx + 1] : m_content.size()); ++j) {
                m_content[new_content_idx] = m_content[j];
                ++new_content_idx;
            }
            ++new_line_idx;
        }
        ++old_line_idx;
    }
    m_line_indices.resize(new_line_idx);
    m_content.resize(new_content_idx);
}

void LineView::reindex_insertions(std::vector<LineShift>& insertions, const std::vector<LineShift>& deletions) const
{
    if (insertions.empty())
        return;

    // Deletions were already applied, lines referenced by insertions are now in different places. Update them.
    // This assumes that vectors are sorted in orig_line_idx. The line is shifted 
    int next_deletion_idx = 0;

    for (LineShift& insertion : insertions) {
        while (static_cast<size_t>(next_deletion_idx) < deletions.size() && deletions[next_deletion_idx].orig_line_idx < insertion.orig_line_idx)
            ++next_deletion_idx;        
        insertion.orig_line_idx -= next_deletion_idx;
    }
    // If two insertions ended up on the same line, merge them together.
    for (size_t i = 1; i < insertions.size(); ++i) {
        if (insertions[i].orig_line_idx == insertions[i-1].orig_line_idx) {
            for (const std::string* s : insertions[i].lines)
                insertions[i-1].lines.emplace_back(s);
            insertions[i].lines.clear();
        }
    }
    insertions.erase(std::remove_if(insertions.begin(), insertions.end(), [](const LineShift& ls) { return ls.lines.empty(); }), insertions.end());
}

void LineView::clear()
{
    m_content.clear(); 
    m_line_indices.clear();
}

void LineView::reserve(size_t n)
{
    m_content.reserve(n);
}

void LineView::shrink_to_fit()
{
    m_content.shrink_to_fit();
    m_line_indices.shrink_to_fit();
};

void LineView::apply_edits(const std::vector<EditingItem>& edits)
{
    if (edits.empty())
        return;

    std::vector<LineShift> insertions;
    std::vector<LineShift> deletions;

    check_edits(edits);
    create_ins_and_del(edits, insertions, deletions);
    apply_deletions(deletions);
    reindex_insertions(insertions, deletions);
    calculate_insertions_shifts(insertions);
    apply_insertions(insertions);
}

LineViewIterator LineView::begin() const { return const_iterator(this, 0); }
LineViewIterator LineView::end() const { return const_iterator(this, m_line_indices.size()); }
LineViewReverseIterator LineView::rbegin() const { return const_reverse_iterator(this, size() - 1); }
LineViewReverseIterator LineView::rend() const { return const_reverse_iterator(this, size_t(-1)); }

std::string_view LineView::operator[](size_t index) const
{
    if (index >= m_line_indices.size()) {
        throw std::out_of_range("LineView: Index out of range");
    }
    size_t start = m_line_indices[index];
    size_t end = (index + 1 < m_line_indices.size()) ? m_line_indices[index + 1]  : m_content.size();
    return std::string_view(m_content.data() + start, end - start);
}

void LineView::push_lines(const std::string& lines)
{
    if (lines.empty() || lines.back() != '\n')
        throw std::runtime_error("LineView internal error: line has to end with '\\n'");

    auto newline_it{std::find(lines.begin(), lines.end(), '\n')};
    m_line_indices.push_back(m_content.size());
    while (std::next(newline_it) != lines.end()) {
        m_line_indices.push_back(m_content.size() + std::distance(lines.begin(), newline_it) + 1);
        newline_it = std::find(std::next(newline_it), lines.end(), '\n');
    }

    m_content.insert(m_content.end(), lines.begin(), lines.end());
}

size_t LineView::line_length(size_t i) const
{
    return (i+1 < m_line_indices.size() ? m_line_indices[i+1] : m_content.size()) - m_line_indices[i];
}

} // namespace Slic3r::Biz::libpgcode
