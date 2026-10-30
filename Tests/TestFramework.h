// Tests/TestFramework.h – minimalster Header-only-Testrunner ohne externe Abhängigkeit.
// Nutzung:  TEST(Name) { CHECK(cond); CHECK_EQ(a,b); }   und main() = mds::test::runAll();
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mds::test
{

struct Case { std::string name; std::function<void()> fn; };

inline std::vector<Case>& registry()
{
    static std::vector<Case> r;
    return r;
}

inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::string& current() { static std::string s; return s; }

struct Registrar
{
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({ name, std::move(fn) }); }
};

inline void fail(const char* file, int line, const std::string& msg)
{
    ++failures();
    std::printf("  FAIL [%s] %s:%d  %s\n", current().c_str(), file, line, msg.c_str());
}

inline int runAll()
{
    int failedCases = 0;
    for (auto& c : registry())
    {
        current() = c.name;
        const int before = failures();
        c.fn();
        const bool ok = (failures() == before);
        if (!ok) ++failedCases;
        std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", c.name.c_str());
    }
    std::printf("\n%d Tests, %d Checks, %d fehlgeschlagene Tests\n",
                static_cast<int>(registry().size()), checks(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

} // namespace mds::test

#define MDS_TEST(name)                                                        \
    static void name();                                                       \
    static ::mds::test::Registrar reg_##name(#name, name);                    \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++::mds::test::checks();                                              \
        if (!(cond)) ::mds::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");\
    } while (false)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        ++::mds::test::checks();                                              \
        if (!((a) == (b))) ::mds::test::fail(__FILE__, __LINE__,              \
            std::string("CHECK_EQ(" #a ", " #b ")"));                         \
    } while (false)

#define CHECK_NEAR(a, b, eps)                                                 \
    do {                                                                      \
        ++::mds::test::checks();                                              \
        if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps))) \
            ::mds::test::fail(__FILE__, __LINE__,                             \
                std::string("CHECK_NEAR(" #a ", " #b ", " #eps ")"));         \
    } while (false)
