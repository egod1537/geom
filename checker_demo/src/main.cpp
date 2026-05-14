#include <checker/dispatch.h>
#include <checker/gui.h>

#include "demo_points.hpp"
#include "gen_paths.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using demo_points::Input;
using demo_points::Output;

Input parse_input(std::string_view raw) {
    std::istringstream in{std::string(raw)};
    int n = 0;
    Input input;
    if (!(in >> n) || n < 0) {
        throw std::runtime_error("bad N");
    }
    input.values.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        int value = 0;
        if (!(in >> value)) {
            throw std::runtime_error("missing value");
        }
        input.values.push_back(value);
    }
    std::string extra;
    if (in >> extra) {
        throw std::runtime_error("too many input values");
    }
    return input;
}

Output reference(const Input& input) {
    int64_t sum = 0;
    for (int value : input.values) {
        sum += value;
    }
    return sum;
}

Output parse_output(std::string_view raw) {
    std::istringstream in{std::string(raw)};
    Output output = 0;
    if (!(in >> output)) {
        throw std::runtime_error("missing output sum");
    }
    std::string extra;
    if (in >> extra) {
        throw std::runtime_error("too many output values");
    }
    return output;
}

Output target(const Input& input) {
    if (std::getenv("CHECKER_DEMO_FORCE_FAIL")) {
        return {};
    }
    return demo_points::solve(input);
}

std::string format_sum(const Output& output) {
    std::ostringstream out;
    out << output << '\n';
    return out.str();
}

void render_plot(const Input& input, const Output& ref, const Output& got, bool matched) {
    ImGui::Text("n=%zu  answer=%lld  output=%lld  %s",
        input.values.size(),
        static_cast<long long>(ref),
        static_cast<long long>(got),
        matched ? "MATCH" : "DIFF");
}

checker::StressConfig<Input, Output> make_config() {
    checker::StressConfig<Input, Output> cfg;
    cfg.script_path = checker::generated::script_path;
    cfg.cases_dir = checker::generated::cases_dir;
    cfg.failed_dir = std::filesystem::path(checker::generated::cases_dir) / "failed";
    cfg.gen_resolver = checker::generated::gen_path;
    cfg.gen_source_resolver = checker::generated::gen_source_path;
    cfg.parse = parse_input;
    cfg.parse_output = parse_output;
    cfg.reference = reference;
    cfg.target = target;
    cfg.gui.format_output = format_sum;
    cfg.gui.render_plot_2d = render_plot;
    cfg.gui.window_title = "checker demo";
    cfg.max_fail = 3;
    cfg.stop_on_first_fail = false;
    cfg.progress_every = 25;
    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    return checker::run(make_config(), argc, argv);
}
