#include "checker/detail/runtime.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace checker::detail {
namespace {

bool contains_failed_component(const fs::path& path) {
    for (const fs::path& part : path) {
        if (part == "failed") {
            return true;
        }
    }
    return false;
}

std::string_view trim_marker(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) {
        line.remove_prefix(1);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

bool starts_with_input_marker(std::string_view raw_bytes) {
    size_t pos = 0;
    while (pos < raw_bytes.size()) {
        size_t line_end = raw_bytes.find('\n', pos);
        if (line_end == std::string_view::npos) {
            line_end = raw_bytes.size();
        }
        const std::string_view marker = trim_marker(raw_bytes.substr(pos, line_end - pos));
        if (!marker.empty()) {
            return marker == "[input]";
        }
        pos = line_end < raw_bytes.size() ? line_end + 1 : line_end;
    }
    return false;
}

} // namespace

std::vector<fs::path> glob_cases(const fs::path& cases_dir) {
    std::vector<fs::path> cases;
    if (!fs::exists(cases_dir)) {
        return cases;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(cases_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (contains_failed_component(entry.path())) {
            continue;
        }
        if (entry.path().extension() == ".txt") {
            cases.push_back(entry.path());
        }
    }

    std::sort(cases.begin(), cases.end());
    return cases;
}

fs::path archive_failed(
    const fs::path& failed_dir,
    std::string_view entry_name,
    int64_t index,
    std::string_view raw_bytes) {
    if (failed_dir.empty()) {
        throw std::invalid_argument("checker: failed_dir is required to archive failures");
    }

    fs::create_directories(failed_dir);
    fs::path output = failed_dir /
        ("auto_" + std::string(entry_name) + "_" + std::to_string(index) + ".txt");
    std::ofstream file(output, std::ios::binary);
    if (!file) {
        throw std::runtime_error("checker: failed to open archive path: " + output.string());
    }
    file.write(raw_bytes.data(), static_cast<std::streamsize>(raw_bytes.size()));
    if (!file) {
        throw std::runtime_error("checker: failed to write archive path: " + output.string());
    }
    return output;
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("checker: failed to open case: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

CaseFileData parse_case_file(std::string_view raw_bytes) {
    CaseFileData data;
    if (!starts_with_input_marker(raw_bytes)) {
        data.input = std::string(raw_bytes);
        return data;
    }

    bool saw_marker = false;
    bool in_output = false;

    size_t pos = 0;
    while (pos < raw_bytes.size()) {
        size_t line_end = raw_bytes.find('\n', pos);
        if (line_end == std::string_view::npos) {
            line_end = raw_bytes.size();
        }
        const size_t next = line_end < raw_bytes.size() ? line_end + 1 : line_end;
        const std::string_view line = raw_bytes.substr(pos, line_end - pos);
        const std::string_view marker = trim_marker(line);

        if (marker == "[input]") {
            saw_marker = true;
            in_output = false;
        } else if (marker == "[output]") {
            saw_marker = true;
            in_output = true;
            if (!data.expected_output) {
                data.expected_output = std::string{};
            }
        } else {
            const std::string_view segment = raw_bytes.substr(pos, next - pos);
            if (in_output) {
                if (!data.expected_output) {
                    data.expected_output = std::string{};
                }
                data.expected_output->append(segment.data(), segment.size());
            } else {
                data.input.append(segment.data(), segment.size());
            }
        }

        pos = next;
    }

    if (!saw_marker) {
        data.input = std::string(raw_bytes);
        data.expected_output.reset();
    }
    return data;
}

std::string format_case_file(std::string_view input, std::string_view expected_output) {
    std::string bytes;
    bytes.reserve(input.size() + expected_output.size() + 20);
    bytes += "[input]\n";
    bytes.append(input.data(), input.size());
    if (!bytes.empty() && bytes.back() != '\n') {
        bytes += '\n';
    }
    bytes += "[output]\n";
    bytes.append(expected_output.data(), expected_output.size());
    if (!bytes.empty() && bytes.back() != '\n') {
        bytes += '\n';
    }
    return bytes;
}

void write_case(const fs::path& cases_dir, std::string_view name, std::string_view raw_bytes) {
    fs::create_directories(cases_dir);
    fs::path output = cases_dir / (std::string(name) + ".txt");
    std::ofstream file(output, std::ios::binary);
    if (!file) {
        throw std::runtime_error("checker: failed to open generated case: " + output.string());
    }
    file.write(raw_bytes.data(), static_cast<std::streamsize>(raw_bytes.size()));
    if (!file) {
        throw std::runtime_error("checker: failed to write generated case: " + output.string());
    }
}

} // namespace checker::detail
