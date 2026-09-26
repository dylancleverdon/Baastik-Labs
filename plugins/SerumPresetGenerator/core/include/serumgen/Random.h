#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace serumgen
{
// xoshiro256** seeded with splitmix64. Hand-rolled (not <random>
// distributions) so a seed gives the same preset on every platform.
class Rng
{
public:
    explicit Rng(std::uint64_t seed)
    {
        for (auto& word : state_)
        {
            seed += 0x9e3779b97f4a7c15ULL;
            auto z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            word = z ^ (z >> 31);
        }
    }

    std::uint64_t next()
    {
        const auto result = rotl(state_[1] * 5, 7) * 9;
        const auto t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return result;
    }

    // Uniform in [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }

    double logRange(double lo, double hi)
    {
        if (lo <= 0.0 || hi <= 0.0)
            return range(lo, hi);
        return std::exp(range(std::log(lo), std::log(hi)));
    }

    bool chance(double p) { return uniform() < p; }

    std::size_t index(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(uniform() * static_cast<double>(n)); }

    // Standard normal via Box-Muller.
    double gaussian()
    {
        const double u1 = 1.0 - uniform();
        const double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    // Weighted pick; returns the index, or 0 for empty/zero-weight input.
    std::size_t weighted(const std::vector<double>& weights)
    {
        double total = 0.0;
        for (auto w : weights)
            total += std::max(w, 0.0);
        if (total <= 0.0)
            return 0;
        double r = uniform() * total;
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            r -= std::max(weights[i], 0.0);
            if (r < 0.0)
                return i;
        }
        return weights.size() - 1;
    }

    // Child generator for an independent stream (keeps stages decoupled so a
    // locked group doesn't shift the random draws of the others).
    Rng fork(std::uint64_t salt) { return Rng(next() ^ (salt * 0x9e3779b97f4a7c15ULL)); }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    std::uint64_t state_[4] {};
};
} // namespace serumgen
