
// main.cpp – Benchmark for milo::flat_map vs std::unordered_map
//==============================================================================
// Compile with: g++ -std=c++20 -O2 -march=native main.cpp -o benchmark
// (Adjust flags for your compiler.)
//


#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <string>
#include <random>
#include <cmath>
#include <array>


#include "milo/flat_map.h"

// RDTSC – cycle‑accurate timer (works on MSVC and GCC/clang)
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(__rdtsc)
static inline uint64_t rdtsc() { return __rdtsc(); }
#else
static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif


// Statistics structure

struct LatencyStats {
    double mean;
    uint64_t p50, p90, p95, p99, p999;
};


static std::vector<milo::char32> generate_fixed_keys(size_t count, uint64_t seed = 99) {
    std::mt19937_64 rng(seed);
    std::vector<milo::char32> keys(count);
    for (auto& k : keys) {
        // Fill with bytes
        uint64_t* p = reinterpret_cast<uint64_t*>(k.data);
        p[0] = rng(); p[1] = rng(); p[2] = rng(); p[3] = rng();
    }
    return keys;
}


static LatencyStats compute_stats(std::vector<uint64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();
    double sum = 0;
    for (auto s : samples) sum += static_cast<double>(s);
    return {
        sum / n,
        samples[n * 50 / 100],
        samples[n * 90 / 100],
        samples[n * 95 / 100],
        samples[n * 99 / 100],
        samples[n * 999 / 1000],
    };
}


// Console output helpers

static void print_header(const std::string& section = "") {
    if (!section.empty()) {
        std::cout << "\n=== " << section << " ===\n";
    }
    std::cout << std::left
        << std::setw(26) << "container"
        << std::setw(22) << "operation"
        << std::right
        << std::setw(8) << "mean"
        << std::setw(8) << "p50"
        << std::setw(8) << "p90"
        << std::setw(8) << "p95"
        << std::setw(8) << "p99"
        << std::setw(9) << "p99.9"
        << "\n"
        << std::string(97, '-') << "\n";
}

static void print_row(const std::string& container,
    const std::string& operation,
    const LatencyStats& s) {
    std::cout << std::left
        << std::setw(26) << container
        << std::setw(22) << operation
        << std::right << std::fixed << std::setprecision(0)
        << std::setw(8) << s.mean
        << std::setw(8) << s.p50
        << std::setw(8) << s.p90
        << std::setw(8) << s.p95
        << std::setw(8) << s.p99
        << std::setw(9) << s.p999
        << "\n";
}


// String key generation (realistic variable‑length keys)

static std::vector<std::string> generate_string_keys(size_t count,
    int min_len, int max_len,
    uint64_t seed = 77) {
    static constexpr char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static constexpr int charset_len = sizeof(charset) - 1;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> len_dist(min_len, max_len);
    std::uniform_int_distribution<int> char_dist(0, charset_len - 1);

    std::vector<std::string> keys(count);
    for (auto& k : keys) {
        int len = len_dist(rng);
        k.resize(len);
        for (int i = 0; i < len; ++i) {
            k[i] = charset[char_dist(rng)];
        }
    }
    return keys;
}


// Benchmark parameters
static constexpr int NUM_OPS = 10'000'000;
static constexpr int WARMUP_OPS = 1'000'000;



int main() {
    std::cout << "milo::flat_map benchmark\n"
        << "Operations per test: " << NUM_OPS / 1'000'000 << "M, "
        << "warmup: " << WARMUP_OPS / 1'000'000 << "M\n"
        << "All times are in CPU cycles (RDTSC)\n\n";

    std::vector<uint64_t> latencies(NUM_OPS);

    // Helper to run a benchmark and record results
    auto run_bench = [&](const std::string& container,
        const std::string& operation,
        auto&& func) {
            func();                     // execute the benchmark (fills latencies)
            auto stats = compute_stats(latencies);
            print_row(container, operation, stats);
        };

     
    // 1. uint64_t keys
    
    {
        std::mt19937_64 rng(42);
        std::vector<uint64_t> keys(NUM_OPS + WARMUP_OPS);
        for (auto& k : keys) k = rng();

        std::vector<uint64_t> lookup_keys(keys.begin() + WARMUP_OPS, keys.end());
        {
            std::mt19937_64 shuffle_rng(123);
            std::shuffle(lookup_keys.begin(), lookup_keys.end(), shuffle_rng);
        }

        print_header("uint64_t keys");

        // milo::flat_map
        {
            milo::flat_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("milo::flat_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("milo::flat_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto* v = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (v ? *v : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto* v = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (v ? *v : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            milo::flat_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            run_bench("milo::flat_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            run_bench("milo::flat_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }

        std::cout << "\n";

        // std::unordered_map
        {
            std::unordered_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("std::unordered_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("std::unordered_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto it = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (it != m.end() ? it->second : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto it = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (it != m.end() ? it->second : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            std::unordered_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            run_bench("std::unordered_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<uint64_t, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[keys[i]] = i;
            run_bench("std::unordered_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
    }

    
    // 2. std::string keys (8–24 chars)
    
    {
        auto all_keys = generate_string_keys(NUM_OPS + WARMUP_OPS, 8, 24, 77);
        std::vector<std::string> lookup_keys(all_keys.begin() + WARMUP_OPS, all_keys.end());
        {
            std::mt19937_64 shuffle_rng(456);
            std::shuffle(lookup_keys.begin(), lookup_keys.end(), shuffle_rng);
        }

        print_header("std::string keys (8-24 chars)");

        // milo::flat_map
        {
            milo::flat_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("milo::flat_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[all_keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[all_keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("milo::flat_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto* v = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (v ? *v : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto* v = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (v ? *v : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            milo::flat_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("milo::flat_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("milo::flat_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }

        std::cout << "\n";

        // std::unordered_map
        {
            std::unordered_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("std::unordered_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[all_keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[all_keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("std::unordered_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto it = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (it != m.end() ? it->second : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto it = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (it != m.end() ? it->second : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            std::unordered_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("std::unordered_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<std::string, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("std::unordered_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
    }

   
    // 3. FixedKey32 (char[32]) – no heap allocation
    // Requires milo::char32, milo::char32Hash
    // Generates synthetic keys to emulate OrderID's or otherwise.
    {
        auto all_keys = generate_fixed_keys(NUM_OPS + WARMUP_OPS, 99);
        std::vector<milo::char32> lookup_keys(all_keys.begin() + WARMUP_OPS, all_keys.end());
        {
            std::mt19937_64 shuffle_rng(789);
            std::shuffle(lookup_keys.begin(), lookup_keys.end(), shuffle_rng);
        }

        print_header("FixedKey32 (char[32]) keys");

        // milo::flat_map with explicit hash
        {
            milo::flat_map<milo::char32, uint64_t, milo::char32Hash> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("milo::flat_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[all_keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[all_keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<milo::char32, uint64_t, milo::char32Hash> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("milo::flat_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto* v = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (v ? *v : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto* v = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (v ? *v : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            milo::flat_map<milo::char32, uint64_t, milo::char32Hash> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("milo::flat_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            milo::flat_map<milo::char32, uint64_t, milo::char32Hash> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("milo::flat_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }

        std::cout << "\n";

        // std::unordered_map
        {
            std::unordered_map<milo::char32, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            run_bench("std::unordered_map", "insert", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) m[all_keys[i]] = i;
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[all_keys[WARMUP_OPS + i]] = i;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<milo::char32, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            volatile uint64_t sink = 0;
            run_bench("std::unordered_map", "lookup", [&] {
                for (int i = 0; i < WARMUP_OPS; ++i) {
                    auto it = m.find(lookup_keys[i % NUM_OPS]);
                    sink += (it != m.end() ? it->second : 0);
                }
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    auto it = m.find(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    sink += (it != m.end() ? it->second : 0);
                    latencies[i] = t1 - t0;
                }
                });
            (void)sink;
        }
        {
            std::unordered_map<milo::char32, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("std::unordered_map", "lookup+append", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m[lookup_keys[i]] += 1;
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
        {
            std::unordered_map<milo::char32, uint64_t> m;
            m.reserve(NUM_OPS + WARMUP_OPS);
            for (int i = 0; i < NUM_OPS + WARMUP_OPS; ++i) m[all_keys[i]] = i;
            run_bench("std::unordered_map", "erase", [&] {
                for (int i = 0; i < NUM_OPS; ++i) {
                    uint64_t t0 = rdtsc();
                    m.erase(lookup_keys[i]);
                    uint64_t t1 = rdtsc();
                    latencies[i] = t1 - t0;
                }
                });
        }
    }

    return 0;
}
