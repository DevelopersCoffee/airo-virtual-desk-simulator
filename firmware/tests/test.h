// Tiny dependency-free test harness (no network needed to build in CI).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace airo_test {

struct Case {
  const char* name;
  std::function<void()> fn;
};

std::vector<Case>& registry();
extern int g_failures;

struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace airo_test

#define AIRO_CAT2(a, b) a##b
#define AIRO_CAT(a, b) AIRO_CAT2(a, b)
#define TEST(name)                                                                              \
  static void AIRO_CAT(test_fn_, __LINE__)();                                                   \
  static airo_test::Register AIRO_CAT(test_reg_, __LINE__)(name, &AIRO_CAT(test_fn_, __LINE__)); \
  static void AIRO_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
      ++airo_test::g_failures;                                                       \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                                          \
  do {                                                                                          \
    const auto& va_ = (a);                                                                      \
    const auto& vb_ = (b);                                                                      \
    if (!(va_ == vb_)) {                                                                        \
      std::printf("    FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);                    \
      ++airo_test::g_failures;                                                                  \
    }                                                                                           \
  } while (0)
