#include "testlib.h"

#include <algorithm>
#include <iostream>

int main(int argc, char* argv[]) {
    registerGen(argc, argv, 1);

    int n = opt<int>("n", 10);
    int max_value = opt<int>("max", 10);
    ensuref(n >= 0, "n must be non-negative");
    ensuref(max_value >= 0, "max must be non-negative");

    std::cout << n << '\n';
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            std::cout << ' ';
        }
        std::cout << rnd.next(0, max_value);
    }
    std::cout << '\n';
    return 0;
}
