
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdint>
#include <string>
#include <string_view>
#include <random>
#include <cstring>
#include <cmath>
#include <array>


namespace milo {

    // Fixed-size 32-byte char string — two per cacheline if fetched from a contiguous storage struct
    struct char32 {
        char data[32];

        bool operator==(const char32& o) const noexcept {
            return std::memcmp(data, o.data, 32) == 0;
        }

        bool operator!=(const char32& o) const noexcept {
            return !(*this == o);
        }

        // Default constructor
        char32() noexcept { std::memset(data, 0, 32); }

        // Implicit constructor from string literals
        template <size_t N>
        char32(const char(&str)[N]) noexcept {
            static_assert(N <= 32, "String literal too long for milo::char32");
            std::memcpy(data, str, N);
            if constexpr (N < 32) {
                std::memset(data + N, 0, 32 - N);
            }
        }

    };


    // Hash for milo::char32 — FNV-1a over the raw bytes
    struct char32Hash {
        size_t operator()(const milo::char32& k) const noexcept {
            uint64_t hash = 0xcbf29ce484222325ULL;
            for (int i = 0; i < 32; ++i) {
                hash ^= static_cast<unsigned char>(k.data[i]);
                hash *= 0x100000001b3ULL;
            }
            return static_cast<size_t>(hash);
        }
    };
}


    // std::hash specialization for milo::char32 (so unordered_map can use it)
    namespace std {
        template<> struct hash<milo::char32> {
            size_t operator()(const milo::char32& k) const noexcept {
                return milo::char32Hash{}(k);
            }
        };
    }


