#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::App::Console {

enum class ValidationStatus
{
    OK,
    Error,
    Warning
};

struct Validation
{
    ValidationStatus status{ValidationStatus::OK};
    std::string message;
};

// Thrown when stdin cannot supply a value (closed, EOF, a terminal that
// delivers no input) and `initial_value` does not validate as OK. Exists so
// that no unvalidated value can ever be returned.
struct InputUnavailableException : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Reads a line from the console with inline editing.
//
// * `prompt`           printed in front of the editable text (plain text, no ANSI)
// * `initial_value`    pre-filled into the edit buffer, cursor at the end
// * `validator`        run on Enter; may be empty (everything accepted)
// * `completion_items` candidates offered on Tab
//
// Editing keys: Left/Right, Home/End (Ctrl-A/Ctrl-E), Backspace, Delete,
// Ctrl-U (kill to start), Ctrl-K (kill to end), Ctrl-W (kill word), Tab, Enter.
// Ctrl-C restores the terminal and re-raises SIGINT.
//
// Validation semantics:
// * OK      -> the value is returned.
// * Error   -> message is shown in red, editing continues; never accepted.
// * Warning -> message is shown in yellow, editing continues, but pressing
// Enter again on the *same* text accepts it.
//
// If stdin is not a TTY the function falls back to std::getline with the same
// validation semantics (a repeated identical line confirms a warning), echoing
// the default as `prompt [initial_value] ` and taking an empty line to mean
// "keep the default".
//
// Every returned value has passed `validator`. If input ends (EOF, Ctrl-D, a
// terminal that yields nothing) the only value that may be returned is the
// current text, and only if it validates as OK; otherwise InputUnavailableException is
// thrown. A pending Warning is never auto-confirmed, because there is nobody
// left to confirm it.
std::string get_input(
    std::string_view prompt,
    std::string_view initial_value,
    std::function<Validation(std::string_view)> validator,
    const std::vector<std::string>& completion_items
);
} // namespace Slic3r::App::Console