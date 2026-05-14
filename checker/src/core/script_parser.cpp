#include "checker/script.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace checker {
namespace {

std::string line_error(size_t line, const std::string& message) {
    return "checker: gen.script line " + std::to_string(line) + ": " + message;
}

std::vector<std::string> tokenize_line(std::string line) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) {
        line.resize(comment);
    }

    std::istringstream input(line);
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

int64_t parse_int(std::string_view text, size_t line, std::string_view context) {
    int64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::runtime_error(line_error(line, "invalid integer for " + std::string(context)));
    }
    return value;
}

ArgToken parse_arg_token(const std::string& token, size_t line) {
    if (token.size() >= 5 && token.front() == '[' && token.back() == ']') {
        std::string_view inner(token.data() + 1, token.size() - 2);
        const size_t dots = inner.find("..");
        if (dots == std::string_view::npos || inner.find("..", dots + 2) != std::string_view::npos) {
            throw std::runtime_error(line_error(line, "invalid range token: " + token));
        }

        int64_t lo = parse_int(inner.substr(0, dots), line, "range lower bound");
        int64_t hi = parse_int(inner.substr(dots + 2), line, "range upper bound");
        if (hi < lo) {
            throw std::runtime_error(line_error(line, "descending range token: " + token));
        }
        return RangeArg{lo, hi};
    }
    return token;
}

ScriptEntry parse_case_entry(const std::vector<std::string>& tokens, size_t line) {
    if (tokens.size() < 4) {
        throw std::runtime_error(line_error(line, "case requires name, generator, and args"));
    }

    ScriptEntry entry;
    entry.kind = ScriptEntry::Kind::Case;
    entry.name = tokens[1];
    entry.generator = tokens[2];
    entry.count = 1;
    for (size_t i = 3; i < tokens.size(); ++i) {
        entry.args.push_back(tokens[i]);
    }
    return entry;
}

ScriptEntry parse_stress_entry(const std::vector<std::string>& tokens, size_t line) {
    if (tokens.size() < 5) {
        throw std::runtime_error(line_error(line, "stress requires name, count, generator, and args"));
    }

    ScriptEntry entry;
    entry.kind = ScriptEntry::Kind::Stress;
    entry.name = tokens[1];
    entry.count = parse_int(tokens[2], line, "stress count");
    if (entry.count < 0) {
        throw std::runtime_error(line_error(line, "stress count must be non-negative"));
    }
    entry.generator = tokens[3];
    for (size_t i = 4; i < tokens.size(); ++i) {
        entry.args.push_back(parse_arg_token(tokens[i], line));
    }
    return entry;
}

} // namespace

Script parse_script(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("checker: failed to open script: " + path.string());
    }
    return parse_script_string(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

Script parse_script_string(std::string_view text) {
    Script script;
    std::set<std::string> case_names;
    std::set<std::string> stress_names;

    std::istringstream input{std::string(text)};
    std::string line_text;
    size_t line = 0;
    while (std::getline(input, line_text)) {
        ++line;
        std::vector<std::string> tokens = tokenize_line(line_text);
        if (tokens.empty()) {
            continue;
        }

        ScriptEntry entry;
        if (tokens[0] == "case") {
            entry = parse_case_entry(tokens, line);
            if (!case_names.insert(entry.name).second) {
                throw std::runtime_error(line_error(line, "duplicate case name: " + entry.name));
            }
        } else if (tokens[0] == "stress") {
            entry = parse_stress_entry(tokens, line);
            if (!stress_names.insert(entry.name).second) {
                throw std::runtime_error(line_error(line, "duplicate stress name: " + entry.name));
            }
        } else {
            throw std::runtime_error(line_error(line, "unknown entry kind: " + tokens[0]));
        }
        script.entries.push_back(std::move(entry));
    }

    return script;
}

std::vector<std::string> resolve_args(const ScriptEntry& entry, std::mt19937& rng) {
    std::vector<std::string> args;
    args.reserve(entry.args.size());

    for (const ArgToken& token : entry.args) {
        if (const auto* literal = std::get_if<std::string>(&token)) {
            args.push_back(*literal);
        } else {
            const auto& range = std::get<RangeArg>(token);
            std::uniform_int_distribution<int64_t> dist(range.lo, range.hi);
            args.push_back(std::to_string(dist(rng)));
        }
    }
    return args;
}

} // namespace checker
