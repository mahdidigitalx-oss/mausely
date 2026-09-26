#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace mausely::test {

// Reads every number of a (simple) JSON file in document order. The test data
// files are written by training/ and contain no digits inside keys.
inline std::vector<double> readNumbers(const std::string& path) {
    std::vector<double> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    const char* p = s.c_str();
    while (*p) {
        if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
            char* end = nullptr;
            double v = std::strtod(p, &end);
            if (end != p) {
                out.push_back(v);
                p = end;
                continue;
            }
        }
        ++p;
    }
    return out;
}

inline std::string dataFile(const char* name) { return std::string(MAUSELY_TEST_DATA_DIR) + "/" + name; }

}  // namespace mausely::test
