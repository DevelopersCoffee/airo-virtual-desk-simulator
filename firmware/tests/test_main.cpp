#include "test.h"

namespace airo_test {
std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
int g_failures = 0;
}  // namespace airo_test

int main() {
  int failed_cases = 0;
  for (const auto& c : airo_test::registry()) {
    const int before = airo_test::g_failures;
    c.fn();
    const bool ok = airo_test::g_failures == before;
    if (!ok) ++failed_cases;
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", c.name);
  }
  std::printf("\n%zu tests, %d failed\n", airo_test::registry().size(), failed_cases);
  return failed_cases == 0 ? 0 : 1;
}
