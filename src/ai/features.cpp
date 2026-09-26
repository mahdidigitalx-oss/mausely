#include "ai/features.h"

#include <algorithm>
#include <cmath>

namespace mausely {
namespace {

constexpr int kTips[5] = {4, 8, 12, 16, 20};
constexpr int kChains[5][5] = {
    {0, 1, 2, 3, 4}, {0, 5, 6, 7, 8}, {0, 9, 10, 11, 12}, {0, 13, 14, 15, 16}, {0, 17, 18, 19, 20}};
constexpr int kDirections[5][2] = {{1, 4}, {5, 8}, {9, 12}, {13, 16}, {17, 20}};
constexpr int kPalmPoints[5] = {0, 5, 9, 13, 17};

// Same formulation as the Python reference (float64 there, so use double here).
struct D3 {
    double x, y, z;
};
D3 sub(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double norm(D3 a) { return std::sqrt(dot(a, a)); }

double angle(D3 a, D3 b) {
    double c = dot(a, b) / std::max(norm(a) * norm(b), 1e-12);
    return std::acos(std::clamp(c, -1.0, 1.0));
}

}  // namespace

FeatureVector computeFeatures(const std::array<Vec3, kNumLandmarks>& world) {
    D3 p[kNumLandmarks];
    for (int i = 0; i < kNumLandmarks; ++i) p[i] = {world[i].x, world[i].y, world[i].z};
    const D3 wrist = p[0];
    const double palm = std::max(norm(sub(p[9], wrist)), 1e-9);

    FeatureVector f{};
    int k = 0;
    for (const auto& chain : kChains)
        for (int j = 1; j < 4; ++j)
            f[k++] = static_cast<float>(angle(sub(p[chain[j]], p[chain[j - 1]]), sub(p[chain[j + 1]], p[chain[j]])));

    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j) f[k++] = static_cast<float>(norm(sub(p[kTips[i]], p[kTips[j]])) / palm);

    for (int t : kTips) f[k++] = static_cast<float>(norm(sub(p[t], wrist)) / palm);

    D3 centre{0, 0, 0};
    for (int i : kPalmPoints) centre = {centre.x + p[i].x / 5, centre.y + p[i].y / 5, centre.z + p[i].z / 5};
    for (int t : kTips) f[k++] = static_cast<float>(norm(sub(p[t], centre)) / palm);

    for (int i = 0; i < 4; ++i) {
        D3 a = sub(p[kDirections[i][1]], p[kDirections[i][0]]);
        D3 b = sub(p[kDirections[i + 1][1]], p[kDirections[i + 1][0]]);
        f[k++] = static_cast<float>(angle(a, b));
    }

    // Anatomical frame (independent of left/right): y = wrist -> middle MCP,
    // x = pinky MCP -> index MCP orthogonalised against y.
    D3 y = sub(p[9], wrist);
    y = {y.x / palm, y.y / palm, y.z / palm};
    D3 across = sub(p[5], p[17]);
    double ay = dot(across, y);
    D3 x = {across.x - ay * y.x, across.y - ay * y.y, across.z - ay * y.z};
    double nx = std::max(norm(x), 1e-12);
    x = {x.x / nx, x.y / nx, x.z / nx};
    for (int i = 0; i < kNumLandmarks; ++i) {
        D3 rel = sub(p[i], wrist);
        f[k++] = static_cast<float>(dot(rel, x) / palm);
        f[k++] = static_cast<float>(dot(rel, y) / palm);
    }
    return f;
}

}  // namespace mausely
