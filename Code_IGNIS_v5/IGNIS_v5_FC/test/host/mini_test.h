// Minimal host test framework (no dependencies). One binary, all tests.
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mt {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline bool& caseFailed() { static bool f = false; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, f}); } };

inline int runAll() {
  int passed = 0, failed = 0;
  for (auto& c : registry()) {
    caseFailed() = false;
    c.fn();
    if (caseFailed()) { failed++; std::printf("[FAIL] %s\n", c.name); }
    else { passed++; std::printf("[PASS] %s\n", c.name); }
  }
  std::printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
  return failed == 0 ? 0 : 1;
}
}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                                   \
  static void name();                                                \
  static mt::Reg MT_CAT(reg_, name)(#name, name);                    \
  static void name()

#define CHECK(cond)                                                              \
  do { if (!(cond)) { mt::caseFailed() = true;                                   \
       std::printf("    %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b)                                                           \
  do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { mt::caseFailed() = true; \
       std::printf("    %s:%d CHECK_EQ(%s, %s) -> %lld vs %lld\n", __FILE__,      \
                   __LINE__, #a, #b, (long long)_a, (long long)_b); } } while (0)
#define CHECK_NEAR(a, b, tol)                                                    \
  do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) {              \
       mt::caseFailed() = true; std::printf("    %s:%d CHECK_NEAR(%s, %s) -> %f vs %f\n", \
       __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
