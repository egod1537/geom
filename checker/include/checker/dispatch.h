#ifndef CHECKER_DISPATCH_H
#define CHECKER_DISPATCH_H

#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "checker/config.h"
#include "checker/gui.h"
#include "checker/stress.h"

namespace checker {
namespace detail {

inline void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--headless] [stress [name] | build | cases | --help]\n"
        << "  (no command)  open the GUI on the Invocations tab\n"
        << "  stress        open the GUI on the Invocations tab, or run all stress entries with --headless\n"
        << "  stress <name> open/run one stress entry\n"
        << "  build         open/run scripted case generation\n"
        << "  cases         open/run saved cases\n"
        << "  --headless    skip GLFW/ImGui and use text-mode execution\n";
}

inline void print_summary(std::string_view label, const StressResult& result) {
    std::cerr << "checker: " << label
              << " invocations=" << result.invocations
              << " ok=" << result.ok
              << " fail=" << result.fail
              << " elapsed_sec=" << result.elapsed_sec << '\n';
}

} // namespace detail

template <typename Input, typename Output>
int run(const StressConfig<Input, Output>& cfg, int argc, char** argv) {
    const char* program = argc > 0 ? argv[0] : "stress_test";
    try {
        bool headless = false;
        std::vector<std::string_view> args;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "--headless") {
                headless = true;
            } else {
                args.push_back(arg);
            }
        }

        if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
            detail::print_usage(program);
            return 0;
        }

        if (args.empty()) {
            if (!headless) {
                return gui::run_window(cfg, gui::LaunchOptions{gui::StartTab::Stress, "", false});
            }
            StressResult result = run_all_stress(cfg);
            detail::print_summary("stress", result);
            return result.exit_code();
        }

        std::string_view command(args[0]);
        if (command == "--help" || command == "-h") {
            detail::print_usage(program);
            return 0;
        }

        if (command == "stress") {
            if (args.size() > 2) {
                detail::print_usage(program);
                return 2;
            }
            if (!headless) {
                gui::LaunchOptions options;
                options.tab = gui::StartTab::Stress;
                options.auto_start = !args.empty();
                if (args.size() == 2) {
                    options.stress_name = std::string(args[1]);
                }
                return gui::run_window(cfg, options);
            }
            StressResult result = args.size() == 2
                ? run_one_stress(cfg, args[1])
                : run_all_stress(cfg);
            detail::print_summary(args.size() == 2 ? args[1] : "stress", result);
            return result.exit_code();
        }

        if (command == "build" && args.size() == 1) {
            if (!headless) {
                return gui::run_window(cfg, gui::LaunchOptions{gui::StartTab::Build, "", true});
            }
            build_cases(cfg);
            std::cerr << "checker: build complete\n";
            return 0;
        }

        if (command == "cases" && args.size() == 1) {
            if (!headless) {
                return gui::run_window(cfg, gui::LaunchOptions{gui::StartTab::Cases, "", true});
            }
            StressResult result = run_cases(cfg);
            detail::print_summary("cases", result);
            return result.exit_code();
        }

        detail::print_usage(program);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "checker: " << ex.what() << '\n';
        return 2;
    }
}

} // namespace checker

#endif // CHECKER_DISPATCH_H
