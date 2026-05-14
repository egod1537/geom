#ifndef CHECKER_CONFIG_H
#define CHECKER_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace checker {

template <typename Input, typename Output>
struct GuiConfig {
    std::function<std::string(const Output&)> format_output;

    std::function<void(
        const Input& input,
        const Output& reference,
        const Output& target,
        bool matched)> render_plot_2d;

    std::string window_title = "checker";
    int window_w = 1280;
    int window_h = 800;
};

template <typename Input, typename Output>
struct StressConfig {
    std::filesystem::path script_path;
    std::filesystem::path cases_dir;
    std::filesystem::path failed_dir;

    std::function<const char*(std::string_view)> gen_resolver;
    std::function<const char*(std::string_view)> gen_source_resolver;

    std::function<Input(std::string_view raw_bytes)> parse;
    std::function<Output(std::string_view raw_bytes)> parse_output;
    std::function<Output(const Input&)> reference;
    std::function<Output(const Input&)> target;

    std::function<Output(Output)> normalize = [](Output output) { return output; };
    std::function<bool(const Output&, const Output&)> compare =
        [](const Output& a, const Output& b) { return a == b; };
    std::function<void(const Input&)> validate;

    GuiConfig<Input, Output> gui;

    int max_fail = 1;
    bool stop_on_first_fail = true;
    int progress_every = 50;
    int subprocess_timeout_sec = 10;
};

} // namespace checker

#endif // CHECKER_CONFIG_H
