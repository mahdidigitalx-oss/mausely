#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mausely {

// Fixed-size window of samples with exact mean / percentile / max.
class RollingStats {
public:
    explicit RollingStats(size_t capacity = 300) : buf_(capacity) {}

    void push(double v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % buf_.size();
        if (count_ < buf_.size()) ++count_;
    }

    void clear() { head_ = count_ = 0; }
    size_t count() const { return count_; }
    size_t capacity() const { return buf_.size(); }

    double last() const {
        return count_ ? buf_[(head_ + buf_.size() - 1) % buf_.size()] : 0.0;
    }

    double mean() const {
        if (!count_) return 0.0;
        double s = 0.0;
        for (size_t i = 0; i < count_; ++i) s += at(i);
        return s / static_cast<double>(count_);
    }

    double max() const {
        double m = 0.0;
        for (size_t i = 0; i < count_; ++i) m = (i == 0 || at(i) > m) ? at(i) : m;
        return m;
    }

    // p in [0, 100]; nearest-rank percentile.
    double percentile(double p) const {
        if (!count_) return 0.0;
        std::vector<double> v(count_);
        for (size_t i = 0; i < count_; ++i) v[i] = at(i);
        size_t k = static_cast<size_t>(p / 100.0 * static_cast<double>(count_ - 1) + 0.5);
        k = std::min(k, count_ - 1);
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
        return v[k];
    }

    // i = 0 is the oldest sample still in the window.
    double at(size_t i) const {
        size_t start = (head_ + buf_.size() - count_) % buf_.size();
        return buf_[(start + i) % buf_.size()];
    }

private:
    std::vector<double> buf_;
    size_t head_ = 0;
    size_t count_ = 0;
};

}  // namespace mausely
