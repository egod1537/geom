#ifndef CHECKER_RESULT_H
#define CHECKER_RESULT_H

#include <cstdint>
#include <filesystem>
#include <vector>

namespace checker {

struct StressResult {
    int64_t invocations = 0;
    int64_t ok = 0;
    int64_t fail = 0;
    double elapsed_sec = 0.0;
    std::vector<std::filesystem::path> archived_cases;

    int exit_code() const {
        return fail == 0 ? 0 : 1;
    }
};

} // namespace checker

#endif // CHECKER_RESULT_H
