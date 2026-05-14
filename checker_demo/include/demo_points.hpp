#ifndef DEMO_POINTS_HPP
#define DEMO_POINTS_HPP

#include <cstdint>
#include <vector>

namespace demo_points {

struct Input {
    std::vector<int> values;
};

using Output = int64_t;

inline Output solve(const Input& input) {
    Output sum = 0;
    for (int value : input.values) {
        sum += value;
    }
    return sum;
}

} // namespace demo_points

#endif // DEMO_POINTS_HPP
