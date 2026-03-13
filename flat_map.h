#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <cassert>
#include <bit>
#include <new>
#include <type_traits>
#include <vector>

#include "char32.h"


// ---------------------------------------------------------------------------
// Compiler compat
// ---------------------------------------------------------------------------
#ifdef _MSC_VER
#define MILO_LIKELY(x)   (x)
#define MILO_UNLIKELY(x) (x)
#define MILO_PREFETCH(addr)
#define MILO_FORCEINLINE __forceinline
#else
#define MILO_LIKELY(x)   __builtin_expect(!!(x), 1)
#define MILO_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define MILO_PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#define MILO_FORCEINLINE __attribute__((always_inline)) inline
#endif

namespace milo {

    // ---------------------------------------------------------------------------
    // milo::FlatMap
    // ---------------------------------------------------------------------------
    //
    // Open-addressing flat hash map optimized for latency determinism:
    //
    //   - seperate metadata array: probe loop touches only a dense byte array,
    //     not KV data. 64 metadata bytes per cache line = 64 slots checked per
    //     cache miss during probing.
    //
    //   - 7-bit hash tag per slot: 0 reserved for empty, tag is stored in the metadata byte itself.
    //     most probe rejections happen without ever loading the key. 
    //
    //   - Backward-shift deletion: no tombstones, no conditional rehash on erase.
    //     Erase is O(1) amortized and never triggers a latency spike.
    //
    //   - Linear probing with branch/prefetch hints over packed metadata
    //
    //   - unchecked_* variants skip capacity checks for pre-reserved hot paths.
    //
    // Metadata byte encoding:
    //   0x00        = empty
    //   0x01..0x7F  = occupied, tag = ((hash >> 7) & 0x7F) | (tag == 0)
    //
    // ---------------------------------------------------------------------------
    


    // Hash helpers
    // Generic integer hash — splitmix64 finalizer (better avalanche than golden ratio)
    template <typename T>
    struct IntHash {
        size_t operator()(T key) const noexcept {
            uint64_t x = static_cast<uint64_t>(key);
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ULL;
            x ^= x >> 33;
            return static_cast<size_t>(x);
        }
    };

    // FNV-1a for strings
    struct StringHash {
        size_t operator()(std::string_view key) const noexcept {
            uint64_t hash = 0xcbf29ce484222325ULL;
            for (unsigned char c : key) {
                hash ^= c;
                hash *= 0x100000001b3ULL;
            }
            return static_cast<size_t>(hash);
        }
    };

    // Default hash selector
    template <typename Key>
    using DefaultHash = std::conditional_t<
        std::is_integral_v<Key>, IntHash<Key>,
        std::conditional_t<std::is_convertible_v<Key, std::string_view>,
        StringHash, std::hash<Key>>
        >;



    template < typename Key, typename Value, typename Hash = DefaultHash<Key> >
    class flat_map {
    public:
        using key_type = Key;
        using mapped_type = Value;
        using value_type = std::pair<Key, Value>;

    private:
        static constexpr uint8_t EMPTY = 0x00;
        static constexpr float   MAX_LOAD_FACTOR = 0.5f;
        static constexpr size_t  MIN_CAPACITY = 8;

        // Storage: metadata separated from KV for cache-efficient probing.
        uint8_t* meta_ = nullptr;
        value_type* kvs_ = nullptr;
        size_t      capacity_ = 0;
        size_t      size_ = 0;
        [[no_unique_address]] Hash hasher_;

        // -- Tag extraction ------------------------------------------------------
        static uint8_t tag_from_hash(size_t h) noexcept {
            uint8_t tag = static_cast<uint8_t>((h >> 7) & 0x7F);
            tag += (tag == 0);  // ensure non-zero (branchless)
            return tag;
        }

        // -- Raw allocation ------------------------------------------------------
        void alloc(size_t cap) {
            assert(cap > 0 && std::has_single_bit(cap));

            meta_ = static_cast<uint8_t*>(
                ::operator new(cap, std::align_val_t{ 64 }));
            std::memset(meta_, EMPTY, cap);

            kvs_ = static_cast<value_type*>(
                ::operator new(cap * sizeof(value_type),
                    std::align_val_t{ alignof(value_type) }));

            capacity_ = cap;
        }

        void dealloc() noexcept {
            if (!meta_) return;

            for (size_t i = 0; i < capacity_; ++i) {
                if (meta_[i] != EMPTY) {
                    kvs_[i].~value_type();
                }
            }
            ::operator delete(meta_, std::align_val_t{ 64 });
            ::operator delete(kvs_, std::align_val_t{ alignof(value_type) });
            meta_ = nullptr;
            kvs_ = nullptr;
            capacity_ = 0;
            size_ = 0;
        }

        // -- Probing -------------------------------------------------------------
        struct ProbeResult {
            size_t  idx;
            uint8_t tag;
            bool    found;
        };

        ProbeResult probe(const Key& key) const noexcept {
            const size_t mask = capacity_ - 1;
            const size_t h = hasher_(key);
            const uint8_t tag = tag_from_hash(h);
            size_t idx = h & mask;

            while (true) {
                const uint8_t m = meta_[idx];

                if (MILO_UNLIKELY(m == EMPTY)) {
                    return { idx, tag, false };
                }

                if (m == tag) {
                    if (MILO_LIKELY(kvs_[idx].first == key)) {
                        return { idx, tag, true };
                    }
                }

                // Prefetch next slot (linear probing — sequential, cache-friendly)
                const size_t next = (idx + 1) & mask;
                MILO_PREFETCH(&meta_[next]);

                idx = next;
            }
        }

        // -- Grow ----------------------------------------------------------------
        void grow(size_t new_cap) {
            assert(std::has_single_bit(new_cap));

            uint8_t* old_meta = meta_;
            value_type* old_kvs = kvs_;
            const size_t old_cap = capacity_;

            meta_ = static_cast<uint8_t*>(
                ::operator new(new_cap, std::align_val_t{ 64 }));
            std::memset(meta_, EMPTY, new_cap);
            kvs_ = static_cast<value_type*>(
                ::operator new(new_cap * sizeof(value_type),
                    std::align_val_t{ alignof(value_type) }));
            capacity_ = new_cap;
            size_ = 0;

            if (old_meta) {
                for (size_t i = 0; i < old_cap; ++i) {
                    if (old_meta[i] != EMPTY) {
                        rehash_insert(std::move(old_kvs[i].first),
                            std::move(old_kvs[i].second));
                        old_kvs[i].~value_type();
                    }
                }
                ::operator delete(old_meta, std::align_val_t{ 64 });
                ::operator delete(old_kvs, std::align_val_t{ alignof(value_type) });
            }
        }

        // Insert without duplicate check (rehash only).
        template <typename K, typename V>
        void rehash_insert(K&& key, V&& val) {
            const size_t mask = capacity_ - 1;
            const size_t h = hasher_(key);
            const uint8_t tag = tag_from_hash(h);
            size_t idx = h & mask;

            while (meta_[idx] != EMPTY) {
                idx = (idx + 1) & mask;
            }

            ::new (&kvs_[idx]) value_type(std::forward<K>(key), std::forward<V>(val));
            meta_[idx] = tag;
            size_++;
        }

        // -- Backward-shift deletion ---------------------------------------------
        void backward_shift_delete(size_t idx) noexcept {
            const size_t mask = capacity_ - 1;

            kvs_[idx].~value_type();
            meta_[idx] = EMPTY;
            size_--;

            size_t gap = idx;
            size_t i = (idx + 1) & mask;

            while (meta_[i] != EMPTY) {
                const size_t home = hasher_(kvs_[i].first) & mask;
                const size_t dist_home_to_i = (i - home) & mask;
                const size_t dist_home_to_gap = (gap - home) & mask;

                if (dist_home_to_gap <= dist_home_to_i) {
                    ::new (&kvs_[gap]) value_type(std::move(kvs_[i]));
                    meta_[gap] = meta_[i];

                    kvs_[i].~value_type();
                    meta_[i] = EMPTY;

                    gap = i;
                }

                i = (i + 1) & mask;
            }
        }

        // -- Capacity check ------------------------------------------------------
        void ensure_capacity() {
            if (MILO_UNLIKELY(capacity_ == 0)) {
                alloc(MIN_CAPACITY);
            }
            else if (MILO_UNLIKELY(
                size_ + 1 > static_cast<size_t>(capacity_ * MAX_LOAD_FACTOR))) {
                grow(capacity_ * 2);
            }
        }

    public:
        // -- Construction / destruction 
        flat_map() = default;

        explicit flat_map(size_t initial_capacity) {
            if (initial_capacity > 0) {
                size_t cap = std::bit_ceil(initial_capacity);
                while (static_cast<float>(initial_capacity) > cap * MAX_LOAD_FACTOR) {
                    cap *= 2;
                }
                alloc(cap);
            }
        }

        ~flat_map() { dealloc(); }

        // -- Move
        flat_map(flat_map&& o) noexcept
            : meta_(o.meta_), kvs_(o.kvs_),
            capacity_(o.capacity_), size_(o.size_),
            hasher_(std::move(o.hasher_))
        {
            o.meta_ = nullptr; o.kvs_ = nullptr;
            o.capacity_ = 0;   o.size_ = 0;
        }

        flat_map& operator=(flat_map&& o) noexcept {
            if (this != &o) {
                dealloc();
                meta_ = o.meta_;       kvs_ = o.kvs_;
                capacity_ = o.capacity_; size_ = o.size_;
                hasher_ = std::move(o.hasher_);
                o.meta_ = nullptr; o.kvs_ = nullptr;
                o.capacity_ = 0;   o.size_ = 0;
            }
            return *this;
        }

        // -- Copy
        flat_map(const flat_map& o) {
            if (o.size_ > 0) {
                alloc(o.capacity_);
                for (size_t i = 0; i < o.capacity_; ++i) {
                    if (o.meta_[i] != EMPTY) {
                        ::new (&kvs_[i]) value_type(o.kvs_[i]);
                        meta_[i] = o.meta_[i];
                        size_++;
                    }
                }
            }
        }

        flat_map& operator=(const flat_map& o) {
            if (this != &o) {
                flat_map tmp(o);
                *this = std::move(tmp);
            }
            return *this;
        }

        // Lookup

        Value* find(const Key& key) noexcept {
            if (MILO_UNLIKELY(capacity_ == 0)) return nullptr;
            auto [idx, tag, found] = probe(key);
            return found ? &kvs_[idx].second : nullptr;
        }

        const Value* find(const Key& key) const noexcept {
            if (MILO_UNLIKELY(capacity_ == 0)) return nullptr;
            auto [idx, tag, found] = probe(key);
            return found ? &kvs_[idx].second : nullptr;
        }

        std::optional<std::reference_wrapper<Value>> get(const Key& key) noexcept {
            Value* v = find(key);
            return v ? std::optional{ std::ref(*v) } : std::nullopt;
        }

        std::optional<std::reference_wrapper<const Value>> get(const Key& key) const noexcept {
            const Value* v = find(key);
            return v ? std::optional{ std::cref(*v) } : std::nullopt;
        }

        bool contains(const Key& key) const noexcept {
            if (MILO_UNLIKELY(capacity_ == 0)) return false;
            return probe(key).found;
        }

        // Mutation 

        Value& operator[](const Key& key) {
            ensure_capacity();
            auto [idx, tag, found] = probe(key);

            if (!found) {
                ::new (&kvs_[idx]) value_type(key, Value{});
                meta_[idx] = tag;
                size_++;
            }

            return kvs_[idx].second;
        }

        template <typename K, typename V>
        void insert_or_assign(K&& key, V&& value) {
            ensure_capacity();
            auto [idx, tag, found] = probe(key);

            if (found) {
                kvs_[idx].second = std::forward<V>(value);
            }
            else {
                ::new (&kvs_[idx]) value_type(std::forward<K>(key),
                    std::forward<V>(value));
                meta_[idx] = tag;
                size_++;
            }
        }

        template <typename K, typename V>
        bool emplace(K&& key, V&& value) {
            ensure_capacity();
            auto [idx, tag, found] = probe(key);

            if (found) return false;

            ::new (&kvs_[idx]) value_type(std::forward<K>(key),
                std::forward<V>(value));
            meta_[idx] = tag;
            size_++;
            return true;
        }

        // Deletion (backward shift, no tombstones) 

        bool erase(const Key& key) noexcept {
            if (MILO_UNLIKELY(capacity_ == 0)) return false;
            auto [idx, tag, found] = probe(key);
            if (!found) return false;

            backward_shift_delete(idx);
            return true;
        }

        // Unchecked hot-path variants 
        // Caller guarantees: reserve() called, capacity is sufficient.
        // These skip the branch + potential rehash in ensure_capacity().

        Value& unchecked_insert_or_access(const Key& key) noexcept {
            assert(capacity_ > 0 &&
                size_ < static_cast<size_t>(capacity_ * MAX_LOAD_FACTOR));

            auto [idx, tag, found] = probe(key);
            if (!found) {
                ::new (&kvs_[idx]) value_type(key, Value{});
                meta_[idx] = tag;
                size_++;
            }
            return kvs_[idx].second;
        }

        template <typename K, typename V>
        void unchecked_insert_or_assign(K&& key, V&& value) noexcept {
            assert(capacity_ > 0 &&
                size_ < static_cast<size_t>(capacity_ * MAX_LOAD_FACTOR));

            auto [idx, tag, found] = probe(key);
            if (found) {
                kvs_[idx].second = std::forward<V>(value);
            }
            else {
                ::new (&kvs_[idx]) value_type(std::forward<K>(key),
                    std::forward<V>(value));
                meta_[idx] = tag;
                size_++;
            }
        }

        // -- Bulk 

        void clear() noexcept {
            if (!meta_) return;
            for (size_t i = 0; i < capacity_; ++i) {
                if (meta_[i] != EMPTY) {
                    kvs_[i].~value_type();
                    meta_[i] = EMPTY;
                }
            }
            size_ = 0;
        }

        void reserve(size_t count) {
            size_t needed = std::bit_ceil(
                static_cast<size_t>(count / MAX_LOAD_FACTOR) + 1);
            if (needed > capacity_) {
                grow(needed);
            }
        }

        // -- Capacity queries 
        size_t size()     const noexcept { return size_; }
        size_t capacity() const noexcept { return capacity_; }
        bool   empty()    const noexcept { return size_ == 0; }
        float  load_factor() const noexcept {
            return capacity_ ? static_cast<float>(size_) / capacity_ : 0.f;
        }

        // -- Iterators 
        // Return pointer/reference to the actual stored pair. No copies or dangling references

        class iterator {
            friend class flat_map;
            using kv_type = std::pair<Key, Value>;
            uint8_t* meta_;
            kv_type* kvs_;
            size_t   idx_;
            size_t   cap_;

            void advance() noexcept {
                while (idx_ < cap_ && meta_[idx_] == EMPTY) ++idx_;
            }

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = kv_type;
            using pointer = kv_type*;
            using reference = kv_type&;
            using iterator_category = std::forward_iterator_tag;

            iterator(uint8_t* m, kv_type* k, size_t i, size_t c) noexcept
                : meta_(m), kvs_(k), idx_(i), cap_(c) {
                advance();
            }

            reference operator*()  const noexcept { return kvs_[idx_]; }
            pointer   operator->() const noexcept { return &kvs_[idx_]; }

            iterator& operator++() noexcept { ++idx_; advance(); return *this; }
            iterator  operator++(int) noexcept { auto t = *this; ++(*this); return t; }

            bool operator==(const iterator& o) const noexcept { return idx_ == o.idx_; }
            bool operator!=(const iterator& o) const noexcept { return idx_ != o.idx_; }
        };

        class const_iterator {
            friend class flat_map;
            using kv_type = std::pair<Key, Value>;
            const uint8_t* meta_;
            const kv_type* kvs_;
            size_t         idx_;
            size_t         cap_;

            void advance() noexcept {
                while (idx_ < cap_ && meta_[idx_] == EMPTY) ++idx_;
            }

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = kv_type;
            using pointer = const kv_type*;
            using reference = const kv_type&;
            using iterator_category = std::forward_iterator_tag;

            const_iterator(const uint8_t* m, const kv_type* k, size_t i, size_t c) noexcept
                : meta_(m), kvs_(k), idx_(i), cap_(c) {
                advance();
            }

            reference operator*()  const noexcept { return kvs_[idx_]; }
            pointer   operator->() const noexcept { return &kvs_[idx_]; }

            const_iterator& operator++() noexcept { ++idx_; advance(); return *this; }
            const_iterator  operator++(int) noexcept { auto t = *this; ++(*this); return t; }

            bool operator==(const const_iterator& o) const noexcept { return idx_ == o.idx_; }
            bool operator!=(const const_iterator& o) const noexcept { return idx_ != o.idx_; }
        };

        iterator       begin()        noexcept { return { meta_, kvs_, 0, capacity_ }; }
        iterator       end()          noexcept { return { meta_, kvs_, capacity_, capacity_ }; }
        const_iterator begin()  const noexcept { return { meta_, kvs_, 0, capacity_ }; }
        const_iterator end()    const noexcept { return { meta_, kvs_, capacity_, capacity_ }; }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend()   const noexcept { return end(); }
    };

} // namespace milo