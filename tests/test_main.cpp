#include "test_framework.hpp"

#include <exception>
#include <iostream>

int main() {
    std::size_t failed = 0;

    for (const auto& test : solo_rig::test::Registry()) {
        try {
            test.execute();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        }
    }

    std::cout << "Executed " << solo_rig::test::Registry().size() << " test(s).\n";
    return failed == 0 ? 0 : 1;
}
