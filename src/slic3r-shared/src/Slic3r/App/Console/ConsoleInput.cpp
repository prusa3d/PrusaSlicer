#include "Slic3r/App/Console/ConsoleInput.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <iostream>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

using Slic3r::App::Console::InputUnavailableException;
using Slic3r::App::Console::Validation;
using Slic3r::App::Console::ValidationStatus;

// ---------------------------------------------------------------- utf-8 ----

constexpr bool is_continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

std::size_t prev_cp(const std::string& s, std::size_t i)
{
    if (i == 0)
        return 0;
    --i;
    while (i > 0 && is_continuation(static_cast<unsigned char>(s[i])))
        --i;
    return i;
}

std::size_t next_cp(const std::string& s, std::size_t i)
{
    if (i >= s.size())
        return s.size();
    ++i;
    while (i < s.size() && is_continuation(static_cast<unsigned char>(s[i])))
        ++i;
    return i;
}

// Rough display width: counts code points, good enough for ASCII/latin text.
std::size_t cp_count(std::string_view s)
{
    std::size_t n = 0;
    for (unsigned char c : s)
        if (!is_continuation(c))
            ++n;
    return n;
}

// -------------------------------------------------------------- terminal ----

bool stdin_is_tty()
{
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

// RAII: put the terminal into character-at-a-time, no-echo mode.
class RawMode
{
public:
    RawMode()
    {
#if defined(_WIN32)
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetConsoleMode(out, &old_out_mode_)) {
            SetConsoleMode(out, old_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            active_ = true;
        }
#else
        if (tcgetattr(STDIN_FILENO, &old_) == -1)
            return;
        termios raw = old_;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
            return;
        active_ = true;
#endif
    }

    ~RawMode()
    {
        restore();
    }

    void restore()
    {
        if (!active_)
            return;
        active_ = false;
#if defined(_WIN32)
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_out_mode_);
#else
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_);
#endif
    }

    RawMode(const RawMode&)            = delete;
    RawMode& operator=(const RawMode&) = delete;

private:
    bool active_ = false;
#if defined(_WIN32)
    DWORD old_out_mode_ = 0;
#else
    termios old_{};
#endif
};

// ------------------------------------------------------------------ keys ----

enum class Key
{
    Char,
    Enter,
    Tab,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    CtrlC,
    CtrlD,
    CtrlU,
    CtrlK,
    CtrlW,
    Escape,
    Unknown,
    Eof
};

struct KeyEvent
{
    Key key = Key::Unknown;
    std::string text; // for Key::Char, one full UTF-8 code point
};

#if !defined(_WIN32)
// Reads one more byte of an escape sequence, giving up after ~100 ms so that a
// lone ESC keypress does not block. Bytes already sitting in the stream buffer
// are returned immediately; only an actually empty buffer hits the timeout.
bool read_byte_timeout(char& c)
{
    termios saved{};
    bool restore = false;
    if (tcgetattr(STDIN_FILENO, &saved) == 0) {
        termios tmp     = saved;
        tmp.c_cc[VMIN]  = 0;
        tmp.c_cc[VTIME] = 1; // tenths of a second
        // TCSANOW, never TCSAFLUSH: flushing would discard typed-ahead input.
        restore = tcsetattr(STDIN_FILENO, TCSANOW, &tmp) == 0;
    }

    const bool ok = static_cast<bool>(std::cin.get(c));
    if (!ok) { // timeout looks like EOF; undo it on both layers
        std::cin.clear();
        std::clearerr(stdin);
    }

    if (restore)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return ok;
}

KeyEvent read_key()
{
    char c;
    if (!std::cin.get(c))
        return {Key::Eof, {}};
    const auto u = static_cast<unsigned char>(c);

    switch (u) {
    case 1:
        return {Key::Home, {}};
    case 3:
        return {Key::CtrlC, {}};
    case 4:
        return {Key::CtrlD, {}};
    case 5:
        return {Key::End, {}};
    case 8:
    case 127:
        return {Key::Backspace, {}};
    case 9:
        return {Key::Tab, {}};
    case 10:
    case 13:
        return {Key::Enter, {}};
    case 11:
        return {Key::CtrlK, {}};
    case 21:
        return {Key::CtrlU, {}};
    case 23:
        return {Key::CtrlW, {}};
    case 27:
        break; // escape sequence, handled below
    default:
        if (u < 32)
            return {Key::Unknown, {}};
        // Assemble the remaining bytes of a UTF-8 code point.
        {
            std::string text(1, c);
            const int extra = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
            for (int i = 0; i < extra && std::cin.get(c); ++i)
                text += c;
            return {Key::Char, std::move(text)};
        }
    }

    // ESC [ ... / ESC O ...
    char a;
    if (!read_byte_timeout(a))
        return {Key::Escape, {}};
    if (a != '[' && a != 'O')
        return {Key::Unknown, {}};

    char b;
    if (!read_byte_timeout(b))
        return {Key::Unknown, {}};
    switch (b) {
    case 'A':
        return {Key::Up, {}};
    case 'B':
        return {Key::Down, {}};
    case 'C':
        return {Key::Right, {}};
    case 'D':
        return {Key::Left, {}};
    case 'H':
        return {Key::Home, {}};
    case 'F':
        return {Key::End, {}};
    default:
        break;
    }
    if (b >= '0' && b <= '9') { // ESC [ <n> ~
        std::string num(1, b);
        char d;
        while (read_byte_timeout(d) && d >= '0' && d <= '9')
            num += d;
        if (num == "1" || num == "7")
            return {Key::Home, {}};
        if (num == "3")
            return {Key::Delete, {}};
        if (num == "4" || num == "8")
            return {Key::End, {}};
    }
    return {Key::Unknown, {}};
}
#else
KeyEvent read_key()
{
    int c = _getch();
    if (c == 0 || c == 224) { // extended key
        switch (_getch()) {
        case 75:
            return {Key::Left, {}};
        case 77:
            return {Key::Right, {}};
        case 72:
            return {Key::Up, {}};
        case 80:
            return {Key::Down, {}};
        case 71:
            return {Key::Home, {}};
        case 79:
            return {Key::End, {}};
        case 83:
            return {Key::Delete, {}};
        default:
            return {Key::Unknown, {}};
        }
    }
    switch (c) {
    case 1:
        return {Key::Home, {}};
    case 3:
        return {Key::CtrlC, {}};
    case 4:
        return {Key::CtrlD, {}};
    case 5:
        return {Key::End, {}};
    case 8:
        return {Key::Backspace, {}};
    case 9:
        return {Key::Tab, {}};
    case 11:
        return {Key::CtrlK, {}};
    case 13:
        return {Key::Enter, {}};
    case 21:
        return {Key::CtrlU, {}};
    case 23:
        return {Key::CtrlW, {}};
    case 26:
        return {Key::Eof, {}};
    case 27:
        return {Key::Escape, {}};
    default:
        if (c < 32)
            return {Key::Unknown, {}};
        return {Key::Char, std::string(1, static_cast<char>(c))};
    }
}
#endif

// --------------------------------------------------------------- drawing ----

// Draws the prompt line plus one status line below it, then parks the cursor.
void render(
    std::string_view prompt,
    const std::string& buf,
    std::size_t cursor,
    std::string_view message,
    ValidationStatus status
)
{
    std::string out;
    out += "\r\x1b[K";
    out.append(prompt);
    out += buf;

    out += "\r\n\x1b[K"; // status line (cleared even when empty)
    if (!message.empty()) {
        out += status == ValidationStatus::Error ? "\x1b[31m" : "\x1b[33m";
        out.append(message);
        if (status == ValidationStatus::Warning) {
            out.append(" (Press Enter again to accept)");
        }
        out += "\x1b[0m";
    }
    out += "\x1b[A\r"; // back up to the prompt line, column 0

    const std::size_t col = cp_count(prompt) + cp_count(std::string_view(buf).substr(0, cursor));
    if (col > 0)
        out += "\x1b[" + std::to_string(col) + "C";

    std::cout << out << std::flush;
}

// Freezes the current line and leaves the cursor on a clean line below it.
void finish_line(std::string_view prompt, const std::string& buf)
{
    std::cout << "\r\x1b[K" << prompt << buf << "\r\n\x1b[K" << std::flush;
}

// ------------------------------------------------------------ completion ----

std::size_t token_start(const std::string& buf, std::size_t cursor)
{
    std::size_t i = cursor;
    while (i > 0 && !std::isspace(static_cast<unsigned char>(buf[i - 1])))
        --i;
    return i;
}

std::vector<std::string> matches_for(const std::vector<std::string>& items, std::string_view token)
{
    std::vector<std::string> out;
    for (const auto& item : items)
        if (std::string_view(item).substr(0, token.size()) == token)
            out.push_back(item);
    return out;
}

std::string common_prefix(const std::vector<std::string>& v)
{
    if (v.empty())
        return {};
    std::string p = v.front();
    for (const auto& s : v) {
        std::size_t i = 0;
        while (i < p.size() && i < s.size() && p[i] == s[i])
            ++i;
        p.resize(i);
    }
    return p;
}

void list_candidates(const std::vector<std::string>& v)
{
    std::size_t width = 0;
    for (const auto& s : v)
        width = std::max(width, cp_count(s));
    width += 2;
    const std::size_t per_line = std::max<std::size_t>(1, 80 / width);

    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        out += v[i];
        out.append(width - cp_count(v[i]), ' ');
        if ((i + 1) % per_line == 0)
            out += "\r\n";
    }
    if (v.size() % per_line != 0)
        out += "\r\n";
    std::cout << out << std::flush;
}

// ---------------------------------------------------------- end-of-input ----

// The only path by which a value can leave get_input() without the user
// pressing Enter, so it still has to satisfy the validator. A Warning does not
// count as acceptance here: there is nobody left to confirm it.
std::string fallback_or_throw(
    std::string_view candidate,
    const std::function<Validation(std::string_view)>& validator,
    const char* reason
)
{
    if (!validator)
        return std::string(candidate);

    const Validation v = validator(candidate);
    if (v.status == ValidationStatus::OK)
        return std::string(candidate);

    throw InputUnavailableException(
        std::string(reason)
        + ", and the default value is not usable: "
        + (v.message.empty() ? "validation failed" : v.message)
    );
}

// ---------------------------------------------------------- non-tty path ----

std::string get_input_plain(
    std::string_view prompt,
    std::string_view initial_value,
    const std::function<Validation(std::string_view)>& validator
)
{
    std::string previous_warned;
    bool have_warning = false;

    for (;;) {
        // Show the default: without inline editing it is the only way the user
        // can see what pressing Enter will accept.
        std::cout << prompt;
        if (!initial_value.empty())
            std::cout << '[' << initial_value << "] ";
        std::cout << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            return fallback_or_throw(initial_value, validator, "no input available on stdin");
        }
        if (!line.empty() && line.back() == '\r') // CRLF arriving through a pipe
            line.pop_back();
        if (line.empty())
            line = std::string(initial_value);

        const Validation v = validator ? validator(line) : Validation{};
        if (v.status == ValidationStatus::OK)
            return line;
        if (v.status == ValidationStatus::Warning && have_warning && line == previous_warned)
            return line;

        std::cerr
            << (v.status == ValidationStatus::Error ? "error: " : "warning: ")
            << v.message
            << '\n';
        have_warning    = v.status == ValidationStatus::Warning;
        previous_warned = have_warning ? line : std::string{};
    }
}

} // namespace

namespace Slic3r::App::Console {
// ------------------------------------------------------------------ main ----

std::string get_input(
    std::string_view prompt,
    std::string_view initial_value,
    std::function<Validation(std::string_view)> validator,
    const std::vector<std::string>& completion_items
)
{
    if (!stdin_is_tty())
        return get_input_plain(prompt, initial_value, validator);

    RawMode raw;

    std::string buf(initial_value);
    std::size_t cursor = buf.size();

    std::string message;
    ValidationStatus message_status = ValidationStatus::OK;

    std::string warned_value; // text that produced the pending warning
    bool have_warning     = false;
    bool previous_was_tab = false;

    render(prompt, buf, cursor, message, message_status);

    for (;;) {
        const KeyEvent ev = read_key();
        const bool is_tab = ev.key == Key::Tab;

        switch (ev.key) {
        case Key::Char:
            buf.insert(cursor, ev.text);
            cursor += ev.text.size();
            break;

        case Key::Backspace:
            if (cursor > 0) {
                const std::size_t p = prev_cp(buf, cursor);
                buf.erase(p, cursor - p);
                cursor = p;
            }
            break;

        case Key::Delete:
            if (cursor < buf.size())
                buf.erase(cursor, next_cp(buf, cursor) - cursor);
            break;

        case Key::CtrlD:
            if (cursor < buf.size())
                buf.erase(cursor, next_cp(buf, cursor) - cursor);
            break;

        case Key::Left:
            cursor = prev_cp(buf, cursor);
            break;

        case Key::Right:
            cursor = next_cp(buf, cursor);
            break;

        case Key::Home:
            cursor = 0;
            break;

        case Key::End:
            cursor = buf.size();
            break;

        case Key::CtrlU:
            buf.erase(0, cursor);
            cursor = 0;
            break;

        case Key::CtrlK:
            buf.erase(cursor);
            break;

        case Key::CtrlW: {
            std::size_t i = cursor;
            while (i > 0 && std::isspace(static_cast<unsigned char>(buf[i - 1])))
                --i;
            while (i > 0 && !std::isspace(static_cast<unsigned char>(buf[i - 1])))
                --i;
            buf.erase(i, cursor - i);
            cursor = i;
            break;
        }

        case Key::Tab: {
            if (completion_items.empty())
                break;
            const std::size_t start = token_start(buf, cursor);
            const std::string_view token(buf.data() + start, cursor - start);
            const auto found = matches_for(completion_items, token);
            if (found.empty())
                break;

            const std::string prefix = common_prefix(found);
            if (prefix.size() > token.size()) {
                buf.replace(start, cursor - start, prefix);
                cursor = start + prefix.size();
            } else if (found.size() > 1 && previous_was_tab) {
                finish_line(prompt, buf); // keep the current line as history
                list_candidates(found); // ... and print the candidates below
            }
            break;
        }

        case Key::Enter: {
            const Validation v = validator ? validator(buf) : Validation{};

            if (v.status == ValidationStatus::OK
                || (v.status == ValidationStatus::Warning && have_warning && buf == warned_value))
            {
                render(prompt, buf, cursor, {}, ValidationStatus::OK);
                finish_line(prompt, buf);
                raw.restore();
                return buf;
            }

            message        = v.message;
            message_status = v.status;
            have_warning   = v.status == ValidationStatus::Warning;
            warned_value   = have_warning ? buf : std::string{};
            break;
        }

        case Key::CtrlC:
            finish_line(prompt, buf);
            raw.restore();
            std::raise(SIGINT);
            // Only reached if the program installed a SIGINT handler that returns.
            throw InputUnavailableException("input interrupted");

        case Key::Eof:
            finish_line(prompt, buf);
            raw.restore();
            return fallback_or_throw(buf, validator, "input stream closed");

        case Key::Up:
        case Key::Down:
        case Key::Escape:
        case Key::Unknown:
        default:
            break;
        }

        // Editing the text invalidates a pending warning/error message.
        if (ev.key != Key::Enter
            && ev.key != Key::Left
            && ev.key != Key::Right
            && ev.key != Key::Home
            && ev.key != Key::End
            && !message.empty())
        {
            message.clear();
            message_status = ValidationStatus::OK;
        }

        previous_was_tab = is_tab;
        render(prompt, buf, cursor, message, message_status);
    }
}
} // namespace Slic3r::App::Console