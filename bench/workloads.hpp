#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lru_bench {

enum class KeyDistribution { kUniform, kZipfian };

// One pregenerated operation: the key in the low 63 bits and the top bit set for a write. Packing both
// into one word keeps a stream at 8 bytes per operation, so millions of operations per thread stay small.
using Op = std::uint64_t;

inline constexpr Op kWriteFlag = Op{1} << 63;

constexpr std::uint64_t key_of(Op op) noexcept {
    return op & ~kWriteFlag;
}

constexpr bool is_write(Op op) noexcept {
    return (op & kWriteFlag) != 0;
}

// splitmix64: fast, statistically sound for workload generation, and seedable per stream so every thread
// and every run of the benchmark sees exactly the same operations.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform in [0, 1), from the top 53 bits so every double in the range is equally likely.
    double next_unit() noexcept {
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t state_;
};

// Every key equally likely. The hardest case for any cache, and the one where eviction policy matters
// least, because no key is more worth keeping than another.
class UniformKeys {
public:
    explicit UniformKeys(std::uint64_t key_space) noexcept : key_space_(key_space) {}

    // The modulo bias is below 1e-13 for a key space of a million, far under any measurement noise.
    std::uint64_t operator()(SplitMix64& rng) const noexcept {
        return rng.next() % key_space_;
    }

private:
    std::uint64_t key_space_;
};

// Zipfian over [0, key_space): key k is drawn with probability proportional to 1 / (k + 1)^theta, so low
// keys are hot. This is the YCSB generator (Gray et al., "Quickly Generating Billion-Record Synthetic
// Databases"). Real cache traffic is skewed like this, and skew is what makes hit rate depend on which
// entries a cache chooses to keep. Requires 0 < theta < 1.
class ZipfianKeys {
public:
    ZipfianKeys(std::uint64_t key_space, double theta)
        : key_space_(key_space),
          n_(static_cast<double>(key_space)),
          theta_(theta),
          alpha_(1.0 / (1.0 - theta)),
          zeta_n_(zeta(key_space, theta)),
          eta_((1.0 - std::pow(2.0 / n_, 1.0 - theta)) / (1.0 - zeta(2, theta) / zeta_n_)) {}

    std::uint64_t operator()(SplitMix64& rng) const noexcept {
        const double u = rng.next_unit();
        const double uz = u * zeta_n_;
        if (uz < 1.0) {
            return 0;
        }
        if (uz < 1.0 + std::pow(0.5, theta_)) {
            return 1;
        }
        const auto key = static_cast<std::uint64_t>(n_ * std::pow(eta_ * u - eta_ + 1.0, alpha_));
        // Floating-point rounding can land exactly on key_space for u close to 1.
        return key < key_space_ ? key : key_space_ - 1;
    }

private:
    static double zeta(std::uint64_t n, double theta) {
        double sum = 0.0;
        for (std::uint64_t i = 1; i <= n; ++i) {
            sum += 1.0 / std::pow(static_cast<double>(i), theta);
        }
        return sum;
    }

    std::uint64_t key_space_;
    double n_;
    double theta_;
    double alpha_;
    double zeta_n_;
    double eta_;
};

struct WorkloadSpec {
    KeyDistribution distribution;
    std::uint64_t key_space;
    double zipf_theta;
    unsigned write_percent;
};

namespace detail {

template <typename Keys>
void fill_ops(std::vector<Op>& ops, const Keys& keys, SplitMix64& rng, unsigned write_percent) {
    for (Op& op : ops) {
        const std::uint64_t key = keys(rng);
        const bool write = rng.next() % 100 < write_percent;
        op = write ? (key | kWriteFlag) : key;
    }
}

}  // namespace detail

// Generates `count` operations up front, so the cost of drawing Zipfian keys is never inside a timed
// region. Each operation is a write with probability write_percent / 100 and a read otherwise.
inline std::vector<Op> generate_ops(const WorkloadSpec& spec, std::size_t count, std::uint64_t seed) {
    std::vector<Op> ops(count);
    SplitMix64 rng(seed);
    if (spec.distribution == KeyDistribution::kUniform) {
        detail::fill_ops(ops, UniformKeys(spec.key_space), rng, spec.write_percent);
    } else {
        detail::fill_ops(ops, ZipfianKeys(spec.key_space, spec.zipf_theta), rng, spec.write_percent);
    }
    return ops;
}

}  // namespace lru_bench
