#include "Slic3r/App/CustomGCodeMigration.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r::App {

static void find_replace(std::string& str, const std::string& from, const std::string& to)
{
    std::size_t pos = 0;

    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

const std::vector<std::pair<std::string, std::string>>& get_renames(){
    static std::vector<std::pair<std::string, std::string>> renames{
        {"filament_retract_length", "retract_length"},
        {"filament_retract_lift", "retract_lift"},
        {"filament_retract_lift_above", "retract_lift_above"},
        {"filament_retract_lift_below", "retract_lift_below"},
        {"filament_retract_speed", "retract_speed"},
        {"filament_deretract_speed", "deretract_speed"},
        {"filament_retract_restart_extra", "retract_restart_extra"},
        {"filament_retract_before_travel", "retract_before_travel"},
        {"filament_retract_length_toolchange", "retract_length_toolchange"},
        {"filament_retract_restart_extra_toolchange", "retract_restart_extra_toolchange"},
        {"filament_retract_layer_change", "retract_layer_change"},
        {"filament_retract_before_wipe", "retract_before_wipe"},
        {"filament_wipe", "wipe"},
        {"filament_travel_max_lift", "travel_max_lift"},
        {"filament_travel_lift_before_obstacle", "travel_lift_before_obstacle"},
        {"filament_travel_ramping_lift", "travel_ramping_lift"},
        {"filament_travel_slope", "travel_slope"},
        {"filament_seam_gap_distance", "seam_gap_distance"},
        {"first_layer_infill_speed", "first_layer_solid_infill_speed"},
    };
    return renames;
}

static void find_replace_renames(std::string& gcode)
{
    for (const auto& [from, to] : get_renames()) {
        find_replace(gcode, from, to);
    }
}

struct Variable
{
    std::string name;
    std::string subscript;
};

enum class VariableScope {
    Local,
    Global
};

struct VariableDefinition {
    VariableScope scope;
    std::string name;
    std::vector<std::variant<std::string, Variable>> expression;
    bool vectorize{false};
};

using ExpressionSegment = std::variant<std::string, Variable>;
using CodeSegment = std::variant<std::string, Variable, VariableDefinition>;

struct CodeBlock
{
    std::optional<int> line_number;
    std::vector<CodeSegment> parsed_code;
};

using ParsedChunk = std::variant<std::string, CodeBlock>;

static int line_number_at(const std::string& s, size_t pos)
{
    return static_cast<int>(std::count(s.begin(), s.begin() + pos, '\n')) + 1;
}

static bool is_word_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool code_text_ends_with_regex_op(const std::string& s)
{
    int end{static_cast<int>(s.size())};
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return end > 0 && s[end - 1] == '~';
}

static std::string try_consume_subscript(const std::string& gcode, size_t& i)
{
    if (i >= gcode.size() || gcode[i] != '[') {
        return "";
    }

    int depth{0};
    const size_t start{i};
    for (; i < gcode.size(); ++i) {
        if (gcode[i] == '[') {
            ++depth;
            continue;
        }
        if (gcode[i] != ']') {
            continue;
        }
        --depth;
        if (depth == 0) {
            ++i;
            break;
        }
    }

    if (depth != 0) {
        return "";
    }
    return gcode.substr(start, i - start);
}

template <typename SegmentVariant>
static void flush_word(
    const std::string& word,
    const std::set<std::string>& vector_vars,
    const std::string& gcode,
    size_t& i,
    std::string& code_text,
    std::vector<SegmentVariant>& segments)
{
    if (word.empty()) {
        return;
    }

    if (!vector_vars.count(word)) {
        code_text += word;
        return;
    }

    if (!code_text.empty()) {
        segments.push_back(code_text);
        code_text.clear();
    }
    const std::string subscript{try_consume_subscript(gcode, i)};
    segments.push_back(Variable{word, subscript});
}

static std::vector<ExpressionSegment>
parse_expression(const std::string& gcode, const std::set<std::string>& vector_vars, size_t& i)
{
    std::vector<ExpressionSegment> segments;
    std::string code_text;
    std::string word;

    bool is_literal{false};
    bool is_regex{false};

    while (i < gcode.size()) {
        const char c{gcode[i]};

        if (is_literal) {
            code_text += c;
            if (c == '\\' && i + 1 < gcode.size()) {
                code_text += gcode[i + 1];
                i += 2;
            } else if (c == '"') {
                is_literal = false;
                ++i;
            } else {
                ++i;
            }
            continue;
        }

        if (is_regex) {
            code_text += c;
            if (c == '\\' && i + 1 < gcode.size()) {
                code_text += gcode[i + 1];
                i += 2;
            } else if (c == '/') {
                is_regex = false;
                ++i;
            } else {
                ++i;
            }
            continue;
        }

        if (is_word_char(c)) {
            word += c;
            ++i;
            continue;
        }

        const bool had_word{!word.empty()};
        const bool was_vector_var{had_word && vector_vars.count(word) != 0};
        flush_word(word, vector_vars, gcode, i, code_text, segments);
        word.clear();

        if (was_vector_var && !std::get<Variable>(segments.back()).subscript.empty()) {
            continue;
        }

        if (c == ';' || c == '}') {
            if (!code_text.empty()) {
                segments.push_back(code_text);
                code_text.clear();
            }
            break;
        }

        if (c == '"') {
            is_literal = true;
            code_text += c;
            ++i;
            continue;
        }

        if (c == '/' && code_text_ends_with_regex_op(code_text)) {
            is_regex = true;
            code_text += c;
            ++i;
            continue;
        }

        code_text += c;
        ++i;
    }

    if (!word.empty()) {
        flush_word(word, vector_vars, gcode, i, code_text, segments);
    }
    if (!code_text.empty()) {
        segments.push_back(code_text);
    }

    return segments;
}

static std::optional<VariableDefinition> try_parse_variable_definition(
    VariableScope scope,
    const std::string& gcode,
    const std::set<std::string>& vector_vars,
    size_t& i)
{
    size_t cursor{i};
    while (cursor < gcode.size() && std::isspace(static_cast<unsigned char>(gcode[cursor]))) {
        ++cursor;
    }

    if (cursor >= gcode.size() || !is_word_char(gcode[cursor])) {
        return std::nullopt;
    }

    const size_t name_start{cursor};
    while (cursor < gcode.size() && is_word_char(gcode[cursor])) {
        ++cursor;
    }
    const std::string name{gcode.substr(name_start, cursor - name_start)};

    while (cursor < gcode.size() && std::isspace(static_cast<unsigned char>(gcode[cursor]))) {
        ++cursor;
    }

    if (cursor >= gcode.size() || gcode[cursor] != '=') {
        return std::nullopt;
    }

    i = cursor + 1;
    std::vector<ExpressionSegment> expression{parse_expression(gcode, vector_vars, i)};
    return VariableDefinition{scope, name, std::move(expression)};
}

static std::vector<ParsedChunk>
parse_custom_gcode(const std::string& gcode, const std::set<std::string>& vector_vars)
{
    std::vector<ParsedChunk> chunks;

    bool is_code{false};
    bool is_literal{false};
    bool is_regex{false};

    std::string text;
    std::string code_text;
    std::string word;
    std::vector<CodeSegment> segments;

    bool is_statement_start{true};

    int block_start_line{0};

    size_t i{0};
    while (i < gcode.size()) {
        const char c{gcode[i]};

        if (!is_code) {
            if (c == '{') {
                if (!text.empty()) {
                    chunks.push_back(text);
                    text.clear();
                }
                is_code    = true;
                code_text.clear();
                word.clear();
                segments.clear();
                block_start_line = line_number_at(gcode, i);
                is_statement_start = true;
                ++i;
            } else {
                text += c;
                ++i;
            }
            continue;
        }

        if (is_literal) {
            code_text += c;
            if (c == '\\' && i + 1 < gcode.size()) {
                code_text += gcode[i + 1];
                i += 2;
            } else if (c == '"') {
                is_literal = false;
                ++i;
            } else {
                ++i;
            }
            continue;
        }

        if (is_regex) {
            code_text += c;
            if (c == '\\' && i + 1 < gcode.size()) {
                code_text += gcode[i + 1];
                i += 2;
            } else if (c == '/') {
                is_regex = false;
                ++i;
            } else {
                ++i;
            }
            continue;
        }

        if (is_word_char(c)) {
            word += c;
            ++i;
            continue;
        }

        const bool had_word{!word.empty()};
        const bool was_vector_var{had_word && vector_vars.count(word) != 0};

        if (had_word && is_statement_start && (word == "local" || word == "global")) {
            if (!code_text.empty()) {
                segments.push_back(code_text);
                code_text.clear();
            }

            const VariableScope scope{word == "local" ? VariableScope::Local : VariableScope::Global};
            std::optional<VariableDefinition> definition{
                try_parse_variable_definition(scope, gcode, vector_vars, i)};
            if (definition) {
                segments.push_back(std::move(*definition));
                word.clear();
                is_statement_start = false;
                continue;
            }
        }

        flush_word(word, vector_vars, gcode, i, code_text, segments);
        word.clear();

        if (had_word) {
            is_statement_start = false;
        }

        if (was_vector_var && !std::get<Variable>(segments.back()).subscript.empty()) {
            continue;
        }

        if (c == '}') {
            if (!code_text.empty()) {
                segments.push_back(code_text);
                code_text.clear();
            }
            const int block_end_line{line_number_at(gcode, i)};
            std::optional<int> line_number;
            if (block_start_line == block_end_line) {
                line_number = block_start_line;
            }
            chunks.push_back(CodeBlock{line_number, std::move(segments)});
            is_code = false;
            is_statement_start = true;
            ++i;
            continue;
        }

        if (c == '"') {
            is_literal = true;
            code_text += c;
            is_statement_start = false;
            ++i;
            continue;
        }

        if (c == '/' && code_text_ends_with_regex_op(code_text)) {
            is_regex = true;
            code_text += c;
            is_statement_start = false;
            ++i;
            continue;
        }

        code_text += c;
        if (c == ';') {
            is_statement_start = true;
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            is_statement_start = false;
        }
        ++i;
    }

    if (!text.empty()) {
        chunks.push_back(text);
    }

    return chunks;
}

static std::string
serialize_expression(const std::vector<std::variant<std::string, Variable>>& expression)
{
    std::string result;
    for (const auto& expr_seg : expression) {
        const auto* expr_text{std::get_if<std::string>(&expr_seg)};
        if (expr_text) {
            result += *expr_text;
            continue;
        }
        const auto& expr_var{std::get<Variable>(expr_seg)};
        result += expr_var.name;
        result += expr_var.subscript;
    }

    return result;
}

static void
add_missing_subscripts(std::vector<std::variant<std::string, Variable>>& expression, int i)
{
    for (auto& seg : expression) {
        auto* variable{std::get_if<Variable>(&seg)};
        if (!variable) {
            continue;
        }
        if (variable->subscript.empty()) {
            variable->subscript = "[" + std::to_string(i) + "]";
        }
    }
}

static void trim(std::string& s)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

static std::string serialize(const std::vector<ParsedChunk>& chunks, std::size_t material_slot_count)
{
    std::string result;

    for (const ParsedChunk& chunk : chunks) {
        const auto* text{std::get_if<std::string>(&chunk)};
        if (text) {
            result += *text;
            continue;
        }

        const auto& block{std::get<CodeBlock>(chunk)};
        result += '{';
        for (const auto& seg : block.parsed_code) {
            const auto* seg_text{std::get_if<std::string>(&seg)};
            if (seg_text) {
                result += *seg_text;
                continue;
            }
            const auto* var{std::get_if<Variable>(&seg)};
            if (var) {
                result += var->name;
                result += var->subscript;
                continue;
            }

            const auto& def{std::get<VariableDefinition>(seg)};
            result += def.scope == VariableScope::Local ? "local " : "global ";
            result += def.name;
            result += " =";
            if (def.vectorize && material_slot_count > 0) {
                result += " (";
                for (int i{}; i < material_slot_count; ++i) {
                    result += i == 0 ? "" : ",";
                    result += "\n";
                    auto expression{def.expression};
                    add_missing_subscripts(expression, i);
                    std::string serialized_expression{serialize_expression(expression)};
                    trim(serialized_expression);
                    result += "  " + serialized_expression;
                }
                result += "\n);";
            } else {
                result += serialize_expression(def.expression);
            }
        }
        result += '}';
    }

    return result;
}

static std::vector<Variable*>
get_variables(CodeBlock& block, std::function<bool(const Variable&)> predicate)
{
    std::vector<Variable*> result;
    for (auto& seg : block.parsed_code) {
        auto* var{std::get_if<Variable>(&seg)};
        if (var) {
            if (!predicate(*var)) {
                continue;
            }
            result.push_back(var);
            continue;
        }

        auto* def{std::get_if<VariableDefinition>(&seg)};
        if (!def) {
            continue;
        }
        for (auto& expr_seg : def->expression) {
            auto* expr_var{std::get_if<Variable>(&expr_seg)};
            if (!expr_var) {
                continue;
            }
            if (!predicate(*expr_var)) {
                continue;
            }
            result.push_back(expr_var);
        }
    }
    return result;
}

static bool should_be_vectorized(const VariableDefinition& definition) {
    std::size_t count{0};
    for (const auto& seg : definition.expression) {
        const auto* variable{std::get_if<Variable>(&seg)};
        if (!variable) {
            continue;
        }
        count++;
        if (!variable->subscript.empty()) {
            return false;
        }
    }
    return count > 0;
}

static std::set<std::string> get_definitions_to_vectorize(const std::vector<ParsedChunk>& chunks)
{
    std::set<std::string> result;
    for (const ParsedChunk& chunk : chunks) {
        auto* block{std::get_if<CodeBlock>(&chunk)};
        if (!block) {
            continue;
        }
        for (auto& seg : block->parsed_code) {
            auto* definition{std::get_if<VariableDefinition>(&seg)};
            if (!definition) {
                continue;
            }
            if (!should_be_vectorized(*definition)) {
                continue;
            }
            result.insert(definition->name);
        }
    }
    return result;
}

static void mark_definitions_to_vectorize(
    std::vector<ParsedChunk>& chunks,
    const std::set<std::string>& to_vectorize)
{
    for (ParsedChunk& chunk : chunks) {
        auto* block{std::get_if<CodeBlock>(&chunk)};
        if (!block) {
            continue;
        }
        for (auto& seg : block->parsed_code) {
            auto* definition{std::get_if<VariableDefinition>(&seg)};
            if (!definition) {
                continue;
            }
            if (to_vectorize.contains(definition->name)) {
                definition->vectorize = true;
            }
        }
    }
}

static std::vector<CodeBlock*>
get_blocks(std::vector<ParsedChunk>& chunks, std::function<bool(const CodeBlock&)> predicate)
{
    std::vector<CodeBlock*> result;
    for (auto& chunk : chunks) {
        auto* block{std::get_if<CodeBlock>(&chunk)};
        if (!block) {
            continue;
        }
        if (!predicate(*block)) {
            continue;
        }
        result.push_back(block);
    }
    return result;
}

static std::string guess_subscript(CodeBlock& block, std::vector<ParsedChunk>& chunks)
{
    const auto resolved_predicte{[](const Variable& var) { return !var.subscript.empty(); }};

    std::vector<Variable*> resolved_variables{get_variables(block, resolved_predicte)};

    if (resolved_variables.empty()) {
        if (!block.line_number) {
            return "";
        }
        const std::vector<CodeBlock*> code_blocks{get_blocks(
            chunks,
            [&](const CodeBlock& other_block)
            { return other_block.line_number == block.line_number; })};

        for (CodeBlock* other_block : code_blocks) {
            const std::vector<Variable*> other_variables{
                get_variables(*other_block, resolved_predicte)};
            resolved_variables
                .insert(resolved_variables.end(), other_variables.begin(), other_variables.end());
        }
    }

    if (resolved_variables.empty()) {
        return "";
    }

    const std::string& result_candidate{resolved_variables.front()->subscript};

    const bool all_same{std::ranges::all_of(
        resolved_variables,
        [&](const Variable* variable) { return variable->subscript == result_candidate; })};

    return all_same ? result_candidate : "";
}

static void add_missing_subscripts(std::vector<ParsedChunk>& chunks)
{
    for (ParsedChunk& chunk : chunks) {
        auto* block{std::get_if<CodeBlock>(&chunk)};
        if (!block) {
            continue;
        }

        const std::vector<Variable*> unresolved_variables{
            get_variables(*block, [](const Variable& var) { return var.subscript.empty(); })};

        if (unresolved_variables.empty()) {
            continue;
        }

        std::string subscript{guess_subscript(*block, chunks)};

        for (Variable* variable : unresolved_variables) {
            variable->subscript = subscript;
        }
    }
}

static bool is_vector(const Domain::ConfigItemDef& def)
{
    if (def.compatibility_rule != Domain::CompatibilityRule::Undefined) {
        return false;
    }
    if (def.location == Domain::ConfigLocation{Domain::FDMConfigLocation::Filament}
        || def.location == Domain::ConfigLocation{Domain::FDMConfigLocation::Tool})
    {
        return true;
    }
    if (def.overrides_in.contains(Domain::FDMConfigLocation::Filament)
        || def.overrides_in.contains(Domain::FDMConfigLocation::Tool))
    {
        return true;
    }
    return false;
}

static std::set<std::string> get_vector_variables()
{
    std::set<std::string> result;
    for (const Domain::ConfigItemDef& def : Domain::get_defs_fdm().defs()) {
        if (!is_vector(def)) {
            continue;
        }
        result.insert(def.name);
    }
    return result;
}

static std::optional<std::size_t> try_parse_extruder_number(const std::string& subscript)
{
    if (subscript.size() != 3) {
        return std::nullopt;
    }
    if (subscript[1] >= '0' && subscript[1] <= '7') {
        return subscript[1] - '0';
    }
    return std::nullopt;
}

static std::size_t deduce_slot_count(std::vector<ParsedChunk>& chunks) {
    std::size_t result{0};
    for (auto& chunk : chunks) {
        auto* block{std::get_if<CodeBlock>(&chunk)};
        if (!block) {
            continue;
        }
        std::vector<Variable*> variables{
            get_variables(*block, [](const Variable& var) { return !var.subscript.empty(); })};

        for (const Variable* variable : variables) {
            std::optional<std::size_t> slot{try_parse_extruder_number(variable->subscript)};
            if (slot && *slot > result) {
                result = *slot;
            }
        }
    }
    return result + 1;
}

std::string migrate_custom_gcode(const std::string& custom_gcode)
{
    std::string to_parse{custom_gcode};
    find_replace_renames(to_parse);

    std::set<std::string> vector_variables{get_vector_variables()};
    std::vector<ParsedChunk> chunks{parse_custom_gcode(to_parse, vector_variables)};

    const std::set<std::string> definitions_to_vectorize{get_definitions_to_vectorize(chunks)};

    vector_variables.insert(definitions_to_vectorize.begin(), definitions_to_vectorize.end());
    chunks = parse_custom_gcode(to_parse, vector_variables);

    mark_definitions_to_vectorize(chunks, definitions_to_vectorize);

    const std::size_t material_slot_count{deduce_slot_count(chunks)};

    add_missing_subscripts(chunks);
    return serialize(chunks, material_slot_count);
}

} // namespace Slic3r::App
