#ifndef CHECKER_GUI_H
#define CHECKER_GUI_H

#include "checker/config.h"
#include "checker/detail/runtime.h"
#include "checker/gui/code_view.h"
#include "checker/result.h"
#include "checker/script.h"
#include "checker/stress.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace checker::gui {

class Canvas2D {
public:
    Canvas2D();

    void fit_bbox(double minx, double miny, double maxx, double maxy);
    ImVec2 world_to_screen(double x, double y) const;

    void segment(
        double x1,
        double y1,
        double x2,
        double y2,
        ImU32 color,
        float thickness = 1.5f);
    void point(double x, double y, float radius, ImU32 color);
    void label(double x, double y, const char* text, ImU32 color);
    void grid(ImU32 color, double step = 1.0);

private:
    ImDrawList* draw_list_ = nullptr;
    ImVec2 origin_{};
    ImVec2 size_{};
    double minx_ = -1.0;
    double miny_ = -1.0;
    double maxx_ = 1.0;
    double maxy_ = 1.0;
};

enum class StartTab {
    Stress,
    Build,
    Cases,
    Files,
};

struct LaunchOptions {
    StartTab tab = StartTab::Stress;
    std::string stress_name;
    bool auto_start = false;
};

namespace detail {

inline void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "checker: GLFW error %d: %s\n", error, description);
}

inline std::string join_args(const std::vector<std::string>& args) {
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            joined += ' ';
        }
        joined += args[i];
    }
    return joined;
}

inline void draw_result_counts(const StressResult& result) {
    ImGui::Text("invocations: %lld  ok: %lld  fail: %lld",
        static_cast<long long>(result.invocations),
        static_cast<long long>(result.ok),
        static_cast<long long>(result.fail));
}

template <typename Input, typename Output>
struct CaseSnapshot {
    std::string title;
    std::string source;
    std::string args;
    std::string raw;
    std::optional<std::string> expected_output_raw;
    std::optional<std::string> reference_text;
    std::optional<std::string> target_text;
    std::optional<Input> input;
    std::optional<Output> reference;
    std::optional<Output> target;
    bool matched = false;
    bool valid = false;
};

struct InvocationTestResult {
    bool ran = false;
    bool matched = false;
    int64_t elapsed_ms = 0;
    int64_t memory_kb = 0;
    std::string error;
};

template <typename Input, typename Output>
struct InvocationTestRecord {
    std::string name;
    std::string kind;
    InvocationTestResult result;
    CaseSnapshot<Input, Output> snapshot;
};

template <typename Input, typename Output>
struct InvocationRecord {
    enum class Kind {
        Stress,
        SelectedCases,
    };

    int64_t id = 0;
    Kind kind = Kind::Stress;
    std::string scope;
    std::string tests;
    std::string status;
    std::string created_at;
    std::string stress_name;
    int64_t progress_done = 0;
    int64_t progress_total = 0;
    bool running = false;
    std::vector<std::filesystem::path> case_paths;
    std::vector<std::string> case_entry_names;
    std::vector<InvocationTestRecord<Input, Output>> test_results;
    StressResult result;
    CaseSnapshot<Input, Output> snapshot;
};

inline std::string current_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

template <typename Input, typename Output>
CaseSnapshot<Input, Output> evaluate_case_data(
    const StressConfig<Input, Output>& cfg,
    std::string_view title,
    std::string_view source,
    const checker::detail::CaseFileData& data) {
    CaseSnapshot<Input, Output> snapshot;
    snapshot.title = std::string(title);
    snapshot.source = std::string(source);
    snapshot.raw = data.input;
    snapshot.expected_output_raw = data.expected_output;

    checker::detail::EvaluatedCase<Input, Output> evaluated =
        checker::detail::evaluate_case_data<Input, Output>(cfg, data);
    snapshot.matched = evaluated.matched;
    snapshot.input = std::move(evaluated.input);
    snapshot.reference = std::move(evaluated.expected);
    snapshot.target = std::move(evaluated.actual);
    snapshot.valid = true;
    return snapshot;
}

template <typename Input, typename Output>
CaseSnapshot<Input, Output> evaluate_raw(
    const StressConfig<Input, Output>& cfg,
    std::string_view title,
    std::string_view source,
    std::string_view raw) {
    checker::detail::CaseFileData data;
    data.input = std::string(raw);
    return evaluate_case_data<Input, Output>(cfg, title, source, data);
}

template <typename Input, typename Output>
CaseSnapshot<Input, Output> evaluate_case_file(
    const StressConfig<Input, Output>& cfg,
    std::string_view title,
    std::string_view source,
    std::string_view raw_file) {
    checker::detail::CaseFileData data = checker::detail::parse_case_file(raw_file);
    return evaluate_case_data<Input, Output>(cfg, title, source, data);
}

template <typename Input, typename Output>
std::string format_output_text(const StressConfig<Input, Output>& cfg, const Output& output) {
    if (cfg.gui.format_output) {
        return cfg.gui.format_output(output);
    }
    return "<format_output callback not provided>";
}

template <typename Input, typename Output>
void capture_snapshot_text(const StressConfig<Input, Output>& cfg, CaseSnapshot<Input, Output>& snapshot) {
    if (!snapshot.reference_text && snapshot.reference) {
        snapshot.reference_text = format_output_text(cfg, *snapshot.reference);
    }
    if (!snapshot.target_text && snapshot.target) {
        snapshot.target_text = format_output_text(cfg, *snapshot.target);
    }
}

template <typename Input, typename Output>
bool draw_case_summary(
    const StressConfig<Input, Output>& cfg,
    const CaseSnapshot<Input, Output>& snapshot) {
    (void)cfg;
    if (!snapshot.valid || !snapshot.input || !snapshot.reference || !snapshot.target) {
        ImGui::TextUnformatted("No case selected.");
        return false;
    }

    ImGui::Text("%s", snapshot.title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(
        snapshot.matched ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f),
        "%s",
        snapshot.matched ? "MATCH" : "DIFF");
    if (!snapshot.source.empty()) {
        ImGui::TextDisabled("%s", snapshot.source.c_str());
    }
    if (snapshot.expected_output_raw) {
        ImGui::TextDisabled("expected output: file");
    }
    if (!snapshot.args.empty()) {
        ImGui::TextDisabled("args: %s", snapshot.args.c_str());
    }
    return ImGui::Button("Detail");
}

template <typename Input, typename Output>
struct GuiState {
    Script script;
    std::vector<const ScriptEntry*> stress_entries;
    std::vector<const ScriptEntry*> case_entries;
    std::vector<std::filesystem::path> case_files;

    int selected_stress = 0;
    int selected_file = 0;
    std::vector<char> selected_test_rows;
    std::vector<char> selected_invocation_cases;
    bool selected_invocation_running = false;
    std::vector<size_t> selected_invocation_rows;
    size_t selected_invocation_index = 0;
    StressResult selected_invocation_result;
    std::vector<std::filesystem::path> selected_invocation_paths;
    std::vector<std::string> selected_invocation_entries;
    std::vector<InvocationTestRecord<Input, Output>> selected_invocation_test_results;
    int64_t selected_invocation_record_id = 0;
    bool tests_well_formed = true;
    bool enable_points = false;
    bool enable_groups = false;
    bool stress_running = false;
    int64_t stress_index = 0;
    int64_t stress_invocation_record_id = 0;
    std::mt19937 rng{std::random_device{}()};
    StressResult stress_result;

    CaseSnapshot<Input, Output> current;
    bool detail_open = false;
    bool invocation_record_open = false;
    bool invocation_test_comparison_open = false;
    bool test_item_open = false;
    bool manual_editor_open = false;
    bool rename_editor_open = false;
    bool script_editor_focus = false;
    std::array<char, 160> detail_save_name{};
    std::array<char, 160> manual_case_name{};
    std::array<char, 8192> manual_input{};
    std::array<char, 8192> manual_output{};
    std::array<char, 32768> script_editor_text{};
    std::filesystem::path rename_source;
    std::array<char, 160> rename_case_name{};
    int64_t next_invocation_id = 1;
    int64_t opened_invocation_record_id = 0;
    size_t selected_invocation_test_index = 0;
    size_t selected_test_item_row = 0;
    std::vector<InvocationRecord<Input, Output>> invocation_history;
    std::string status;
};

template <typename Input, typename Output>
void reload_case_files(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    state.case_files = checker::detail::glob_cases(cfg.cases_dir);
}

template <typename Input, typename Output>
InvocationRecord<Input, Output>* find_invocation_record(
    GuiState<Input, Output>& state,
    int64_t record_id) {
    auto found = std::find_if(
        state.invocation_history.begin(),
        state.invocation_history.end(),
        [&](const InvocationRecord<Input, Output>& record) {
            return record.id == record_id;
        });
    return found == state.invocation_history.end() ? nullptr : &*found;
}

template <size_t N>
void set_text_buffer(std::array<char, N>& buffer, std::string_view text) {
    buffer.fill('\0');
    const size_t count = std::min(text.size(), N - 1);
    std::copy_n(text.data(), count, buffer.data());
}

template <typename Input, typename Output>
void apply_script(GuiState<Input, Output>& state, Script script, std::string_view selected_stress = {}) {
    if (InvocationRecord<Input, Output>* record =
            find_invocation_record(state, state.selected_invocation_record_id)) {
        if (record->running) {
            record->running = false;
            record->status = "STOPPED";
        }
    }
    if (InvocationRecord<Input, Output>* record =
            find_invocation_record(state, state.stress_invocation_record_id)) {
        if (record->running) {
            record->running = false;
            record->status = "STOPPED";
        }
    }
    state.stress_running = false;
    state.selected_invocation_running = false;
    state.selected_invocation_rows.clear();
    state.selected_invocation_index = 0;
    state.selected_invocation_record_id = 0;
    state.stress_invocation_record_id = 0;
    state.script = std::move(script);
    state.stress_entries.clear();
    state.case_entries.clear();
    for (const ScriptEntry& entry : state.script.entries) {
        if (entry.kind == ScriptEntry::Kind::Stress) {
            state.stress_entries.push_back(&entry);
        } else {
            state.case_entries.push_back(&entry);
        }
    }

    if (!selected_stress.empty()) {
        state.selected_stress = 0;
        for (size_t i = 0; i < state.stress_entries.size(); ++i) {
            if (state.stress_entries[i]->name == selected_stress) {
                state.selected_stress = static_cast<int>(i);
                break;
            }
        }
    } else if (state.selected_stress >= static_cast<int>(state.stress_entries.size())) {
        state.selected_stress = 0;
    }

    state.selected_test_rows.assign(state.case_files.size() + state.case_entries.size(), 0);
    state.selected_invocation_cases.assign(state.case_files.size() + state.case_entries.size(), 1);
}

template <typename Input, typename Output>
void reload_script_file(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    std::string_view selected_stress = {}) {
    const std::string text = checker::detail::read_file(cfg.script_path);
    set_text_buffer(state.script_editor_text, text);
    apply_script(state, parse_script_string(text), selected_stress);
}

template <typename Input, typename Output>
void save_script_file(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    const std::string text = state.script_editor_text.data();
    Script parsed = parse_script_string(text);
    std::ofstream file(cfg.script_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("checker: failed to open script path: " + cfg.script_path.string());
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        throw std::runtime_error("checker: failed to write script path: " + cfg.script_path.string());
    }
    apply_script(state, std::move(parsed));
    state.status = "Saved " + cfg.script_path.filename().string();
}

template <typename Input, typename Output>
std::filesystem::path invocation_history_path(const StressConfig<Input, Output>& cfg) {
    std::filesystem::path base = cfg.script_path.parent_path();
    if (base.empty()) {
        base = cfg.cases_dir.parent_path();
    }
    if (base.empty()) {
        base = ".";
    }
    return base / ".checker" / "invocations.txt";
}

inline void write_quoted_field(std::ostream& out, std::string_view name, std::string_view value) {
    out << name << ' ' << std::quoted(std::string(value)) << '\n';
}

inline void write_optional_quoted_field(
    std::ostream& out,
    std::string_view name,
    const std::optional<std::string>& value) {
    out << name << ' ' << (value ? 1 : 0);
    if (value) {
        out << ' ' << std::quoted(*value);
    }
    out << '\n';
}

inline std::string read_tag(std::istream& in) {
    std::string tag;
    if (!(in >> tag)) {
        throw std::runtime_error("checker: malformed invocation history");
    }
    return tag;
}

inline void expect_tag(std::istream& in, std::string_view expected) {
    const std::string tag = read_tag(in);
    if (tag != expected) {
        throw std::runtime_error(
            "checker: malformed invocation history, expected " +
            std::string(expected) + " got " + tag);
    }
}

inline std::string read_quoted_field(std::istream& in, std::string_view name) {
    expect_tag(in, name);
    std::string value;
    if (!(in >> std::quoted(value))) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    return value;
}

inline std::optional<std::string> read_optional_quoted_field(std::istream& in, std::string_view name) {
    expect_tag(in, name);
    int present = 0;
    if (!(in >> present)) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    if (present == 0) {
        return std::nullopt;
    }
    std::string value;
    if (!(in >> std::quoted(value))) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    return value;
}

inline int64_t read_i64_field(std::istream& in, std::string_view name) {
    expect_tag(in, name);
    int64_t value = 0;
    if (!(in >> value)) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    return value;
}

inline double read_double_field(std::istream& in, std::string_view name) {
    expect_tag(in, name);
    double value = 0.0;
    if (!(in >> value)) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    return value;
}

inline bool read_bool_field(std::istream& in, std::string_view name) {
    return read_i64_field(in, name) != 0;
}

inline size_t read_size_field(std::istream& in, std::string_view name) {
    const int64_t value = read_i64_field(in, name);
    if (value < 0) {
        throw std::runtime_error("checker: malformed invocation history field: " + std::string(name));
    }
    return static_cast<size_t>(value);
}

template <typename Input, typename Output>
void prepare_snapshot_for_persistence(
    const StressConfig<Input, Output>& cfg,
    CaseSnapshot<Input, Output>& snapshot) {
    capture_snapshot_text(cfg, snapshot);
}

template <typename Input, typename Output>
void prepare_record_for_persistence(
    const StressConfig<Input, Output>& cfg,
    InvocationRecord<Input, Output>& record) {
    if (record.running) {
        record.running = false;
        record.status = "STOPPED";
    }
    prepare_snapshot_for_persistence(cfg, record.snapshot);
    for (InvocationTestRecord<Input, Output>& test : record.test_results) {
        prepare_snapshot_for_persistence(cfg, test.snapshot);
    }
}

template <typename Input, typename Output>
void write_snapshot(std::ostream& out, const CaseSnapshot<Input, Output>& snapshot) {
    out << "snapshot_valid " << (snapshot.valid ? 1 : 0) << '\n';
    write_quoted_field(out, "snapshot_title", snapshot.title);
    write_quoted_field(out, "snapshot_source", snapshot.source);
    write_quoted_field(out, "snapshot_args", snapshot.args);
    write_quoted_field(out, "snapshot_raw", snapshot.raw);
    write_optional_quoted_field(out, "snapshot_expected", snapshot.expected_output_raw);
    write_optional_quoted_field(out, "snapshot_reference_text", snapshot.reference_text);
    write_optional_quoted_field(out, "snapshot_target_text", snapshot.target_text);
    out << "snapshot_matched " << (snapshot.matched ? 1 : 0) << '\n';
}

template <typename Input, typename Output>
CaseSnapshot<Input, Output> read_snapshot(std::istream& in) {
    CaseSnapshot<Input, Output> snapshot;
    snapshot.valid = read_bool_field(in, "snapshot_valid");
    snapshot.title = read_quoted_field(in, "snapshot_title");
    snapshot.source = read_quoted_field(in, "snapshot_source");
    snapshot.args = read_quoted_field(in, "snapshot_args");
    snapshot.raw = read_quoted_field(in, "snapshot_raw");
    snapshot.expected_output_raw = read_optional_quoted_field(in, "snapshot_expected");
    snapshot.reference_text = read_optional_quoted_field(in, "snapshot_reference_text");
    snapshot.target_text = read_optional_quoted_field(in, "snapshot_target_text");
    snapshot.matched = read_bool_field(in, "snapshot_matched");
    return snapshot;
}

template <typename Input, typename Output>
void write_invocation_record(std::ostream& out, const InvocationRecord<Input, Output>& record) {
    out << "record\n";
    out << "id " << record.id << '\n';
    out << "kind " << (record.kind == InvocationRecord<Input, Output>::Kind::Stress ? "stress" : "selected") << '\n';
    write_quoted_field(out, "scope", record.scope);
    write_quoted_field(out, "tests", record.tests);
    write_quoted_field(out, "status", record.status);
    write_quoted_field(out, "created_at", record.created_at);
    write_quoted_field(out, "stress_name", record.stress_name);
    out << "progress_done " << record.progress_done << '\n';
    out << "progress_total " << record.progress_total << '\n';
    out << "running " << (record.running ? 1 : 0) << '\n';
    out << "result_invocations " << record.result.invocations << '\n';
    out << "result_ok " << record.result.ok << '\n';
    out << "result_fail " << record.result.fail << '\n';
    out << "result_elapsed_sec " << record.result.elapsed_sec << '\n';

    out << "archived_cases " << record.result.archived_cases.size() << '\n';
    for (const std::filesystem::path& path : record.result.archived_cases) {
        write_quoted_field(out, "archived_case", path.string());
    }
    out << "case_paths " << record.case_paths.size() << '\n';
    for (const std::filesystem::path& path : record.case_paths) {
        write_quoted_field(out, "case_path", path.string());
    }
    out << "case_entries " << record.case_entry_names.size() << '\n';
    for (const std::string& name : record.case_entry_names) {
        write_quoted_field(out, "case_entry", name);
    }

    write_snapshot(out, record.snapshot);
    out << "test_results " << record.test_results.size() << '\n';
    for (const InvocationTestRecord<Input, Output>& test : record.test_results) {
        out << "test\n";
        write_quoted_field(out, "test_name", test.name);
        write_quoted_field(out, "test_kind", test.kind);
        out << "test_ran " << (test.result.ran ? 1 : 0) << '\n';
        out << "test_matched " << (test.result.matched ? 1 : 0) << '\n';
        out << "test_elapsed_ms " << test.result.elapsed_ms << '\n';
        out << "test_memory_kb " << test.result.memory_kb << '\n';
        write_quoted_field(out, "test_error", test.result.error);
        write_snapshot(out, test.snapshot);
        out << "endtest\n";
    }
    out << "endrecord\n";
}

template <typename Input, typename Output>
InvocationRecord<Input, Output> read_invocation_record(std::istream& in) {
    InvocationRecord<Input, Output> record;
    record.id = read_i64_field(in, "id");

    expect_tag(in, "kind");
    std::string kind;
    if (!(in >> kind)) {
        throw std::runtime_error("checker: malformed invocation history field: kind");
    }
    record.kind = kind == "selected"
        ? InvocationRecord<Input, Output>::Kind::SelectedCases
        : InvocationRecord<Input, Output>::Kind::Stress;

    record.scope = read_quoted_field(in, "scope");
    record.tests = read_quoted_field(in, "tests");
    record.status = read_quoted_field(in, "status");
    record.created_at = read_quoted_field(in, "created_at");
    record.stress_name = read_quoted_field(in, "stress_name");
    record.progress_done = read_i64_field(in, "progress_done");
    record.progress_total = read_i64_field(in, "progress_total");
    record.running = read_bool_field(in, "running");
    if (record.running) {
        record.running = false;
        record.status = "STOPPED";
    }

    record.result.invocations = read_i64_field(in, "result_invocations");
    record.result.ok = read_i64_field(in, "result_ok");
    record.result.fail = read_i64_field(in, "result_fail");
    record.result.elapsed_sec = read_double_field(in, "result_elapsed_sec");

    const size_t archived_count = read_size_field(in, "archived_cases");
    record.result.archived_cases.reserve(archived_count);
    for (size_t i = 0; i < archived_count; ++i) {
        record.result.archived_cases.emplace_back(read_quoted_field(in, "archived_case"));
    }
    const size_t path_count = read_size_field(in, "case_paths");
    record.case_paths.reserve(path_count);
    for (size_t i = 0; i < path_count; ++i) {
        record.case_paths.emplace_back(read_quoted_field(in, "case_path"));
    }
    const size_t entry_count = read_size_field(in, "case_entries");
    record.case_entry_names.reserve(entry_count);
    for (size_t i = 0; i < entry_count; ++i) {
        record.case_entry_names.push_back(read_quoted_field(in, "case_entry"));
    }

    record.snapshot = read_snapshot<Input, Output>(in);
    const size_t test_count = read_size_field(in, "test_results");
    record.test_results.reserve(test_count);
    for (size_t i = 0; i < test_count; ++i) {
        expect_tag(in, "test");
        InvocationTestRecord<Input, Output> test;
        test.name = read_quoted_field(in, "test_name");
        test.kind = read_quoted_field(in, "test_kind");
        test.result.ran = read_bool_field(in, "test_ran");
        test.result.matched = read_bool_field(in, "test_matched");
        test.result.elapsed_ms = read_i64_field(in, "test_elapsed_ms");
        test.result.memory_kb = read_i64_field(in, "test_memory_kb");
        test.result.error = read_quoted_field(in, "test_error");
        test.snapshot = read_snapshot<Input, Output>(in);
        expect_tag(in, "endtest");
        record.test_results.push_back(std::move(test));
    }
    expect_tag(in, "endrecord");
    return record;
}

template <typename Input, typename Output>
void save_invocation_history(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    const std::filesystem::path path = invocation_history_path(cfg);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("checker: failed to open invocation history path: " + path.string());
    }

    out << "checker_invocations_v1\n";
    out << "records " << state.invocation_history.size() << '\n';
    for (InvocationRecord<Input, Output> record : state.invocation_history) {
        prepare_record_for_persistence(cfg, record);
        write_invocation_record(out, record);
    }
    if (!out) {
        throw std::runtime_error("checker: failed to write invocation history path: " + path.string());
    }
}

template <typename Input, typename Output>
void load_invocation_history(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    const std::filesystem::path path = invocation_history_path(cfg);
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("checker: failed to open invocation history path: " + path.string());
    }

    expect_tag(in, "checker_invocations_v1");
    const size_t record_count = read_size_field(in, "records");
    std::vector<InvocationRecord<Input, Output>> records;
    records.reserve(record_count);
    int64_t next_id = 1;
    for (size_t i = 0; i < record_count; ++i) {
        expect_tag(in, "record");
        InvocationRecord<Input, Output> record = read_invocation_record<Input, Output>(in);
        next_id = std::max(next_id, record.id + 1);
        records.push_back(std::move(record));
    }

    state.invocation_history = std::move(records);
    state.next_invocation_id = std::max(state.next_invocation_id, next_id);
}

inline std::string script_case_line(const ScriptEntry& entry, std::string_view name);
inline std::string unique_copy_name(
    std::string_view base_name,
    const std::function<bool(std::string_view)>& exists);
inline void write_case_bytes(
    const std::filesystem::path& cases_dir,
    const std::filesystem::path& name,
    std::string_view bytes);

template <typename Input, typename Output>
void duplicate_script_case(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    const ScriptEntry& entry) {
    const std::string new_name = unique_copy_name(
        entry.name,
        [&](std::string_view candidate) {
            return std::any_of(
                state.case_entries.begin(),
                state.case_entries.end(),
                [&](const ScriptEntry* existing) {
                    return existing->name == candidate;
                });
        });

    std::string text = state.script_editor_text.data();
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    text += script_case_line(entry, new_name);
    text += '\n';
    set_text_buffer(state.script_editor_text, text);
    save_script_file(cfg, state);
    state.status = "Duplicated script case " + entry.name + " as " + new_name;
}

template <typename Input, typename Output>
void duplicate_case_file(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    std::string_view label,
    std::string_view raw) {
    const std::string duplicate_name = unique_copy_name(
        label,
        [&](std::string_view candidate) {
            return std::filesystem::exists(cfg.cases_dir / std::string(candidate));
        });
    write_case_bytes(cfg.cases_dir, duplicate_name, raw);
    reload_case_files(cfg, state);
    state.selected_test_rows.assign(state.case_files.size() + state.case_entries.size(), 0);
    state.status = "Duplicated " + std::string(label) + " as " + duplicate_name;
}

template <typename Input, typename Output>
void initialize_state(
    const StressConfig<Input, Output>& cfg,
    const LaunchOptions& options,
    GuiState<Input, Output>& state) {
    reload_script_file(cfg, state, options.stress_name);
    state.stress_running = options.auto_start && !state.stress_entries.empty();
    reload_case_files(cfg, state);
    load_invocation_history(cfg, state);
}

template <typename Input, typename Output>
void reset_stress_run(GuiState<Input, Output>& state) {
    state.stress_index = 0;
    state.stress_invocation_record_id = 0;
    state.stress_result = StressResult{};
    state.current = CaseSnapshot<Input, Output>{};
    state.rng.seed(std::random_device{}());
}

template <typename Input, typename Output>
void open_detail(GuiState<Input, Output>& state, const CaseSnapshot<Input, Output>& snapshot) {
    if (!snapshot.valid) {
        return;
    }
    state.detail_open = true;
    std::string default_name = snapshot.title.empty() ? "case.txt" : snapshot.title;
    for (char& ch : default_name) {
        if (ch == '/' || ch == '\\' || ch == ' ') {
            ch = '_';
        }
    }
    if (default_name.find('.') == std::string::npos) {
        default_name += ".txt";
    }
    std::snprintf(state.detail_save_name.data(), state.detail_save_name.size(), "%s", default_name.c_str());
}

template <typename Input, typename Output>
void open_test_item(GuiState<Input, Output>& state, size_t row) {
    state.selected_test_item_row = row;
    state.test_item_open = true;
}

inline void draw_text_box(std::string_view text, const ImVec2& size) {
    ImGui::BeginChild("text_box", size, true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::EndChild();
}

inline bool icon_button(const char* label, const char* tooltip) {
    const bool clicked = ImGui::SmallButton(label);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

inline std::string arg_token_text(const ArgToken& token) {
    if (const auto* literal = std::get_if<std::string>(&token)) {
        return *literal;
    }
    const auto& range = std::get<RangeArg>(token);
    return "[" + std::to_string(range.lo) + ".." + std::to_string(range.hi) + "]";
}

inline std::string entry_command(const ScriptEntry& entry) {
    std::string text = entry.generator;
    for (const ArgToken& token : entry.args) {
        text += ' ';
        text += arg_token_text(token);
    }
    return text;
}

inline std::string script_case_line(const ScriptEntry& entry, std::string_view name) {
    return "case " + std::string(name) + " " + entry_command(entry);
}

inline std::string unique_copy_name(
    std::string_view base_name,
    const std::function<bool(std::string_view)>& exists) {
    std::string stem(base_name);
    std::string extension;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) {
        extension = stem.substr(dot);
        stem.resize(dot);
    }

    for (int suffix = 1; suffix < 10000; ++suffix) {
        std::string candidate = stem + "_copy";
        if (suffix > 1) {
            candidate += std::to_string(suffix);
        }
        candidate += extension;
        if (!exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("checker: failed to create unique duplicate name");
}

struct FileViewEntry {
    std::string label;
    std::string kind;
    std::filesystem::path path;
};

template <typename Input, typename Output>
std::vector<std::string> generator_names(const GuiState<Input, Output>& state) {
    std::vector<std::string> names;
    auto append = [&](const ScriptEntry* entry) {
        if (std::find(names.begin(), names.end(), entry->generator) == names.end()) {
            names.push_back(entry->generator);
        }
    };
    for (const ScriptEntry* entry : state.case_entries) {
        append(entry);
    }
    for (const ScriptEntry* entry : state.stress_entries) {
        append(entry);
    }
    return names;
}

inline std::string preview_lines(std::string_view text, int max_lines = 5, size_t max_chars = 260) {
    std::string preview;
    int lines = 0;
    size_t pos = 0;
    while (pos < text.size() && lines < max_lines && preview.size() < max_chars) {
        size_t line_end = text.find('\n', pos);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        std::string_view line = text.substr(pos, line_end - pos);
        if (preview.size() + line.size() > max_chars) {
            line = line.substr(0, max_chars - preview.size());
        }
        preview.append(line.data(), line.size());
        ++lines;
        pos = line_end < text.size() ? line_end + 1 : line_end;
        if (pos < text.size() && lines < max_lines && preview.size() < max_chars) {
            preview += '\n';
        }
    }
    if (pos < text.size() || preview.size() >= max_chars) {
        preview += "\n...";
    }
    return preview;
}

inline std::string preview_block(std::string_view text, int max_lines, size_t max_chars) {
    std::string preview = preview_lines(text, max_lines, max_chars);
    return preview.empty() ? "<empty>" : preview;
}

inline std::string preview_case_data(const checker::detail::CaseFileData& data) {
    if (!data.expected_output) {
        return preview_lines(data.input);
    }

    return "input:\n" + preview_block(data.input, 3, 160) +
        "\noutput:\n" + preview_block(*data.expected_output, 3, 160);
}

template <typename Input, typename Output>
void open_manual_editor(
    GuiState<Input, Output>& state,
    std::string_view name,
    std::string_view input,
    std::string_view output) {
    set_text_buffer(state.manual_case_name, name);
    set_text_buffer(state.manual_input, input);
    set_text_buffer(state.manual_output, output);
    state.manual_editor_open = true;
}

inline std::string result_status(const StressResult& result) {
    return result.fail == 0 ? "FINISHED" : "FAILED";
}

inline ImVec4 status_color(std::string_view status) {
    if (status == "RUNNING") {
        return ImVec4(0.35f, 0.62f, 1.0f, 1.0f);
    }
    if (status == "FINISHED") {
        return ImVec4(0.25f, 0.78f, 0.34f, 1.0f);
    }
    if (status == "FAILED") {
        return ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
    }
    if (status == "STOPPED") {
        return ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
    }
    return ImVec4(0.72f, 0.74f, 0.78f, 1.0f);
}

inline void draw_status_text(std::string_view status) {
    ImGui::TextColored(status_color(status), "%.*s", static_cast<int>(status.size()), status.data());
}

inline std::string result_summary(const StressResult& result) {
    return "ok=" + std::to_string(result.ok) +
        " fail=" + std::to_string(result.fail) +
        " invocations=" + std::to_string(result.invocations);
}

inline int64_t elapsed_ms_since(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
}

inline void draw_invocation_test_result(const InvocationTestResult& result) {
    if (!result.ran) {
        ImGui::TextDisabled("-");
        return;
    }
    if (!result.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "ERR");
        ImGui::SameLine();
        ImGui::TextDisabled("%lld / %lld",
            static_cast<long long>(result.elapsed_ms),
            static_cast<long long>(result.memory_kb));
        return;
    }

    ImGui::TextColored(
        result.matched ? ImVec4(0.20f, 0.75f, 0.25f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f),
        "%s",
        result.matched ? "OK" : "DIFF");
    ImGui::SameLine();
    ImGui::TextDisabled("%lld / %lld",
        static_cast<long long>(result.elapsed_ms),
        static_cast<long long>(result.memory_kb));
}

inline float invocation_progress_fraction(int64_t done, int64_t total) {
    if (total <= 0) {
        return 0.0f;
    }
    done = std::clamp<int64_t>(done, 0, total);
    return static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
}

inline void draw_invocation_progress_bar(const char* id, int64_t done, int64_t total) {
    done = std::clamp<int64_t>(done, 0, std::max<int64_t>(total, 0));
    const float fraction = invocation_progress_fraction(done, total);
    char overlay[96]{};
    std::snprintf(
        overlay,
        sizeof(overlay),
        "%lld / %lld (%.1f%%)",
        static_cast<long long>(done),
        static_cast<long long>(std::max<int64_t>(total, 0)),
        static_cast<double>(fraction) * 100.0);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
    (void)id;
}

template <typename Input, typename Output>
void open_invocation_record(GuiState<Input, Output>& state, int64_t record_id) {
    state.opened_invocation_record_id = record_id;
    state.invocation_record_open = true;
}

template <typename Input, typename Output>
void open_invocation_test_comparison(
    GuiState<Input, Output>& state,
    int64_t record_id,
    size_t test_index) {
    state.opened_invocation_record_id = record_id;
    state.selected_invocation_test_index = test_index;
    state.invocation_test_comparison_open = true;
}

template <typename Input, typename Output>
int64_t add_invocation_record(
    GuiState<Input, Output>& state,
    typename InvocationRecord<Input, Output>::Kind kind,
    std::string scope,
    std::string tests,
    StressResult result,
    CaseSnapshot<Input, Output> snapshot,
    std::string stress_name = {},
    std::vector<std::filesystem::path> case_paths = {},
    std::vector<std::string> case_entry_names = {},
    std::vector<InvocationTestRecord<Input, Output>> test_results = {}) {
    InvocationRecord<Input, Output> record;
    record.id = state.next_invocation_id++;
    const int64_t id = record.id;
    record.kind = kind;
    record.scope = std::move(scope);
    record.tests = std::move(tests);
    record.status = result_status(result);
    record.created_at = current_timestamp();
    record.stress_name = std::move(stress_name);
    record.progress_done = result.invocations;
    record.progress_total = result.invocations;
    record.running = false;
    record.case_paths = std::move(case_paths);
    record.case_entry_names = std::move(case_entry_names);
    record.test_results = std::move(test_results);
    record.result = std::move(result);
    record.snapshot = std::move(snapshot);
    state.invocation_history.insert(state.invocation_history.begin(), std::move(record));
    return id;
}

template <typename Input, typename Output>
int64_t add_running_invocation_record(
    GuiState<Input, Output>& state,
    typename InvocationRecord<Input, Output>::Kind kind,
    std::string scope,
    std::string tests,
    int64_t progress_total,
    std::string stress_name = {}) {
    InvocationRecord<Input, Output> record;
    record.id = state.next_invocation_id++;
    const int64_t id = record.id;
    record.kind = kind;
    record.scope = std::move(scope);
    record.tests = std::move(tests);
    record.status = "RUNNING";
    record.created_at = current_timestamp();
    record.stress_name = std::move(stress_name);
    record.progress_done = 0;
    record.progress_total = progress_total;
    record.running = true;
    state.invocation_history.insert(state.invocation_history.begin(), std::move(record));
    return id;
}

inline std::filesystem::path checked_case_name(std::string_view raw_name) {
    std::string text(raw_name);
    if (text.empty()) {
        throw std::runtime_error("checker: case name is required");
    }

    std::filesystem::path name = text;
    if (name.extension().empty()) {
        name += ".txt";
    }
    if (name.is_absolute() || name.has_parent_path() ||
        name.string().find("..") != std::string::npos) {
        throw std::runtime_error("checker: case name must be a simple relative file name");
    }
    return name;
}

inline void write_case_bytes(
    const std::filesystem::path& cases_dir,
    const std::filesystem::path& name,
    std::string_view bytes) {
    std::filesystem::create_directories(cases_dir);
    const std::filesystem::path destination = cases_dir / name;
    std::ofstream file(destination, std::ios::binary);
    if (!file) {
        throw std::runtime_error("checker: failed to open case path: " + destination.string());
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("checker: failed to write case path: " + destination.string());
    }
}

template <typename Input, typename Output>
void save_detail_raw(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    std::filesystem::path name = checked_case_name(state.detail_save_name.data());
    const std::string bytes = state.current.expected_output_raw
        ? checker::detail::format_case_file(state.current.raw, *state.current.expected_output_raw)
        : state.current.raw;
    write_case_bytes(cfg.cases_dir, name, bytes);
    state.status = "Saved " + name.filename().string();
    reload_case_files(cfg, state);
}

template <typename Input, typename Output>
void save_manual_case(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    const std::filesystem::path name = checked_case_name(state.manual_case_name.data());
    const std::string input = state.manual_input.data();
    if (input.empty()) {
        throw std::runtime_error("checker: manual input is required");
    }
    const std::string output = state.manual_output.data();
    const std::string bytes = output.empty()
        ? input
        : checker::detail::format_case_file(input, output);
    write_case_bytes(cfg.cases_dir, name, bytes);
    reload_case_files(cfg, state);
    state.status = "Saved " + name.filename().string();
}

template <typename Input, typename Output>
void draw_manual_editor_modal(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.manual_editor_open) {
        ImGui::OpenPopup("Test Editor");
    }

    if (!ImGui::BeginPopupModal("Test Editor", &state.manual_editor_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("file", state.manual_case_name.data(), state.manual_case_name.size());

    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = std::max(280.0f, (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f);
    ImGui::InputTextMultiline("input", state.manual_input.data(), state.manual_input.size(), ImVec2(width, 220.0f));
    ImGui::SameLine();
    ImGui::InputTextMultiline("output", state.manual_output.data(), state.manual_output.size(), ImVec2(width, 220.0f));

    if (ImGui::Button("Run draft")) {
        try {
            checker::detail::CaseFileData data;
            data.input = state.manual_input.data();
            if (state.manual_output[0] != '\0') {
                data.expected_output = std::string(state.manual_output.data());
            }
            state.current = evaluate_case_data<Input, Output>(
                cfg, state.manual_case_name.data(), "manual", data);
            state.status = "Manual case evaluated.";
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        try {
            save_manual_case(cfg, state);
            state.manual_editor_open = false;
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        state.manual_editor_open = false;
        ImGui::CloseCurrentPopup();
    }
    if (!state.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", state.status.c_str());
    }

    ImGui::EndPopup();
}

template <typename Input, typename Output>
void draw_case_detail_modal(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.detail_open) {
        ImGui::OpenPopup("Case Detail");
    }

    if (!ImGui::BeginPopupModal("Case Detail", &state.detail_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const CaseSnapshot<Input, Output>& snapshot = state.current;
    if (!snapshot.valid || !snapshot.input || !snapshot.reference || !snapshot.target) {
        ImGui::TextUnformatted("No case selected.");
        if (ImGui::Button("Close")) {
            state.detail_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("%s", snapshot.title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(
        snapshot.matched ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f),
        "%s",
        snapshot.matched ? "MATCH" : "DIFF");
    if (!snapshot.source.empty()) {
        ImGui::TextDisabled("%s", snapshot.source.c_str());
    }

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("case file", state.detail_save_name.data(), state.detail_save_name.size());
    ImGui::SameLine();
    if (ImGui::Button("Save raw to cases/")) {
        try {
            save_detail_raw(cfg, state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        state.detail_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SeparatorText("Input");
    draw_text_box(snapshot.raw, ImVec2(720.0f, 150.0f));

    if (ImGui::BeginTabBar("case_detail_tabs")) {
        if (ImGui::BeginTabItem("Text")) {
            const std::string ref = format_output_text(cfg, *snapshot.reference);
            const std::string got = format_output_text(cfg, *snapshot.target);
            ImGui::Text("Output diff (%s vs target)",
                snapshot.expected_output_raw ? "expected" : "reference");
            ImGui::BeginChild("detail_text", ImVec2(720.0f, 260.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(snapshot.expected_output_raw ? "Expected:" : "Reference:");
            ImGui::TextUnformatted(ref.data(), ref.data() + ref.size());
            ImGui::Separator();
            ImGui::TextUnformatted("Target:");
            ImGui::TextUnformatted(got.data(), got.data() + got.size());
            ImGui::Separator();
            ImGui::Text("Summary: %s", snapshot.matched ? "matched" : "different");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (cfg.gui.render_plot_2d && ImGui::BeginTabItem("Plot")) {
            cfg.gui.render_plot_2d(*snapshot.input, *snapshot.reference, *snapshot.target, snapshot.matched);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndPopup();
}

template <typename Input, typename Output>
bool run_one_stress_step(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.stress_entries.empty()) {
        state.status = "No stress entries in script.";
        return false;
    }

    const ScriptEntry& entry = *state.stress_entries[static_cast<size_t>(state.selected_stress)];
    if (state.stress_index >= entry.count) {
        state.status = "Stress entry finished.";
        return false;
    }

    const char* generator_path = checker::detail::resolve_generator(cfg.gen_resolver, entry.generator);
    std::vector<std::string> args = resolve_args(entry, state.rng);
    std::string raw = checker::detail::run_generator(generator_path, args, cfg.subprocess_timeout_sec);

    CaseSnapshot<Input, Output> snapshot =
        evaluate_raw<Input, Output>(cfg, entry.name, entry.generator, raw);
    snapshot.args = join_args(args);

    ++state.stress_result.invocations;
    if (snapshot.matched) {
        ++state.stress_result.ok;
    } else {
        ++state.stress_result.fail;
        std::filesystem::path archived =
            checker::detail::archive_failed(cfg.failed_dir, entry.name, state.stress_index, raw);
        state.stress_result.archived_cases.push_back(archived);
        reload_case_files(cfg, state);
    }

    state.current = std::move(snapshot);
    ++state.stress_index;

    if (cfg.stop_on_first_fail && state.stress_result.fail > 0) {
        state.status = "Stopped on first failure.";
        return false;
    }
    if (cfg.max_fail > 0 && state.stress_result.fail >= cfg.max_fail) {
        state.status = "Stopped at max_fail.";
        return false;
    }
    if (state.stress_index >= entry.count) {
        state.status = "Stress entry finished.";
        return false;
    }
    return true;
}

template <typename Input, typename Output>
void finish_selected_case_run(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    state.selected_invocation_running = false;
    state.stress_running = false;
    state.stress_result = state.selected_invocation_result;
    state.status = "selected tests: ok=" + std::to_string(state.selected_invocation_result.ok) +
        " fail=" + std::to_string(state.selected_invocation_result.fail);
    if (InvocationRecord<Input, Output>* record =
            find_invocation_record(state, state.selected_invocation_record_id)) {
        record->status = result_status(state.selected_invocation_result);
        record->running = false;
        record->progress_done = static_cast<int64_t>(state.selected_invocation_rows.size());
        record->progress_total = static_cast<int64_t>(state.selected_invocation_rows.size());
        record->result = state.selected_invocation_result;
        record->snapshot = state.current;
        record->case_paths = state.selected_invocation_paths;
        record->case_entry_names = state.selected_invocation_entries;
        record->test_results = state.selected_invocation_test_results;
    }
    try {
        save_invocation_history(cfg, state);
    } catch (const std::exception& ex) {
        state.status = ex.what();
    }
}

template <typename Input, typename Output>
bool start_selected_case_run(GuiState<Input, Output>& state) {
    state.selected_invocation_rows.clear();
    for (size_t i = 0; i < state.case_files.size(); ++i) {
        if (i >= state.selected_invocation_cases.size() || !state.selected_invocation_cases[i]) {
            continue;
        }
        state.selected_invocation_rows.push_back(i);
    }

    const size_t entry_offset = state.case_files.size();
    for (size_t i = 0; i < state.case_entries.size(); ++i) {
        const size_t row = entry_offset + i;
        if (row >= state.selected_invocation_cases.size() || !state.selected_invocation_cases[row]) {
            continue;
        }
        state.selected_invocation_rows.push_back(row);
    }

    if (state.selected_invocation_rows.empty()) {
        state.status = "No tests selected.";
        return false;
    }

    state.stress_running = false;
    state.selected_invocation_running = true;
    state.selected_invocation_index = 0;
    state.selected_invocation_result = StressResult{};
    state.selected_invocation_paths.clear();
    state.selected_invocation_entries.clear();
    state.selected_invocation_test_results.clear();
    state.selected_invocation_record_id = add_running_invocation_record<Input, Output>(
        state,
        InvocationRecord<Input, Output>::Kind::SelectedCases,
        "selected tests",
        std::to_string(state.selected_invocation_rows.size()) + " selected",
        static_cast<int64_t>(state.selected_invocation_rows.size()));
    state.status = "Running selected tests.";
    return true;
}

template <typename Input, typename Output>
bool run_one_selected_case_step(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (!state.selected_invocation_running) {
        return false;
    }
    if (state.selected_invocation_index >= state.selected_invocation_rows.size()) {
        finish_selected_case_run(cfg, state);
        return false;
    }

    const size_t row = state.selected_invocation_rows[state.selected_invocation_index];
    InvocationTestRecord<Input, Output> test_record;
    InvocationTestResult row_result;
    row_result.ran = true;
    ++state.selected_invocation_result.invocations;

    if (row < state.case_files.size()) {
        const std::filesystem::path& path = state.case_files[row];
        test_record.name = path.filename().string();
        test_record.kind = "file";
        state.selected_invocation_paths.push_back(path);
        try {
            const auto started = std::chrono::steady_clock::now();
            std::string raw = checker::detail::read_file(path);
            CaseSnapshot<Input, Output> snapshot =
                evaluate_case_file<Input, Output>(cfg, path.filename().string(), path.string(), raw);
            row_result.elapsed_ms = elapsed_ms_since(started);
            row_result.matched = snapshot.matched;
            if (snapshot.matched) {
                ++state.selected_invocation_result.ok;
            } else {
                ++state.selected_invocation_result.fail;
            }
            test_record.snapshot = snapshot;
            state.current = std::move(snapshot);
        } catch (const std::exception& ex) {
            row_result.elapsed_ms = 0;
            row_result.error = ex.what();
            ++state.selected_invocation_result.fail;
        }
    } else if (row - state.case_files.size() < state.case_entries.size()) {
        const ScriptEntry& entry = *state.case_entries[row - state.case_files.size()];
        state.selected_invocation_entries.push_back(entry.name);
        test_record.name = entry.name;
        test_record.kind = "script";
        try {
            const auto started = std::chrono::steady_clock::now();
            const char* generator_path =
                checker::detail::resolve_generator(cfg.gen_resolver, entry.generator);
            std::mt19937 rng(0);
            std::vector<std::string> args = resolve_args(entry, rng);
            std::string raw = checker::detail::run_generator(
                generator_path, args, cfg.subprocess_timeout_sec);
            CaseSnapshot<Input, Output> snapshot =
                evaluate_raw<Input, Output>(cfg, entry.name, entry.generator, raw);
            snapshot.args = join_args(args);
            row_result.elapsed_ms = elapsed_ms_since(started);
            row_result.matched = snapshot.matched;
            if (snapshot.matched) {
                ++state.selected_invocation_result.ok;
            } else {
                ++state.selected_invocation_result.fail;
            }
            test_record.snapshot = snapshot;
            state.current = std::move(snapshot);
        } catch (const std::exception& ex) {
            row_result.elapsed_ms = 0;
            row_result.error = ex.what();
            ++state.selected_invocation_result.fail;
        }
    } else {
        test_record.name = "missing row " + std::to_string(row + 1);
        test_record.kind = "missing";
        row_result.error = "test row no longer exists";
        ++state.selected_invocation_result.fail;
    }

    test_record.result = std::move(row_result);
    state.selected_invocation_test_results.push_back(std::move(test_record));
    ++state.selected_invocation_index;

    if (InvocationRecord<Input, Output>* record =
            find_invocation_record(state, state.selected_invocation_record_id)) {
        record->progress_done = static_cast<int64_t>(state.selected_invocation_index);
        record->progress_total = static_cast<int64_t>(state.selected_invocation_rows.size());
        record->result = state.selected_invocation_result;
        record->snapshot = state.current;
        record->case_paths = state.selected_invocation_paths;
        record->case_entry_names = state.selected_invocation_entries;
        record->test_results = state.selected_invocation_test_results;
    }

    if (state.selected_invocation_index >= state.selected_invocation_rows.size()) {
        finish_selected_case_run(cfg, state);
        return false;
    }

    return true;
}

template <typename Input, typename Output>
void run_selected_case_files(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (!start_selected_case_run(state)) {
        return;
    }
    while (state.selected_invocation_running) {
        run_one_selected_case_step(cfg, state);
    }
}

template <typename Input, typename Output>
void draw_invocation_test_comparison_modal(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state) {
    if (state.invocation_test_comparison_open) {
        ImGui::OpenPopup("Test Comparison");
    }

    if (!ImGui::BeginPopupModal(
            "Test Comparison",
            &state.invocation_test_comparison_open,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto found = std::find_if(
        state.invocation_history.begin(),
        state.invocation_history.end(),
        [&](const InvocationRecord<Input, Output>& record) {
            return record.id == state.opened_invocation_record_id;
        });

    if (found == state.invocation_history.end() ||
        state.selected_invocation_test_index >= found->test_results.size()) {
        ImGui::TextUnformatted("No test result selected.");
        if (ImGui::Button("Close")) {
            state.invocation_test_comparison_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    const InvocationTestRecord<Input, Output>& test =
        found->test_results[state.selected_invocation_test_index];
    ImGui::Text("%s", test.name.c_str());
    ImGui::SameLine();
    if (!test.result.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "ERR");
    } else {
        ImGui::TextColored(
            test.result.matched ? ImVec4(0.20f, 0.75f, 0.25f, 1.0f) :
                                  ImVec4(0.95f, 0.25f, 0.2f, 1.0f),
            "%s",
            test.result.matched ? "OK" : "DIFF");
    }
    ImGui::TextDisabled(
        "%s  %lld ms",
        test.kind.c_str(),
        static_cast<long long>(test.result.elapsed_ms));

    if (!test.result.error.empty()) {
        ImGui::SeparatorText("Error");
        ImGui::TextWrapped("%s", test.result.error.c_str());
    }

    const CaseSnapshot<Input, Output>& snapshot = test.snapshot;
    const bool has_typed_output = snapshot.reference && snapshot.target;
    const bool has_persisted_output = snapshot.reference_text && snapshot.target_text;
    if (!snapshot.valid || (!has_typed_output && !has_persisted_output)) {
        ImGui::Separator();
        ImGui::TextDisabled("No input/output snapshot is available for this test.");
        if (ImGui::Button("Close")) {
            state.invocation_test_comparison_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    if (!snapshot.source.empty()) {
        ImGui::TextDisabled("%s", snapshot.source.c_str());
    }
    if (!snapshot.args.empty()) {
        ImGui::TextDisabled("args: %s", snapshot.args.c_str());
    }

    ImGui::SeparatorText("Input");
    ImGui::PushID("comparison_input");
    draw_text_box(snapshot.raw, ImVec2(760.0f, 150.0f));
    ImGui::PopID();

    const std::string answer = has_typed_output
        ? format_output_text(cfg, *snapshot.reference)
        : *snapshot.reference_text;
    const std::string output = has_typed_output
        ? format_output_text(cfg, *snapshot.target)
        : *snapshot.target_text;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float box_w = (760.0f - style.ItemSpacing.x) * 0.5f;

    ImGui::SeparatorText("Answer / Output");
    ImGui::BeginGroup();
    ImGui::TextUnformatted(snapshot.expected_output_raw ? "Answer (expected)" : "Answer (reference)");
    ImGui::PushID("comparison_answer");
    draw_text_box(answer, ImVec2(box_w, 260.0f));
    ImGui::PopID();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Output");
    ImGui::PushID("comparison_output");
    draw_text_box(output, ImVec2(box_w, 260.0f));
    ImGui::PopID();
    ImGui::EndGroup();

    if (ImGui::Button("Close")) {
        state.invocation_test_comparison_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

template <typename Input, typename Output>
void draw_invocation_record_modal(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state) {
    if (state.invocation_record_open) {
        ImGui::OpenPopup("Judging Record");
    }

    if (!ImGui::BeginPopupModal(
            "Judging Record",
            &state.invocation_record_open,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto found = std::find_if(
        state.invocation_history.begin(),
        state.invocation_history.end(),
        [&](const InvocationRecord<Input, Output>& record) {
            return record.id == state.opened_invocation_record_id;
        });

    if (found == state.invocation_history.end()) {
        ImGui::TextUnformatted("No invocation record selected.");
        if (ImGui::Button("Close")) {
            state.invocation_record_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    const InvocationRecord<Input, Output>& record = *found;
    ImGui::Text("%s", record.created_at.c_str());
    ImGui::TextDisabled("invocation #%lld", static_cast<long long>(record.id));
    ImGui::Separator();
    ImGui::Text("scope: %s", record.scope.c_str());
    ImGui::Text("tests: %s", record.tests.c_str());
    ImGui::TextUnformatted("status:");
    ImGui::SameLine();
    draw_status_text(record.status);
    ImGui::PushID(static_cast<int>(record.id));
    draw_invocation_progress_bar(
        "##judging_record_progress",
        record.progress_done,
        record.progress_total);
    ImGui::PopID();

    ImGui::SeparatorText("Result");
    ImGui::Text("%s", result_summary(record.result).c_str());
    if (record.result.elapsed_sec > 0.0) {
        ImGui::TextDisabled("elapsed: %.3f sec", record.result.elapsed_sec);
    }
    if (!record.result.archived_cases.empty()) {
        ImGui::TextDisabled("archived failures:");
        for (const std::filesystem::path& path : record.result.archived_cases) {
            ImGui::BulletText("%s", path.filename().string().c_str());
        }
    }

    ImGui::SeparatorText("Tests");
    if (record.test_results.empty()) {
        ImGui::TextDisabled("No per-test result rows stored for this invocation.");
    } else if (ImGui::BeginTable(
                   "judging_record_results",
                   3,
                   ImGuiTableFlags_Borders |
                       ImGuiTableFlags_RowBg |
                       ImGuiTableFlags_Resizable |
                       ImGuiTableFlags_ScrollY,
                   ImVec2(720.0f, 260.0f))) {
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < record.test_results.size(); ++i) {
            const InvocationTestRecord<Input, Output>& test = record.test_results[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(test.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                open_invocation_test_comparison(state, record.id, i);
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(test.kind.c_str());
            ImGui::TableSetColumnIndex(2);
            draw_invocation_test_result(test.result);
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Close")) {
        state.invocation_record_open = false;
        ImGui::CloseCurrentPopup();
    }

    draw_invocation_test_comparison_modal(cfg, state);

    ImGui::EndPopup();
}

template <typename Input, typename Output>
void draw_invocation_history(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    ImGui::SeparatorText("Invocations");
    ImGui::TextDisabled("history: %s", invocation_history_path(cfg).string().c_str());

    if (state.invocation_history.empty()) {
        ImGui::TextDisabled("No invocations in this GUI session.");
        return;
    }

    std::optional<size_t> remove_index;
    std::optional<size_t> rejudge_index;

    if (ImGui::BeginTable(
            "invocation_history_table",
            6,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY,
            ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Invocation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tests", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < state.invocation_history.size(); ++i) {
            InvocationRecord<Input, Output>& record = state.invocation_history[i];
            ImGui::TableNextRow();
            if (record.result.fail > 0) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(90, 42, 42, 120));
            }

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(record.id));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", record.created_at.c_str());
            ImGui::TextDisabled("%s", record.scope.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", record.tests.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::PushID(static_cast<int>(record.id));
            draw_invocation_progress_bar(
                "##invocation_progress",
                record.progress_done,
                record.progress_total);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(4);
            draw_status_text(record.status);
            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(static_cast<int>(record.id));
            if (icon_button("i", "View record")) {
                open_invocation_record(state, record.id);
            }
            if (!record.running) {
                ImGui::SameLine();
                if (icon_button("X", "Remove")) {
                    remove_index = i;
                }
                ImGui::SameLine();
                if (icon_button("~", "Rejudge")) {
                    rejudge_index = i;
                }
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("Running");
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (remove_index) {
        state.invocation_history.erase(state.invocation_history.begin() + static_cast<std::ptrdiff_t>(*remove_index));
        try {
            save_invocation_history(cfg, state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }

    if (rejudge_index && *rejudge_index < state.invocation_history.size()) {
        InvocationRecord<Input, Output> record = state.invocation_history[*rejudge_index];
        try {
            if (record.kind == InvocationRecord<Input, Output>::Kind::Stress) {
                auto found = std::find_if(
                    state.stress_entries.begin(),
                    state.stress_entries.end(),
                    [&](const ScriptEntry* entry) { return entry->name == record.stress_name; });
                if (found == state.stress_entries.end()) {
                    throw std::runtime_error("checker: stress entry not found: " + record.stress_name);
                }
                state.selected_stress = static_cast<int>(std::distance(state.stress_entries.begin(), found));
                reset_stress_run(state);
                bool keep_running = true;
                while (keep_running) {
                    keep_running = run_one_stress_step(cfg, state);
                }
                add_invocation_record<Input, Output>(
                    state,
                    InvocationRecord<Input, Output>::Kind::Stress,
                    "stress " + record.stress_name,
                    std::to_string(state.stress_result.invocations) + " generated",
                    state.stress_result,
                    state.current,
                    record.stress_name);
                save_invocation_history(cfg, state);
            } else {
                state.selected_invocation_cases.assign(
                    state.case_files.size() + state.case_entries.size(), 0);
                for (const std::filesystem::path& path : record.case_paths) {
                    auto found = std::find(state.case_files.begin(), state.case_files.end(), path);
                    if (found != state.case_files.end()) {
                        state.selected_invocation_cases[static_cast<size_t>(std::distance(state.case_files.begin(), found))] = 1;
                    }
                }
                for (const std::string& name : record.case_entry_names) {
                    auto found = std::find_if(
                        state.case_entries.begin(),
                        state.case_entries.end(),
                        [&](const ScriptEntry* entry) { return entry->name == name; });
                    if (found != state.case_entries.end()) {
                        const size_t index = state.case_files.size() +
                            static_cast<size_t>(std::distance(state.case_entries.begin(), found));
                        state.selected_invocation_cases[index] = 1;
                    }
                }
                run_selected_case_files(cfg, state);
            }
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
}

template <typename Input, typename Output>
void draw_stress_tab(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    const size_t invocation_test_count = state.case_files.size() + state.case_entries.size();
    if (state.selected_invocation_cases.size() != invocation_test_count) {
        state.selected_invocation_cases.assign(invocation_test_count, 1);
    }

    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float tests_h = std::max(260.0f, avail_h * 0.52f);

    if (state.stress_running) {
        if (state.stress_invocation_record_id == 0 && !state.stress_entries.empty()) {
            const ScriptEntry& entry = *state.stress_entries[static_cast<size_t>(state.selected_stress)];
            state.stress_invocation_record_id = add_running_invocation_record<Input, Output>(
                state,
                InvocationRecord<Input, Output>::Kind::Stress,
                "stress " + entry.name,
                std::to_string(entry.count) + " generated",
                entry.count,
                entry.name);
        }

        const int steps = 10;
        for (int step = 0; step < steps && state.stress_running; ++step) {
            try {
                const bool keep_running = run_one_stress_step(cfg, state);
                state.stress_running = keep_running;
                if (InvocationRecord<Input, Output>* record =
                        find_invocation_record(state, state.stress_invocation_record_id)) {
                    const ScriptEntry& entry = *state.stress_entries[static_cast<size_t>(state.selected_stress)];
                    record->progress_done = state.stress_index;
                    record->progress_total = entry.count;
                    record->result = state.stress_result;
                    record->snapshot = state.current;
                    if (!keep_running) {
                        record->running = false;
                        record->status = result_status(state.stress_result);
                        record->tests = std::to_string(state.stress_result.invocations) + " generated";
                        try {
                            save_invocation_history(cfg, state);
                        } catch (const std::exception& ex) {
                            state.status = ex.what();
                        }
                    }
                }
            } catch (const std::exception& ex) {
                state.stress_running = false;
                state.status = ex.what();
                if (InvocationRecord<Input, Output>* record =
                        find_invocation_record(state, state.stress_invocation_record_id)) {
                    record->running = false;
                    record->status = "FAILED";
                    record->result = state.stress_result;
                    record->snapshot = state.current;
                    try {
                        save_invocation_history(cfg, state);
                    } catch (const std::exception& save_ex) {
                        state.status = save_ex.what();
                    }
                }
            }
        }
    }

    if (state.selected_invocation_running) {
        const int steps = 4;
        for (int step = 0; step < steps && state.selected_invocation_running; ++step) {
            run_one_selected_case_step(cfg, state);
        }
    }

    ImGui::BeginChild("invocation_tests_panel", ImVec2(0, tests_h), false);
    ImGui::TextUnformatted("Tests to run:");
    ImGui::SameLine();
    if (icon_button("*##invoke_select_all", "Select all")) {
        std::fill(state.selected_invocation_cases.begin(), state.selected_invocation_cases.end(), 1);
    }
    ImGui::SameLine();
    if (icon_button("0##invoke_select_none", "Select none")) {
        std::fill(state.selected_invocation_cases.begin(), state.selected_invocation_cases.end(), 0);
    }
    ImGui::SameLine();
    if (!state.selected_invocation_running && icon_button("~##invoke_refresh", "Refresh")) {
        reload_case_files(cfg, state);
        state.selected_invocation_cases.assign(state.case_files.size() + state.case_entries.size(), 1);
    }
    ImGui::SameLine();
    if (state.selected_invocation_running) {
        ImGui::TextDisabled("Running");
    } else if (icon_button(">##invoke_run_selected", "Run selected tests")) {
        start_selected_case_run(state);
    }

    if (ImGui::BeginTable(
            "invocation_tests_table",
            3,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY,
            ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < state.case_files.size(); ++i) {
            const std::filesystem::path& path = state.case_files[i];
            std::string kind = "input";
            try {
                checker::detail::CaseFileData data =
                    checker::detail::parse_case_file(checker::detail::read_file(path));
                if (data.expected_output) {
                    kind = "manual";
                } else {
                    for (const ScriptEntry* entry : state.case_entries) {
                        if (path.stem().string() == entry->name) {
                            kind = "gen file";
                            break;
                        }
                    }
                }
            } catch (const std::exception&) {
                kind = "error";
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = state.selected_invocation_cases[i] != 0;
            if (ImGui::Checkbox(("##invoke_case_" + std::to_string(i)).c_str(), &selected)) {
                state.selected_invocation_cases[i] = selected ? 1 : 0;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu  %s", i + 1, path.filename().string().c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(kind.c_str());
        }

        const size_t entry_offset = state.case_files.size();
        for (size_t i = 0; i < state.case_entries.size(); ++i) {
            const size_t row = entry_offset + i;
            const ScriptEntry& entry = *state.case_entries[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = state.selected_invocation_cases[row] != 0;
            if (ImGui::Checkbox(("##invoke_entry_" + std::to_string(i)).c_str(), &selected)) {
                state.selected_invocation_cases[row] = selected ? 1 : 0;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu  %s", row + 1, entry.name.c_str());
            ImGui::TextDisabled("%s", entry_command(entry).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted("script");
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!state.status.empty()) {
        ImGui::TextDisabled("%s", state.status.c_str());
    }
    draw_result_counts(state.stress_result);

    draw_invocation_history(cfg, state);
}

struct GeneratedScriptCase {
    std::string raw;
    std::vector<std::string> args;
};

template <typename Input, typename Output>
GeneratedScriptCase generate_script_case(
    const StressConfig<Input, Output>& cfg,
    const ScriptEntry& entry) {
    const char* generator_path = checker::detail::resolve_generator(cfg.gen_resolver, entry.generator);
    std::mt19937 rng(0);
    GeneratedScriptCase generated;
    generated.args = resolve_args(entry, rng);
    generated.raw = checker::detail::run_generator(
        generator_path,
        generated.args,
        cfg.subprocess_timeout_sec);
    return generated;
}

template <typename Input, typename Output>
void close_test_item_modal(GuiState<Input, Output>& state) {
    state.test_item_open = false;
    ImGui::CloseCurrentPopup();
}

inline void next_modal_action(bool& has_previous_action) {
    if (has_previous_action) {
        ImGui::SameLine();
    }
    has_previous_action = true;
}

template <typename Input, typename Output>
void draw_file_test_item_content(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    const std::filesystem::path& path) {
    const std::string label = path.filename().string();
    std::string raw;
    checker::detail::CaseFileData data;
    bool raw_loaded = false;
    bool parsed = false;
    try {
        raw = checker::detail::read_file(path);
        raw_loaded = true;
        data = checker::detail::parse_case_file(raw);
        parsed = true;
    } catch (const std::exception& ex) {
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "%s", ex.what());
    }

    ImGui::Text("%s", label.c_str());
    ImGui::TextDisabled("%s", path.string().c_str());
    ImGui::SeparatorText("Content");
    draw_text_box(raw_loaded ? preview_block(raw, 24, 5000) : "<unavailable>", ImVec2(760.0f, 220.0f));

    bool has_action = false;
    if (parsed) {
        next_modal_action(has_action);
        if (icon_button("E##file_edit", "Edit")) {
            open_manual_editor(state, label, data.input, data.expected_output.value_or(""));
            close_test_item_modal(state);
        }
    }
    if (raw_loaded) {
        next_modal_action(has_action);
        if (icon_button("++##file_duplicate", "Duplicate")) {
            try {
                duplicate_case_file(cfg, state, label, raw);
                close_test_item_modal(state);
            } catch (const std::exception& ex) {
                state.status = ex.what();
            }
        }
    }
    if (parsed) {
        next_modal_action(has_action);
        if (icon_button("=##file_example", "Example")) {
            try {
                state.current = evaluate_case_file<Input, Output>(cfg, label, path.string(), raw);
                const std::string expected = format_output_text(cfg, *state.current.reference);
                open_manual_editor(state, label, state.current.raw, expected);
                close_test_item_modal(state);
            } catch (const std::exception& ex) {
                state.status = ex.what();
            }
        }

        next_modal_action(has_action);
        if (icon_button("?##file_preview", "Preview")) {
            try {
                state.current = evaluate_case_file<Input, Output>(cfg, label, path.string(), raw);
                open_detail(state, state.current);
                close_test_item_modal(state);
            } catch (const std::exception& ex) {
                state.status = ex.what();
            }
        }
    }
    if (raw_loaded) {
        next_modal_action(has_action);
        if (icon_button("X##file_delete", "Delete")) {
            try {
                std::filesystem::remove(path);
                reload_case_files(cfg, state);
                state.selected_test_rows.assign(
                    state.case_files.size() + state.case_entries.size(),
                    0);
                state.status = "Deleted " + label;
                close_test_item_modal(state);
            } catch (const std::exception& ex) {
                state.status = ex.what();
            }
        }
    }
}

template <typename Input, typename Output>
void draw_script_test_item_content(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    const ScriptEntry& entry) {
    const std::string command = entry_command(entry);
    ImGui::Text("%s", entry.name.c_str());
    ImGui::TextDisabled("scripted case");
    ImGui::SeparatorText("Command");
    draw_text_box(command, ImVec2(760.0f, 96.0f));

    bool has_action = false;
    next_modal_action(has_action);
    if (icon_button("E##script_edit", "Edit script")) {
        state.script_editor_focus = true;
        close_test_item_modal(state);
    }

    next_modal_action(has_action);
    if (icon_button(">##script_build", "Build")) {
        try {
            const GeneratedScriptCase generated = generate_script_case(cfg, entry);
            checker::detail::write_case(cfg.cases_dir, entry.name, generated.raw);
            reload_case_files(cfg, state);
            state.status = "Generated " + entry.name + ".txt";
            close_test_item_modal(state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }

    next_modal_action(has_action);
    if (icon_button("++##script_duplicate", "Duplicate")) {
        try {
            duplicate_script_case(cfg, state, entry);
            close_test_item_modal(state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }

    next_modal_action(has_action);
    if (icon_button("=##script_example", "Example")) {
        try {
            const GeneratedScriptCase generated = generate_script_case(cfg, entry);
            state.current = evaluate_raw<Input, Output>(cfg, entry.name, entry.generator, generated.raw);
            state.current.args = join_args(generated.args);
            const std::string expected = format_output_text(cfg, *state.current.reference);
            open_manual_editor(state, entry.name + ".txt", state.current.raw, expected);
            close_test_item_modal(state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }

    next_modal_action(has_action);
    if (icon_button("?##script_preview", "Preview")) {
        try {
            const GeneratedScriptCase generated = generate_script_case(cfg, entry);
            state.current = evaluate_raw<Input, Output>(cfg, entry.name, entry.generator, generated.raw);
            state.current.args = join_args(generated.args);
            open_detail(state, state.current);
            close_test_item_modal(state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
}

template <typename Input, typename Output>
void draw_test_item_modal(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.test_item_open) {
        ImGui::OpenPopup("Test Item");
    }

    if (!ImGui::BeginPopupModal("Test Item", &state.test_item_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const size_t file_count = state.case_files.size();
    const size_t row = state.selected_test_item_row;
    if (row < file_count) {
        draw_file_test_item_content(cfg, state, state.case_files[row]);
    } else if (row - file_count < state.case_entries.size()) {
        draw_script_test_item_content(cfg, state, *state.case_entries[row - file_count]);
    } else {
        ImGui::TextUnformatted("No test item selected.");
    }

    if (!state.status.empty()) {
        ImGui::TextDisabled("%s", state.status.c_str());
    }
    if (ImGui::Button("Close")) {
        close_test_item_modal(state);
    }

    ImGui::EndPopup();
}

template <typename Input, typename Output>
size_t tests_row_count(const GuiState<Input, Output>& state) {
    return state.case_files.size() + state.case_entries.size();
}

template <typename Input, typename Output>
void sync_tests_selection(GuiState<Input, Output>& state) {
    const size_t row_count = tests_row_count(state);
    if (state.selected_test_rows.size() != row_count) {
        state.selected_test_rows.assign(row_count, 0);
    }
}

template <typename Input, typename Output>
void reset_tests_selection(GuiState<Input, Output>& state) {
    state.selected_test_rows.assign(tests_row_count(state), 0);
}

template <typename Input, typename Output>
std::optional<size_t> single_selected_case_file(const GuiState<Input, Output>& state) {
    std::optional<size_t> selected;
    for (size_t i = 0; i < state.case_files.size(); ++i) {
        if (i >= state.selected_test_rows.size() || !state.selected_test_rows[i]) {
            continue;
        }
        if (selected) {
            return std::nullopt;
        }
        selected = i;
    }
    return selected;
}

template <typename Input, typename Output>
void open_new_manual_test(GuiState<Input, Output>& state) {
    const std::string name = "manual_" + std::to_string(state.case_files.size() + 1) + ".txt";
    open_manual_editor(state, name, "", "");
}

template <typename Input, typename Output>
void draw_tests_header(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    ImGui::BeginChild("tests_header", ImVec2(0, 32), true);
    ImGui::TextColored(
        ImVec4(0.35f, 0.55f, 1.0f, 1.0f),
        "Tests (%zu)",
        tests_row_count(state));
    ImGui::SameLine();

    if (icon_button("X##bulk_delete", "Delete selected")) {
        try {
            int deleted = 0;
            for (int i = static_cast<int>(state.case_files.size()) - 1; i >= 0; --i) {
                if (state.selected_test_rows[static_cast<size_t>(i)]) {
                    std::filesystem::remove(state.case_files[static_cast<size_t>(i)]);
                    ++deleted;
                }
            }
            reload_case_files(cfg, state);
            reset_tests_selection(state);
            state.status = deleted == 0
                ? "No case files selected."
                : "Deleted " + std::to_string(deleted) + " case file(s).";
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (icon_button("R##rename_test", "Rename selected")) {
        const std::optional<size_t> selected = single_selected_case_file(state);
        if (selected) {
            state.rename_source = state.case_files[*selected];
            set_text_buffer(state.rename_case_name, state.rename_source.filename().string());
            state.rename_editor_open = true;
        } else {
            state.status = "Select exactly one case file to rename.";
        }
    }
    ImGui::SameLine();
    if (icon_button("+##create_test", "Create test")) {
        open_new_manual_test(state);
    }
    ImGui::EndChild();
}

template <typename Input, typename Output>
void draw_rename_test_modal(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.rename_editor_open) {
        ImGui::OpenPopup("Rename Test");
    }
    if (!ImGui::BeginPopupModal("Rename Test", &state.rename_editor_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("file", state.rename_case_name.data(), state.rename_case_name.size());
    if (ImGui::Button("Rename")) {
        try {
            const std::filesystem::path name = checked_case_name(state.rename_case_name.data());
            const std::filesystem::path destination = cfg.cases_dir / name;
            if (std::filesystem::exists(destination)) {
                throw std::runtime_error("checker: destination already exists");
            }
            std::filesystem::rename(state.rename_source, destination);
            reload_case_files(cfg, state);
            reset_tests_selection(state);
            state.status = "Renamed to " + name.filename().string();
            state.rename_editor_open = false;
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.rename_editor_open = false;
        ImGui::CloseCurrentPopup();
    }
    if (!state.status.empty()) {
        ImGui::TextDisabled("%s", state.status.c_str());
    }
    ImGui::EndPopup();
}

template <typename Input, typename Output>
void draw_script_editor_panel(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    ImGui::BeginChild("script_editor_panel", ImVec2(0, 190.0f), true);
    ImGui::TextUnformatted("gen.script");
    ImGui::SameLine();
    if (icon_button("S##save_script", "Save script")) {
        try {
            save_script_file(cfg, state);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (icon_button("~##reload_script", "Reload script")) {
        try {
            reload_script_file(cfg, state);
            state.status = "Reloaded " + cfg.script_path.filename().string();
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::TextDisabled("%s", cfg.script_path.string().c_str());
    if (state.script_editor_focus) {
        ImGui::SetKeyboardFocusHere();
        state.script_editor_focus = false;
    }
    ImGui::InputTextMultiline(
        "##gen_script_editor",
        state.script_editor_text.data(),
        state.script_editor_text.size(),
        ImVec2(-1.0f, 125.0f),
        ImGuiInputTextFlags_AllowTabInput);
    ImGui::EndChild();
}

template <typename Input, typename Output>
void draw_tests_toolbar(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    ImGui::Checkbox("Tests well-formed", &state.tests_well_formed);
    ImGui::Checkbox("Enable points", &state.enable_points);
    ImGui::Checkbox("Enable groups", &state.enable_groups);

    if (icon_button("?##preview_tests", "Preview tests")) {
        try {
            StressResult result = run_cases(cfg);
            state.status = "cases: ok=" + std::to_string(result.ok) +
                " fail=" + std::to_string(result.fail);
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (icon_button("+##add_test", "Add test")) {
        open_new_manual_test(state);
    }
    ImGui::SameLine();
    if (icon_button("~##refresh_tests", "Refresh")) {
        try {
            reload_script_file(cfg, state);
            reload_case_files(cfg, state);
            state.status = "Reloaded tests.";
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    ImGui::SameLine();
    if (icon_button(">##rebuild_generated", "Rebuild generated")) {
        try {
            save_script_file(cfg, state);
            build_cases(cfg);
            reload_case_files(cfg, state);
            state.status = "Generated all scripted case entries.";
        } catch (const std::exception& ex) {
            state.status = ex.what();
        }
    }
    if (!state.status.empty()) {
        ImGui::TextDisabled("%s", state.status.c_str());
    }
}

template <typename Input, typename Output>
void draw_file_test_row(
    const StressConfig<Input, Output>& cfg,
    GuiState<Input, Output>& state,
    size_t row,
    const std::filesystem::path& path) {
    (void)cfg;
    const std::string label = path.filename().string();
    std::string raw;
    checker::detail::CaseFileData data;
    std::string desc = "input";
    std::string example;
    std::string content;
    bool loaded = false;
    try {
        raw = checker::detail::read_file(path);
        data = checker::detail::parse_case_file(raw);
        content = preview_case_data(data);
        loaded = true;
        if (data.expected_output) {
            desc = "manual";
            example = "Y";
        } else {
            for (const ScriptEntry* entry : state.case_entries) {
                if (path.stem().string() == entry->name) {
                    desc = "gen file";
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        content = ex.what();
        desc = "error";
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%zu", row + 1);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(content.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemClicked()) {
        open_test_item(state, row);
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%zu", raw.size());
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(desc.c_str());
    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(example.c_str());
    ImGui::TableSetColumnIndex(5);
    ImGui::PushID(static_cast<int>(row));
    if (loaded && icon_button("E", "Edit")) {
        open_manual_editor(state, label, data.input, data.expected_output.value_or(""));
    }
    ImGui::PopID();
    ImGui::TableSetColumnIndex(6);
    bool selected = state.selected_test_rows[row] != 0;
    if (ImGui::Checkbox(("##select_file_" + std::to_string(row)).c_str(), &selected)) {
        state.selected_test_rows[row] = selected ? 1 : 0;
    }
}

template <typename Input, typename Output>
void draw_script_test_row(
    GuiState<Input, Output>& state,
    size_t row,
    size_t script_index,
    const ScriptEntry& entry) {
    const std::string command = entry_command(entry);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%zu", row + 1);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 1.0f, 1.0f), "%s", command.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemClicked()) {
        open_test_item(state, row);
    }
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted("generator");
    ImGui::TableSetColumnIndex(5);
    ImGui::PushID(static_cast<int>(row));
    if (icon_button("E", "Edit script")) {
        state.script_editor_focus = true;
    }
    ImGui::PopID();
    ImGui::TableSetColumnIndex(6);
    bool selected = state.selected_test_rows[row] != 0;
    if (ImGui::Checkbox(("##select_gen_" + std::to_string(script_index)).c_str(), &selected)) {
        state.selected_test_rows[row] = selected ? 1 : 0;
    }
}

template <typename Input, typename Output>
void draw_tests_table(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    sync_tests_selection(state);

    if (!ImGui::BeginTable(
            "tests_table",
            7,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 38.0f);
    ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Desc", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Example", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < state.case_files.size(); ++i) {
        draw_file_test_row(cfg, state, i, state.case_files[i]);
    }

    const size_t file_count = state.case_files.size();
    for (size_t i = 0; i < state.case_entries.size(); ++i) {
        draw_script_test_row(state, file_count + i, i, *state.case_entries[i]);
    }

    ImGui::EndTable();
}

template <typename Input, typename Output>
void draw_cases_tab(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    if (state.manual_case_name[0] == '\0') {
        std::snprintf(state.manual_case_name.data(), state.manual_case_name.size(), "%s", "manual.txt");
    }

    sync_tests_selection(state);
    draw_tests_header(cfg, state);
    draw_rename_test_modal(cfg, state);
    draw_script_editor_panel(cfg, state);
    draw_tests_toolbar(cfg, state);
    draw_tests_table(cfg, state);
}

template <typename Input, typename Output>
void draw_files_tab(const StressConfig<Input, Output>& cfg, GuiState<Input, Output>& state) {
    std::vector<FileViewEntry> files;
    for (const std::string& name : generator_names(state)) {
        const char* source_path = cfg.gen_source_resolver ? cfg.gen_source_resolver(name) : nullptr;
        files.push_back(FileViewEntry{
            name,
            "generator",
            source_path != nullptr && source_path[0] != '\0'
                ? std::filesystem::path(source_path)
                : std::filesystem::path{},
        });
    }

    if (state.selected_file >= static_cast<int>(files.size())) {
        state.selected_file = files.empty() ? 0 : static_cast<int>(files.size()) - 1;
    }

    ImGui::BeginChild("files_list", ImVec2(320, 0), true);
    ImGui::Text("Files (%zu)", files.size());
    ImGui::Separator();
    if (files.empty()) {
        ImGui::TextDisabled("No generator files.");
    }
    for (size_t i = 0; i < files.size(); ++i) {
        const FileViewEntry& file = files[i];
        const bool selected = state.selected_file == static_cast<int>(i);
        if (ImGui::Selectable(file.label.c_str(), selected)) {
            state.selected_file = static_cast<int>(i);
        }
        ImGui::TextDisabled("%s", file.kind.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("file_viewer", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (files.empty()) {
        ImGui::TextUnformatted("No file selected.");
        ImGui::EndChild();
        return;
    }

    const FileViewEntry& file = files[static_cast<size_t>(state.selected_file)];
    ImGui::Text("%s", file.label.c_str());
    ImGui::TextDisabled("%s", file.kind.c_str());
    if (!file.path.empty()) {
        ImGui::TextDisabled("%s", file.path.string().c_str());
    }
    if (file.kind == "generator") {
        const char* executable_path = cfg.gen_resolver ? cfg.gen_resolver(file.label) : nullptr;
        if (executable_path != nullptr && executable_path[0] != '\0') {
            ImGui::TextDisabled("executable: %s", executable_path);
        }
    }
    ImGui::Separator();

    if (file.path.empty()) {
        ImGui::TextWrapped("No source path is available for this file.");
    } else {
        try {
            std::string code = checker::detail::read_file(file.path);
            if (file.kind == "generator") {
                draw_cpp_highlighted_code(code);
            } else {
                ImGui::TextUnformatted(code.data(), code.data() + code.size());
            }
        } catch (const std::exception& ex) {
            ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "%s", ex.what());
        }
    }
    ImGui::EndChild();
}

template <typename Input, typename Output>
int run_window(
    const StressConfig<Input, Output>& cfg,
    const LaunchOptions& options = {}) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        throw std::runtime_error("checker: failed to initialize GLFW");
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(
        cfg.gui.window_w,
        cfg.gui.window_h,
        cfg.gui.window_title.c_str(),
        nullptr,
        nullptr);
    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("checker: failed to create GLFW window");
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    GuiState<Input, Output> state;
    int selected_tab = options.tab == StartTab::Stress ? 1 : options.tab == StartTab::Files ? 2 : 0;

    try {
        initialize_state(cfg, options, state);
        if (options.auto_start && options.tab == StartTab::Build) {
            build_cases(cfg);
            reload_case_files(cfg, state);
            state.status = "Generated all scripted case entries.";
        } else if (options.auto_start && options.tab == StartTab::Cases) {
            StressResult result = run_cases(cfg);
            state.status = "cases: ok=" + std::to_string(result.ok) +
                " fail=" + std::to_string(result.fail);
        }
    } catch (const std::exception& ex) {
        state.status = ex.what();
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("checker", nullptr, flags);

        ImGui::BeginChild("checker_sidebar", ImVec2(170, 0), true);
        if (ImGui::Selectable("Tests", selected_tab == 0)) {
            selected_tab = 0;
        }
        if (ImGui::Selectable("Invocations", selected_tab == 1)) {
            selected_tab = 1;
        }
        if (ImGui::Selectable("Files", selected_tab == 2)) {
            selected_tab = 2;
        }
        ImGui::Separator();
        ImGui::TextDisabled("script");
        ImGui::TextWrapped("%s", cfg.script_path.string().c_str());
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("checker_main", ImVec2(0, 0), false);
        if (selected_tab == 0) {
            draw_cases_tab(cfg, state);
        } else if (selected_tab == 1) {
            draw_stress_tab(cfg, state);
        } else {
            draw_files_tab(cfg, state);
        }
        ImGui::EndChild();

        draw_invocation_record_modal(cfg, state);
        draw_test_item_modal(cfg, state);
        draw_case_detail_modal(cfg, state);
        draw_manual_editor_modal(cfg, state);

        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    try {
        const std::string editor_text = state.script_editor_text.data();
        if (!cfg.script_path.empty() && checker::detail::read_file(cfg.script_path) != editor_text) {
            save_script_file(cfg, state);
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "checker: failed to save gen.script on shutdown: %s\n", ex.what());
    }

    try {
        save_invocation_history(cfg, state);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "checker: failed to save invocation history: %s\n", ex.what());
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace detail

template <typename Input, typename Output>
int run_window(const StressConfig<Input, Output>& cfg, const LaunchOptions& options = {}) {
    return detail::run_window(cfg, options);
}

} // namespace checker::gui

#endif // CHECKER_GUI_H
