#pragma once

#include <windows.h>

#include <cstdint>

namespace mausely {

// Monotonic microsecond clock backed by QueryPerformanceCounter.
class Clock {
public:
    static int64_t nowUs() {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return static_cast<int64_t>(static_cast<double>(t.QuadPart) * usPerTick());
    }

private:
    static double usPerTick() {
        static const double v = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return 1e6 / static_cast<double>(f.QuadPart);
        }();
        return v;
    }
};

// Measures elapsed milliseconds since construction or the last lap().
class Stopwatch {
public:
    Stopwatch() : start_(Clock::nowUs()) {}
    double lapMs() {
        int64_t now = Clock::nowUs();
        double ms = static_cast<double>(now - start_) / 1000.0;
        start_ = now;
        return ms;
    }

private:
    int64_t start_;
};

}  // namespace mausely
