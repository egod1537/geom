#ifndef CHECKER_STRESS_H
#define CHECKER_STRESS_H

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "checker/config.h"
#include "checker/detail/runtime.h"
#include "checker/result.h"
#include "checker/script.h"

namespace checker {
namespace detail {

inline std::atomic<bool>& interrupt_requested() {
    static std::atomic<bool> flag{false};
    return flag;
}

inline void handle_interrupt(int) {
    interrupt_requested().store(true);
}

class ScopedInterruptHandler {
public:
    ScopedInterruptHandler() {
        interrupt_requested().store(false);
        previous_ = std::signal(SIGINT, handle_interrupt);
    }

    ~ScopedInterruptHandler() {
        std::signal(SIGINT, previous_);
    }

    ScopedInterruptHandler(const ScopedInterruptHandler&) = delete;
    ScopedInterruptHandler& operator=(const ScopedInterruptHandler&) = delete;

private:
    void (*previous_)(int) = SIG_DFL;
};

inline void validate_common_config(bool has_resolver, bool has_parse, bool has_reference, bool has_target) {
    if (!has_resolver) {
        throw std::invalid_argument("checker: config.gen_resolver is required");
    }
    if (!has_parse) {
        throw std::invalid_argument("checker: config.parse is required");
    }
    if (!has_reference) {
        throw std::invalid_argument("checker: config.reference is required");
    }
    if (!has_target) {
        throw std::invalid_argument("checker: config.target is required");
    }
}

inline bool should_stop_after_fail(const StressResult& result, int max_fail, bool stop_on_first_fail) {
    if (stop_on_first_fail) {
        return true;
    }
    return max_fail > 0 && result.fail >= max_fail;
}

inline void log_progress(std::string_view label, int64_t index, const StressResult& result) {
    std::cerr << "checker: " << label
              << " index=" << index
              << " invocations=" << result.invocations
              << " ok=" << result.ok
              << " fail=" << result.fail << '\n';
}

template <typename Input, typename Output>
struct EvaluatedCase {
    Input input;
    Output expected;
    Output actual;
    bool matched;
};

template <typename Input, typename Output>
EvaluatedCase<Input, Output> evaluate_case_data(
    const StressConfig<Input, Output>& cfg,
    const CaseFileData& data) {
    Input input = cfg.parse(data.input);
    if (cfg.validate) {
        cfg.validate(input);
    }
    Output expected = data.expected_output
        ? [&]() {
              if (!cfg.parse_output) {
                  throw std::runtime_error(
                      "checker: config.parse_output is required for case files with [output]");
              }
              return cfg.normalize(cfg.parse_output(*data.expected_output));
          }()
        : cfg.normalize(cfg.reference(input));
    Output actual = cfg.normalize(cfg.target(input));
    bool matched = cfg.compare(actual, expected);
    return EvaluatedCase<Input, Output>{
        std::move(input),
        std::move(expected),
        std::move(actual),
        matched};
}

template <typename Input, typename Output>
bool evaluate_raw_case(const StressConfig<Input, Output>& cfg, std::string_view raw_bytes) {
    CaseFileData data;
    data.input = std::string(raw_bytes);
    return evaluate_case_data<Input, Output>(cfg, data).matched;
}

template <typename Input, typename Output>
void validate_config(const StressConfig<Input, Output>& cfg) {
    validate_common_config(
        static_cast<bool>(cfg.gen_resolver),
        static_cast<bool>(cfg.parse),
        static_cast<bool>(cfg.reference),
        static_cast<bool>(cfg.target));
    if (cfg.max_fail < 0) {
        throw std::invalid_argument("checker: max_fail must be non-negative");
    }
    if (cfg.subprocess_timeout_sec <= 0) {
        throw std::invalid_argument("checker: subprocess_timeout_sec must be positive");
    }
}

inline const char* resolve_generator(
    const std::function<const char*(std::string_view)>& resolver,
    std::string_view name) {
    const char* path = resolver(name);
    if (path == nullptr || path[0] == '\0') {
        throw std::runtime_error("checker: unknown generator: " + std::string(name));
    }
    return path;
}

} // namespace detail

template <typename Input, typename Output>
StressResult run_one_stress(const StressConfig<Input, Output>& cfg, std::string_view name) {
    detail::validate_config(cfg);
    Script script = parse_script(cfg.script_path);

    const ScriptEntry* selected = nullptr;
    for (const ScriptEntry& entry : script.entries) {
        if (entry.kind == ScriptEntry::Kind::Stress && entry.name == name) {
            selected = &entry;
            break;
        }
    }
    if (selected == nullptr) {
        throw std::runtime_error("checker: stress entry not found: " + std::string(name));
    }

    StressResult result;
    const auto started = std::chrono::steady_clock::now();
    detail::ScopedInterruptHandler interrupt_handler;
    std::random_device device;
    std::mt19937 rng(device());

    const char* generator_path = detail::resolve_generator(cfg.gen_resolver, selected->generator);
    for (int64_t i = 0; i < selected->count; ++i) {
        std::vector<std::string> args = resolve_args(*selected, rng);
        std::string raw = detail::run_generator(generator_path, args, cfg.subprocess_timeout_sec);

        ++result.invocations;
        if (detail::evaluate_raw_case(cfg, raw)) {
            ++result.ok;
        } else {
            ++result.fail;
            result.archived_cases.push_back(detail::archive_failed(cfg.failed_dir, selected->name, i, raw));
            if (detail::should_stop_after_fail(result, cfg.max_fail, cfg.stop_on_first_fail)) {
                break;
            }
        }

        if (cfg.progress_every > 0 && (i + 1) % cfg.progress_every == 0) {
            detail::log_progress(selected->name, i + 1, result);
        }
        if (detail::interrupt_requested().load()) {
            detail::log_progress("interrupted", i + 1, result);
            break;
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    result.elapsed_sec = std::chrono::duration<double>(finished - started).count();
    return result;
}

template <typename Input, typename Output>
StressResult run_all_stress(const StressConfig<Input, Output>& cfg) {
    detail::validate_config(cfg);
    Script script = parse_script(cfg.script_path);

    StressResult total;
    const auto started = std::chrono::steady_clock::now();
    detail::ScopedInterruptHandler interrupt_handler;
    std::random_device device;
    std::mt19937 rng(device());

    for (const ScriptEntry& entry : script.entries) {
        if (entry.kind != ScriptEntry::Kind::Stress) {
            continue;
        }

        const char* generator_path = detail::resolve_generator(cfg.gen_resolver, entry.generator);
        for (int64_t i = 0; i < entry.count; ++i) {
            std::vector<std::string> args = resolve_args(entry, rng);
            std::string raw = detail::run_generator(generator_path, args, cfg.subprocess_timeout_sec);

            ++total.invocations;
            if (detail::evaluate_raw_case(cfg, raw)) {
                ++total.ok;
            } else {
                ++total.fail;
                total.archived_cases.push_back(detail::archive_failed(cfg.failed_dir, entry.name, i, raw));
                if (detail::should_stop_after_fail(total, cfg.max_fail, cfg.stop_on_first_fail)) {
                    break;
                }
            }

            if (cfg.progress_every > 0 && total.invocations % cfg.progress_every == 0) {
                detail::log_progress(entry.name, i + 1, total);
            }
            if (detail::interrupt_requested().load()) {
                detail::log_progress("interrupted", i + 1, total);
                break;
            }
        }

        if (detail::interrupt_requested().load()) {
            break;
        }
        if (detail::should_stop_after_fail(total, cfg.max_fail, cfg.stop_on_first_fail) && total.fail > 0) {
            break;
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    total.elapsed_sec = std::chrono::duration<double>(finished - started).count();
    return total;
}

template <typename Input, typename Output>
void build_cases(const StressConfig<Input, Output>& cfg) {
    if (!cfg.gen_resolver) {
        throw std::invalid_argument("checker: config.gen_resolver is required");
    }

    Script script = parse_script(cfg.script_path);
    for (const ScriptEntry& entry : script.entries) {
        if (entry.kind != ScriptEntry::Kind::Case) {
            continue;
        }
        const char* generator_path = detail::resolve_generator(cfg.gen_resolver, entry.generator);
        std::mt19937 rng(0);
        std::vector<std::string> args = resolve_args(entry, rng);
        std::string raw = detail::run_generator(generator_path, args, cfg.subprocess_timeout_sec);
        detail::write_case(cfg.cases_dir, entry.name, raw);
    }
}

template <typename Input, typename Output>
StressResult run_cases(const StressConfig<Input, Output>& cfg) {
    detail::validate_config(cfg);

    StressResult result;
    const auto started = std::chrono::steady_clock::now();
    const std::vector<std::filesystem::path> cases = detail::glob_cases(cfg.cases_dir);

    for (const std::filesystem::path& path : cases) {
        detail::CaseFileData data = detail::parse_case_file(detail::read_file(path));
        ++result.invocations;
        if (detail::evaluate_case_data<Input, Output>(cfg, data).matched) {
            ++result.ok;
        } else {
            ++result.fail;
            if (detail::should_stop_after_fail(result, cfg.max_fail, cfg.stop_on_first_fail)) {
                break;
            }
        }

        if (cfg.progress_every > 0 && result.invocations % cfg.progress_every == 0) {
            detail::log_progress("cases", result.invocations, result);
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    result.elapsed_sec = std::chrono::duration<double>(finished - started).count();
    return result;
}

} // namespace checker

#endif // CHECKER_STRESS_H
