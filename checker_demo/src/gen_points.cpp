#include "testlib.h"

#include <iostream>

int main(int argc, char* argv[]) {
    registerGen(argc, argv, 1);

    const int n = opt<int>("n", 10);
    const int max_abs = opt<int>("max", 100);

    std::cout << n << '\n';
    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            std::cout << ' ';
        }
        std::cout << rnd.next(-max_abs, max_abs);
    }
    std::cout << '\n';
}
