#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace Slic3r::Biz::libpgcode {

enum class EditingType : uint8_t
{
    Replacement,
    Insertion,
    Deletion
};

struct EditingItem
{
    EditingType type;
    size_t gcode_line_id;
    std::vector<std::string> lines;
};

class LineView;

class LineViewIterator
{
public:
    LineViewIterator(const LineView* parent, size_t index);
  
    std::string_view operator*() const;
    LineViewIterator& operator++();

    bool operator==(const LineViewIterator& other) const;
    bool operator!=(const LineViewIterator& other) const;

    LineViewIterator operator+(size_t n) const;
    std::size_t operator-(const LineViewIterator& other) const;

    std::size_t line_index() const;
private:
    const LineView* m_parent; // Reference to the parent LineView
    size_t m_index;           // Current line index
};

class LineViewReverseIterator
{
public:
    LineViewReverseIterator(const LineView* parent, size_t index);

    std::string_view operator*() const;
    LineViewReverseIterator& operator++();

    bool operator==(const LineViewReverseIterator& other) const;
    bool operator!=(const LineViewReverseIterator& other) const;

    LineViewReverseIterator operator+(size_t n) const;
    std::size_t operator-(const LineViewReverseIterator& other) const;

    LineViewIterator base() const;
    std::size_t line_index() const;

private:
    const LineView* m_parent; // Reference to the parent LineView
    size_t m_index;           // Current line index
};

// This class provides std::vector<std::string>-like experience when working with lines.
// Each line inside is required to end with \n. All lines are stored in one long string,
// making the storage memory efficient and cache-friendly.
// There is [] getter to get a given line, the class is range-for ready.
// The apply_edits method can be used to ins/del/replace multiple lines efficiently.
class LineView
{
public:
    using const_iterator = LineViewIterator;  // Alias for your custom iterator type
    using const_reverse_iterator = LineViewReverseIterator;

    explicit LineView() = default;

    // Create the object from existing string of lines, separated by \n (and ending so).
    explicit LineView(std::string&& gcode);

    // Basic accessors.
    std::string_view operator[](size_t index) const;
    size_t line_length(size_t i) const;
    bool empty() const;
    size_t size() const;
    size_t size_in_bytes() const;
    const std::string& str() const;

    // Insert a line (or possibly multiple lines). The line has to end with \n.
    void push_lines(const std::string& lines);

    // Other modifing methods.
    void clear();
    void reserve(size_t n);
    void shrink_to_fit();
    void apply_edits(const std::vector<EditingItem>& edits);

    const_iterator begin() const;
    const_iterator end() const;
    const_reverse_iterator rbegin() const;
    const_reverse_iterator rend() const;

private:
    struct LineShift
    {
        uint32_t orig_line_idx{0};
        int line_idx_shift{0};
        int content_shift{0};
        std::vector<const std::string*> lines{};

        explicit LineShift(size_t line_idx);
    };

    void check_edits(const std::vector<EditingItem>& edits) const;
    void create_ins_and_del(const std::vector<EditingItem>& edits, std::vector<LineShift>& insertions, std::vector<LineShift>& deletions) const;
    void calculate_insertions_shifts(std::vector<LineShift>& insertions) const;
    void apply_insertions(const std::vector<LineShift>& insertions);
    void reindex_insertions(std::vector<LineShift>& insertions, const std::vector<LineShift>& deletions) const;
    void apply_deletions(const std::vector<LineShift>& deletions);

private:
    std::string m_content;                // Underlying string containig all lines in sequence.
    std::vector<uint32_t> m_line_indices; // Indices where each line starts.
};

} // namespace Slic3r::Biz::libpgcode
