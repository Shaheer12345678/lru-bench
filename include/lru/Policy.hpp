#pragma once

#include <concepts>
#include <cstddef>
#include <utility>

namespace lru {

namespace detail {

// A named no-op callable, used instead of a lambda inside the concept so every translation unit
// sees the same type when the constraint is checked.
struct DiscardValue {
    template <typename T>
    void operator()(const T&) const noexcept {}
};

}  // namespace detail

// The cache designs in this repo are only worth comparing if they honour the same contract.
// Pinning that contract down as a concept lets the shared correctness suite and the benchmarks
// be written once, and makes a design that drifts from it fail to compile instead of quietly
// being measured against a different API.
//
// Reads go through visit() rather than returning a reference: a reference would outlive the lock
// and could dangle the moment another thread evicts the entry. visit() also keeps move-only
// value types usable, since nothing has to be copied out.
template <typename C>
concept LruCacheLike = requires(C& cache, const C& const_cache, typename C::key_type key,
                                typename C::mapped_type value) {
    { cache.put(std::move(key), std::move(value)) } -> std::same_as<void>;
    { cache.visit(key, detail::DiscardValue{}) } -> std::same_as<bool>;
    { const_cache.size() } -> std::same_as<std::size_t>;
    { const_cache.capacity() } -> std::same_as<std::size_t>;
};

}  // namespace lru
