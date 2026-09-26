#include <doctest/doctest.h>

#include "core/geometry.h"
#include "core/rolling_stats.h"

using namespace mausely;

TEST_CASE("RollingStats computes mean, percentiles and max") {
    RollingStats s(100);
    for (int i = 1; i <= 100; ++i) s.push(i);
    CHECK(s.count() == 100);
    CHECK(s.mean() == doctest::Approx(50.5));
    CHECK(s.percentile(50) == doctest::Approx(50).epsilon(0.02));
    CHECK(s.percentile(95) == doctest::Approx(95).epsilon(0.02));
    CHECK(s.max() == 100);
    CHECK(s.last() == 100);
}

TEST_CASE("RollingStats evicts the oldest samples") {
    RollingStats s(3);
    for (int i = 1; i <= 5; ++i) s.push(i);
    CHECK(s.count() == 3);
    CHECK(s.at(0) == 3);
    CHECK(s.at(2) == 5);
    CHECK(s.mean() == doctest::Approx(4));
}

TEST_CASE("normalizeRadians wraps into [-pi, pi)") {
    CHECK(normalizeRadians(3 * kPi) == doctest::Approx(-kPi));
    CHECK(normalizeRadians(-kPi / 2) == doctest::Approx(-kPi / 2));
    CHECK(normalizeRadians(2 * kPi + 0.25f) == doctest::Approx(0.25f));
}
