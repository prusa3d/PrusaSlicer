///|/ Copyright (c) Prusa Research 2017 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GCodeReader.hpp"

#include <boost/nowide/cstdio.hpp>
#include <fast_float.h>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "Utils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

static inline char get_extrusion_axis_char(const GCodeConfig &config)
{
    std::string axis = get_extrusion_axis(config);
    assert(axis.size() <= 1);
    // Return 0 for gcfNoExtrusion
    return axis.empty() ? 0 : axis[0];
}

void GCodeReader::apply_config(const GCodeConfig &config)
{
    m_config = config;
    m_extrusion_axis = get_extrusion_axis_char(m_config);
}

void GCodeReader::apply_config(const DynamicPrintConfig &config)
{
    m_config.apply(config, true);
    m_extrusion_axis = get_extrusion_axis_char(m_config);
}

const char* GCodeReader::parse_line_internal(const char *ptr, const char *end, GCodeLine &gline, std::pair<const char*, const char*> &command)
{
    assert(is_decimal_separator_point());
    
    // command and args
    const char *c = ptr;
    {
        // Skip the whitespaces.
        command.first = skip_whitespaces(c);
        // Skip the command.
        c = command.second = skip_word(command.first);
        // Up to the end of line or comment.
		while (! is_end_of_gcode_line(*c)) {
            // Skip whitespaces.
            c = skip_whitespaces(c);
			if (is_end_of_gcode_line(*c))
				break;
            // Check the name of the axis.
            Axis axis = NUM_AXES_WITH_UNKNOWN;
            switch (*c) {
            case 'X': axis = X; break;
            case 'Y': axis = Y; break;
            case 'Z': axis = Z; break;
            case 'F': axis = F; break;
            default:
                if (*c == m_extrusion_axis) {
                    if (m_extrusion_axis != 0)
                        axis = E;
                } else if (*c >= 'A' && *c <= 'Z')
                	// Unknown axis, but we still want to remember that such a axis was seen.
                	axis = UNKNOWN_AXIS;
                break;
            }
            if (axis != NUM_AXES_WITH_UNKNOWN) {
                // Try to parse the numeric value.
                double v;
                c = skip_whitespaces(++ c);
                auto [pend, ec] = fast_float::from_chars(c, end, v);
                if (pend != c && is_end_of_word(*pend)) {
                    // The axis value has been parsed correctly.
                    if (axis != UNKNOWN_AXIS)
	                    gline.m_axis[int(axis)] = float(v);
                    gline.m_mask |= 1 << int(axis);
                    c = pend;
                } else
                    // Skip the rest of the word.
                    c = skip_word(c);
            } else
                // Skip the rest of the word.
                c = skip_word(c);
        }
    }
    
    if (gline.has(E) && m_config.use_relative_e_distances)
        m_position[E] = 0;

    // Skip the rest of the line.
    for (; ! is_end_of_line(*c); ++ c);

    // Copy the raw string including the comment, without the trailing newlines.
    if (c > ptr)
        gline.m_raw.assign(ptr, c);

    // Skip the trailing newlines.
	if (*c == '\r')
		++ c;
	if (*c == '\n')
		++ c;

    if (m_verbose)
        std::cout << gline.m_raw << std::endl;

    return c;
}

void GCodeReader::update_coordinates(GCodeLine &gline, std::pair<const char*, const char*> &command)
{
    if (*command.first == 'G') {
        int cmd_len = int(command.second - command.first);
        if ((cmd_len == 2 && (command.first[1] == '0' || command.first[1] == '1')) ||
            (cmd_len == 3 &&  command.first[1] == '9' && command.first[2] == '2')) {
            for (size_t i = 0; i < NUM_AXES; ++ i)
                if (gline.has(Axis(i)))
                    m_position[i] = gline.value(Axis(i));
        }
    }
}

template<typename ParseLineCallback, typename LineEndCallback>
bool GCodeReader::parse_file_raw_internal(const std::string &filename, ParseLineCallback parse_line_callback, LineEndCallback line_end_callback)
{
    FilePtr in{ boost::nowide::fopen(filename.c_str(), "rb") };

    fseek(in.f, 0, SEEK_END);
    const long file_size = ftell(in.f);
    rewind(in.f);

    // Read the input stream 64kB at a time, extract lines and process them.
    std::vector<char> buffer(65536 * 10, 0);
    // Line buffer.
    std::string gcode_line;
    size_t file_pos = 0;
    m_parsing = true;
    for (;;) {
        size_t cnt_read = ::fread(buffer.data(), 1, buffer.size(), in.f);
        if (::ferror(in.f))
            return false;

        bool eof       = cnt_read == 0;
        auto it        = buffer.begin();
        auto it_bufend = buffer.begin() + cnt_read;
        while (it != it_bufend || (eof && ! gcode_line.empty())) {
            // Find end of line.
            bool eol    = false;
            auto it_end = it;
            for (; it_end != it_bufend && ! (eol = *it_end == '\r' || *it_end == '\n'); ++ it_end)
                if (*it_end == '\n')
                    line_end_callback(file_pos + (it_end - buffer.begin()) + 1);
            // End of line is indicated also if end of file was reached.
            eol |= eof && it_end == it_bufend;
            if (eol) {
                if (gcode_line.empty())
                    parse_line_callback(&(*it), &(*it_end));
                else {
                    gcode_line.insert(gcode_line.end(), it, it_end);
                    parse_line_callback(gcode_line.c_str(), gcode_line.c_str() + gcode_line.size());
                    gcode_line.clear();
                }
                if (! m_parsing)
                    // The callback wishes to exit.
                    return true;
            } else
                gcode_line.insert(gcode_line.end(), it, it_end);
            // Skip EOL.
            it = it_end; 
            if (it != it_bufend && *it == '\r')
                ++ it;
            if (it != it_bufend && *it == '\n') {
                line_end_callback(file_pos + (it - buffer.begin()) + 1);
                ++ it;
            }
        }
        if (eof)
            break;
        file_pos += cnt_read;
        if (m_progress_callback != nullptr)
            m_progress_callback(static_cast<float>(file_pos) / static_cast<float>(file_size));
    }
    return true;
}

template<typename ParseLineCallback, typename LineEndCallback>
bool GCodeReader::parse_file_internal(const std::string &filename, ParseLineCallback parse_line_callback, LineEndCallback line_end_callback)
{
    GCodeLine gline;    
    return this->parse_file_raw_internal(filename, 
        [this, &gline, parse_line_callback](const char *begin, const char *end) {
            gline.reset();
            this->parse_line(begin, end, gline, parse_line_callback);
        }, 
        line_end_callback);
}

bool GCodeReader::parse_file(const std::string &file, callback_t callback)
{
    return this->parse_file_internal(file, callback, [](size_t){});
}

bool GCodeReader::parse_file(const std::string& file, callback_t callback, std::vector<std::vector<size_t>>& lines_ends)
{
    lines_ends.clear();
    lines_ends.push_back(std::vector<size_t>());
    return this->parse_file_internal(file, callback, [&lines_ends](size_t file_pos) { lines_ends.front().emplace_back(file_pos); });
}

bool GCodeReader::parse_file_raw(const std::string &filename, raw_line_callback_t line_callback)
{
    return this->parse_file_raw_internal(filename,
        [this, line_callback](const char *begin, const char *end) { line_callback(*this, begin, end); }, 
        [](size_t){});
}

const char* GCodeReader::axis_pos(const char *raw_str, char axis)
{
    const char *c = raw_str;
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (! is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis)
            return c;
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return nullptr;
}

bool GCodeReader::GCodeLine::has(char axis) const
{
    return GCodeReader::axis_pos(this->raw().c_str(), axis);
}

std::string_view GCodeReader::GCodeLine::axis_pos(char axis) const
{ 
    const std::string &s = this->raw();
    const char *c = GCodeReader::axis_pos(this->raw().c_str(), axis);
    return c ? std::string_view{ c, s.size() - (c - s.data()) } : std::string_view();
}

bool GCodeReader::GCodeLine::has_value(std::string_view axis_pos, float &value)
{
    if (const char *c = axis_pos.data(); c) {
        // Try to parse the numeric value.
        double v = 0.;
        const char *end = axis_pos.data() + axis_pos.size();
        auto [pend, ec] = fast_float::from_chars(++ c, end, v);
        if (pend != c && is_end_of_word(*pend)) {
            // The axis value has been parsed correctly.
            value = float(v);
            return true;
        }
    }
    return false;
}

bool GCodeReader::GCodeLine::has_value(char axis, float &value) const
{
    assert(is_decimal_separator_point());
    return this->has_value(this->axis_pos(axis), value);
}

bool GCodeReader::GCodeLine::has_value(std::string_view axis_pos, int &value)
{
    if (const char *c = axis_pos.data(); c) {
        // Try to parse the numeric value.
        char   *pend = nullptr;
        long    v = strtol(++ c, &pend, 10);
        if (pend != nullptr && is_end_of_word(*pend)) {
            // The axis value has been parsed correctly.
            value = int(v);
            return true;
        }
    }
    return false;
}

bool GCodeReader::GCodeLine::has_value(char axis, int &value) const
{
    return this->has_value(this->axis_pos(axis), value);
}

void GCodeReader::GCodeLine::set(const GCodeReader &reader, const Axis axis, const float new_value, const int decimal_digits)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_digits) << new_value;

    char match[3] = " X";
    if (int(axis) < 3)
        match[1] += int(axis);
    else if (axis == F)
        match[1] = 'F';
    else {
        assert(axis == E);
        // Extruder axis is set.
        assert(reader.extrusion_axis() != 0);
        match[1] = reader.extrusion_axis();
    }

    if (this->has(axis)) {
        size_t pos = m_raw.find(match)+2;
        size_t end = m_raw.find(' ', pos+1);
        m_raw = m_raw.replace(pos, end-pos, ss.str());
    } else {
        size_t pos = m_raw.find(' ');
        if (pos == std::string::npos)
            m_raw += std::string(match) + ss.str();
        else
            m_raw = m_raw.replace(pos, 0, std::string(match) + ss.str());
    }
    m_axis[axis] = new_value;
    m_mask |= 1 << int(axis);
}

}
