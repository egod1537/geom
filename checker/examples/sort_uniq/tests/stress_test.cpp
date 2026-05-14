#include <checker/dispatch.h>
#include <checker/gui.h>

#include "gen_paths.h"
#include "sort_uniq.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<int> parse_input(std::string_view raw) {
    std::istringstream input{std::string(raw)};
    int n = 0;
    if (!(input >> n)) {
        throw std::runtime_error("missing n");
    }
    if (n < 0) {
        throw std::runtime_error("negative n");
    }

    std::vector<int> values;
    values.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        int value = 0;
        if (!(input >> value)) {
            throw std::runtime_error("not enough values");
        }
        values.push_back(value);
    }
    return values;
}

std::vector<int> reference_sort_uniq(const std::vector<int>& input) {
    std::vector<int> output;
    for (int value : input) {
        bool seen = false;
        for (int existing : output) {
            if (existing == value) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            output.push_back(value);
        }
    }

    for (size_t i = 0; i < output.size(); ++i) {
        for (size_t j = i + 1; j < output.size(); ++j) {
            if (output[j] < output[i]) {
                std::swap(output[i], output[j]);
            }
        }
    }
    return output;
}

std::string format_values(const std::vector<int>& values) {
    std::ostringstream out;
    for (int value : values) {
        out << value << '\n';
    }
    return out.str();
}

void render_sort_uniq_plot(
    const std::vector<int>& input,
    const std::vector<int>& reference,
    const std::vector<int>& target,
    bool matched) {
    ImGui::Text("input: %zu  reference: %zu  target: %zu  %s",
        input.size(),
        reference.size(),
        target.size(),
        matched ? "MATCH" : "DIFF");

    int min_value = 0;
    int max_value = 1;
    auto scan = [&](const std::vector<int>& values) {
        for (int value : values) {
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
    };
    scan(input);
    scan(reference);
    scan(target);

    const int max_count = static_cast<int>(
        std::max({input.size(), reference.size(), target.size(), size_t{1}}));
    checker::gui::Canvas2D canvas;
    canvas.fit_bbox(-1.0, min_value - 1.0, max_count + 1.0, max_value + 1.0);
    canvas.grid(IM_COL32(55, 58, 66, 255), 1.0);

    for (size_t i = 0; i < input.size(); ++i) {
        canvas.segment(
            static_cast<double>(i),
            0.0,
            static_cast<double>(i),
            input[i],
            IM_COL32(180, 184, 192, 255),
            5.0f);
    }
    for (size_t i = 0; i < reference.size(); ++i) {
        canvas.point(static_cast<double>(i) - 0.12, reference[i], 5.0f, IM_COL32(60, 220, 120, 255));
    }
    for (size_t i = 0; i < target.size(); ++i) {
        canvas.point(static_cast<double>(i) + 0.12, target[i], 4.0f, IM_COL32(70, 160, 255, 255));
    }

    ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.76f, 1.0f), "bars=input  green=reference  blue=target");
}

checker::StressConfig<std::vector<int>, std::vector<int>> make_config() {
    checker::StressConfig<std::vector<int>, std::vector<int>> cfg;
    cfg.script_path = checker::generated::script_path;
    cfg.cases_dir = checker::generated::cases_dir;
    cfg.failed_dir = std::filesystem::path(checker::generated::cases_dir) / "failed";
    cfg.gen_resolver = checker::generated::gen_path;
    cfg.gen_source_resolver = checker::generated::gen_source_path;
    cfg.parse = parse_input;
    cfg.reference = reference_sort_uniq;
    cfg.target = [](const std::vector<int>& input) {
        return sort_uniq::compute(input);
    };
    cfg.gui.format_output = format_values;
    cfg.gui.render_plot_2d = render_sort_uniq_plot;
    cfg.gui.window_title = "sort_uniq checker";
    cfg.max_fail = 5;
    cfg.stop_on_first_fail = false;
    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    return checker::run(make_config(), argc, argv);
}
