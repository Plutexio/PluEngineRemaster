//
// Created by Plutex on 2026-08-06.
//
// A ~100-line test runner. The repo vendors no test framework and pulling one in through
// vcpkg for a handful of assertions is not worth the dependency — this matches the
// "own STL" spirit of the project and has no build-system footprint at all.
//
// Usage:
//     PLU_TEST(ConcurrentHashMap_InsertRejectsDuplicates)
//     {
//         Plu::ConcurrentHashMap<int, int> map;
//         PLU_CHECK(map.Insert(1, 10));
//         PLU_CHECK_FALSE(map.Insert(1, 11));
//         PLU_CHECK_EQ(map.Size(), 1u);
//     }
//
// Every PLU_CHECK* that fails prints file:line and keeps going, so one broken invariant
// does not hide the next five. A test with any failed check is reported as failed and the
// process exits non-zero.

#ifndef PLU_TESTS_TESTFRAMEWORK_H
#define PLU_TESTS_TESTFRAMEWORK_H

#include <cstddef>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace PluTest
{
    // Thread count for the stress suites. Sized to the machine, but never below 4 — a
    // single-core CI box must still exercise the interleavings.
    inline unsigned int StressThreadCount()
    {
        const unsigned int hardware = std::thread::hardware_concurrency();
        return hardware < 4 ? 4u : hardware;
    }

    using TestFunction = void(*)();

    struct TestCase
    {
        const char* Name;
        TestFunction Function;
    };

    // Function-local static: avoids the static-initialization-order problem between the
    // registry and the self-registering TestRegistrar objects in each test file.
    inline std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    // Failures of the test currently running. Reset by the runner before each test.
    inline int& CurrentFailureCount()
    {
        static int failures = 0;
        return failures;
    }

    struct TestRegistrar
    {
        TestRegistrar(const char* name, TestFunction function)
        {
            Registry().push_back(TestCase{name, function});
        }
    };

    inline void ReportFailure(const char* file, int line, const std::string& message)
    {
        std::printf("    FAIL %s:%d\n         %s\n", file, line, message.c_str());
        ++CurrentFailureCount();
    }

    // Values are stringified through std::to_string where possible so the report shows what
    // was actually compared, not just "the check failed".
    template<typename T>
    std::string Stringify(const T& value)
    {
        if constexpr (std::is_convertible_v<T, std::string>) return std::string(value);
        else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
        else if constexpr (std::is_arithmetic_v<T>) return std::to_string(value);
        else return std::string("<value>");
    }

    // Runs everything in the registry. Returns the number of failed tests.
    inline int RunAll()
    {
        int failedTests = 0;
        const std::size_t total = Registry().size();

        std::printf("Running %zu test(s)\n\n", total);

        for (const TestCase& test : Registry())
        {
            std::printf("  %s\n", test.Name);
            std::fflush(stdout);

            CurrentFailureCount() = 0;
            test.Function();

            if (CurrentFailureCount() > 0)
            {
                ++failedTests;
                std::printf("    -> FAILED (%d check(s))\n", CurrentFailureCount());
            }
            std::fflush(stdout);
        }

        std::printf("\n%zu test(s), %d failed\n", total, failedTests);
        return failedTests;
    }
}

#define PLU_TEST_CONCAT_IMPL(a, b) a##b
#define PLU_TEST_CONCAT(a, b) PLU_TEST_CONCAT_IMPL(a, b)

#define PLU_TEST(name)                                                                     \
    static void name();                                                                    \
    static ::PluTest::TestRegistrar PLU_TEST_CONCAT(gRegistrar_, name)(#name, &name);       \
    static void name()

#define PLU_CHECK(expr)                                                                    \
    do {                                                                                   \
        if (!(expr)) ::PluTest::ReportFailure(__FILE__, __LINE__, "expected: " #expr);     \
    } while (false)

#define PLU_CHECK_FALSE(expr)                                                              \
    do {                                                                                   \
        if (expr) ::PluTest::ReportFailure(__FILE__, __LINE__, "expected NOT: " #expr);    \
    } while (false)

#define PLU_CHECK_EQ(actual, expected)                                                     \
    do {                                                                                   \
        const auto& pluActual = (actual);                                                  \
        const auto& pluExpected = (expected);                                              \
        if (!(pluActual == pluExpected)) {                                                 \
            ::PluTest::ReportFailure(__FILE__, __LINE__,                                   \
                std::string(#actual " == " #expected " | actual: ")                        \
                    + ::PluTest::Stringify(pluActual)                                      \
                    + ", expected: " + ::PluTest::Stringify(pluExpected));                 \
        }                                                                                  \
    } while (false)

#define PLU_CHECK_NE(actual, expected)                                                     \
    do {                                                                                   \
        const auto& pluActual = (actual);                                                  \
        const auto& pluExpected = (expected);                                              \
        if (pluActual == pluExpected) {                                                    \
            ::PluTest::ReportFailure(__FILE__, __LINE__,                                   \
                std::string(#actual " != " #expected " | both: ")                          \
                    + ::PluTest::Stringify(pluActual));                                    \
        }                                                                                  \
    } while (false)

#endif //PLU_TESTS_TESTFRAMEWORK_H
