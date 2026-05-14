#ifndef CHECKER_EXAMPLES_SORT_UNIQ_HPP
#define CHECKER_EXAMPLES_SORT_UNIQ_HPP

#include <algorithm>
#include <vector>

namespace sort_uniq {

inline std::vector<int> compute(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

} // namespace sort_uniq

#endif // CHECKER_EXAMPLES_SORT_UNIQ_HPP
