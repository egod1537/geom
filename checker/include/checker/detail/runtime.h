#ifndef CHECKER_DETAIL_RUNTIME_H
#define CHECKER_DETAIL_RUNTIME_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace checker::detail {

struct SubprocessResult {
    int exit_code = -1;
    bool signaled = false;
    bool timed_out = false;
    int signal = 0;
    std::string stdout_bytes;
    std::string stderr_bytes;
};

struct CaseFileData {
    std::string input;
    std::optional<std::string> expected_output;
};

SubprocessResult run_subprocess(const std::vector<std::string>& argv, int timeout_sec);

std::string run_generator(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    int timeout_sec);

std::vector<std::filesystem::path> glob_cases(const std::filesystem::path& cases_dir);

std::filesystem::path archive_failed(
    const std::filesystem::path& failed_dir,
    std::string_view entry_name,
    int64_t index,
    std::string_view raw_bytes);

std::string read_file(const std::filesystem::path& path);
CaseFileData parse_case_file(std::string_view raw_bytes);
std::string format_case_file(std::string_view input, std::string_view expected_output);
void write_case(
    const std::filesystem::path& cases_dir,
    std::string_view name,
    std::string_view raw_bytes);

} // namespace checker::detail

#endif // CHECKER_DETAIL_RUNTIME_H
