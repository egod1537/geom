#include <checker/dispatch.h>
#include <checker/script.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<int> parse_values(std::string_view raw) {
    std::istringstream input{std::string(raw)};
    int n = 0;
    if (!(input >> n)) {
        throw std::runtime_error("missing n");
    }
    std::vector<int> values;
    for (int i = 0; i < n; ++i) {
        int value = 0;
        if (!(input >> value)) {
            throw std::runtime_error("missing value");
        }
        values.push_back(value);
    }
    return values;
}

std::vector<int> sorted_values(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values;
}

fs::path temp_root() {
    fs::path root = fs::temp_directory_path() /
        ("checker_unit_" + std::to_string(static_cast<long long>(getpid())));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << bytes;
}

void make_executable(const fs::path& path) {
    fs::permissions(path,
        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
            fs::perms::group_exec | fs::perms::group_read |
            fs::perms::others_exec | fs::perms::others_read,
        fs::perm_options::replace);
}

checker::StressConfig<std::vector<int>, std::vector<int>> base_config(
    const fs::path& root,
    const fs::path& generator) {
    checker::StressConfig<std::vector<int>, std::vector<int>> cfg;
    cfg.script_path = root / "gen.script";
    cfg.cases_dir = root / "cases";
    cfg.failed_dir = root / "cases" / "failed";
    cfg.gen_resolver = [gen = generator.string()](std::string_view name) -> const char* {
        if (name == "mock_gen") {
            return gen.c_str();
        }
        return nullptr;
    };
    cfg.parse = parse_values;
    cfg.parse_output = parse_values;
    cfg.reference = sorted_values;
    cfg.target = sorted_values;
    cfg.progress_every = 0;
    return cfg;
}

void test_script_parser() {
    checker::Script empty = checker::parse_script_string("  \n# comment\n");
    assert(empty.entries.empty());

    checker::Script script = checker::parse_script_string(
        "case basic gen --n [2..3] 1\n"
        "stress small 3 gen --n [1..5] --x [-2..2] [0..0]\n");
    assert(script.entries.size() == 2);
    assert(script.entries[0].kind == checker::ScriptEntry::Kind::Case);
    assert(script.entries[0].name == "basic");
    assert(script.entries[0].count == 1);
    assert(std::get<std::string>(script.entries[0].args[1]) == "[2..3]");
    assert(script.entries[1].kind == checker::ScriptEntry::Kind::Stress);
    assert(script.entries[1].name == "small");
    assert(script.entries[1].count == 3);

    std::mt19937 a(123);
    std::mt19937 b(123);
    std::vector<std::string> args_a = checker::resolve_args(script.entries[1], a);
    std::vector<std::string> args_b = checker::resolve_args(script.entries[1], b);
    assert(args_a == args_b);

    auto must_throw_with_line = [](std::string_view text) {
        try {
            (void)checker::parse_script_string(text);
        } catch (const std::runtime_error& ex) {
            assert(std::string(ex.what()).find("line ") != std::string::npos);
            return;
        }
        assert(false && "expected parse_script_string to throw");
    };

    must_throw_with_line("case only_name\n");
    must_throw_with_line("stress bad 1 gen [5..3]\n");
    must_throw_with_line("case dup gen 1\ncase dup gen 2\n");
}

} // namespace

int main() {
    test_script_parser();

    fs::path root = temp_root();
    fs::path generator = root / "mock_gen.sh";
    write_file(generator,
        "#!/bin/sh\n"
        "if [ \"$1\" = \"sleep\" ]; then sleep 20; fi\n"
        "printf '3\\n3 1 2\\n'\n");
    make_executable(generator);

    write_file(root / "gen.script",
        "case generated mock_gen case 1\n"
        "stress ok 3 mock_gen stress [1..10]\n"
        "stress timeout 1 sleep_gen 20\n");

    checker::StressConfig<std::vector<int>, std::vector<int>> cfg = base_config(root, generator);
    checker::StressResult ok = checker::run_one_stress(cfg, "ok");
    assert(ok.invocations == 3);
    assert(ok.ok == 3);
    assert(ok.fail == 0);
    assert(ok.exit_code() == 0);

    cfg.target = [](const std::vector<int>&) { return std::vector<int>{}; };
    cfg.stop_on_first_fail = true;
    checker::StressResult first_fail = checker::run_one_stress(cfg, "ok");
    assert(first_fail.invocations == 1);
    assert(first_fail.fail == 1);
    assert(first_fail.archived_cases.size() == 1);
    assert(fs::exists(first_fail.archived_cases[0]));

    cfg.stop_on_first_fail = false;
    cfg.max_fail = 2;
    checker::StressResult max_fail = checker::run_one_stress(cfg, "ok");
    assert(max_fail.invocations == 2);
    assert(max_fail.fail == 2);

    cfg = base_config(root, generator);
    cfg.reference = [](const std::vector<int>& values) {
        std::vector<int> output = values;
        std::sort(output.begin(), output.end());
        return output;
    };
    cfg.target = [](const std::vector<int>& values) {
        std::vector<int> output = values;
        std::reverse(output.begin(), output.end());
        return output;
    };
    cfg.normalize = [](std::vector<int> values) {
        std::sort(values.begin(), values.end());
        return values;
    };
    checker::StressResult normalized = checker::run_one_stress(cfg, "ok");
    assert(normalized.fail == 0);

    checker::build_cases(cfg);
    assert(fs::exists(root / "cases" / "generated.txt"));

    write_file(root / "cases" / "001_manual.txt", "2\n9 8\n");
    write_file(root / "cases" / "failed" / "auto_ignore.txt", "1\n0\n");
    cfg = base_config(root, generator);
    checker::StressResult cases = checker::run_cases(cfg);
    assert(cases.invocations == 2);
    assert(cases.fail == 0);

    fs::remove_all(root / "cases");
    write_file(root / "cases" / "manual_expected.txt",
        "[input]\n"
        "2\n9 8\n"
        "[output]\n"
        "2\n8 9\n");
    cfg = base_config(root, generator);
    cfg.reference = [](const std::vector<int>&) { return std::vector<int>{42}; };
    checker::StressResult manual_expected = checker::run_cases(cfg);
    assert(manual_expected.invocations == 1);
    assert(manual_expected.fail == 0);

    cfg.subprocess_timeout_sec = 1;
    cfg.gen_resolver = [gen = generator.string()](std::string_view name) -> const char* {
        if (name == "mock_gen") {
            return gen.c_str();
        }
        if (name == "sleep_gen") {
            return "/bin/sleep";
        }
        return nullptr;
    };
    bool timed_out = false;
    try {
        (void)checker::run_one_stress(cfg, "timeout");
    } catch (const std::runtime_error& ex) {
        timed_out = std::string(ex.what()).find("timed out") != std::string::npos;
    }
    assert(timed_out);

    write_file(root / "gen.script",
        "case generated mock_gen case 1\n"
        "stress ok 2 mock_gen stress [1..10]\n");
    cfg = base_config(root, generator);

    const char* argv_help[] = {"unit", "--help"};
    assert(checker::run(cfg, 2, const_cast<char**>(argv_help)) == 0);
    const char* argv_bad[] = {"unit", "wat"};
    assert(checker::run(cfg, 2, const_cast<char**>(argv_bad)) == 2);
    const char* argv_build[] = {"unit", "--headless", "build"};
    assert(checker::run(cfg, 3, const_cast<char**>(argv_build)) == 0);
    const char* argv_cases[] = {"unit", "--headless", "cases"};
    assert(checker::run(cfg, 3, const_cast<char**>(argv_cases)) == 0);
    const char* argv_stress_name[] = {"unit", "--headless", "stress", "ok"};
    assert(checker::run(cfg, 4, const_cast<char**>(argv_stress_name)) == 0);
    const char* argv_stress_all[] = {"unit", "--headless", "stress"};
    assert(checker::run(cfg, 3, const_cast<char**>(argv_stress_all)) == 0);

    fs::remove_all(root);
    return 0;
}
