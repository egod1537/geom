#ifndef CHECKER_SCRIPT_H
#define CHECKER_SCRIPT_H

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace checker {

struct RangeArg {
    int64_t lo = 0;
    int64_t hi = 0;
};

using ArgToken = std::variant<std::string, RangeArg>;

struct ScriptEntry {
    enum class Kind {
        Case,
        Stress,
    };

    Kind kind = Kind::Case;
    std::string name;
    std::string generator;
    std::vector<ArgToken> args;
    int64_t count = 1;
};

struct Script {
    std::vector<ScriptEntry> entries;
};

Script parse_script(const std::filesystem::path& path);
Script parse_script_string(std::string_view text);
std::vector<std::string> resolve_args(const ScriptEntry& entry, std::mt19937& rng);

} // namespace checker

#endif // CHECKER_SCRIPT_H
