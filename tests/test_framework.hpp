#pragma once

#include <exception>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace solo_rig::test {

struct TestCase {
    std::string name;
    std::function<void()> execute;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

class Registrar {
public:
    Registrar(std::string name, std::function<void()> execute) {
        Registry().push_back({std::move(name), std::move(execute)});
    }
};

inline void Require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ':' << line << " requirement failed: " << expression;
        throw std::runtime_error(message.str());
    }
}

} // namespace solo_rig::test

#define SOLO_RIG_CONCATENATE_IMPL(left, right) left##right
#define SOLO_RIG_CONCATENATE(left, right) SOLO_RIG_CONCATENATE_IMPL(left, right)

#define TEST_CASE(name) \
    static void SOLO_RIG_CONCATENATE(TestFunction_, __LINE__)(); \
    static const solo_rig::test::Registrar SOLO_RIG_CONCATENATE(TestRegistrar_, __LINE__)( \
        name, SOLO_RIG_CONCATENATE(TestFunction_, __LINE__)); \
    static void SOLO_RIG_CONCATENATE(TestFunction_, __LINE__)()

#define REQUIRE(expression) solo_rig::test::Require((expression), #expression, __FILE__, __LINE__)
