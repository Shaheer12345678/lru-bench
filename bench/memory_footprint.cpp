// Memory footprint of one design at one capacity, as a JSON object on stdout.
//
// Peak RSS inside the benchmark matrix would be meaningless: a process's peak only rises, so each cell
// would report the largest cell run before it. This tool runs one design per process instead and reports
// bytes requested from the allocator and resident memory at three points: after construction, after
// filling 1% of capacity, and after inserting as many distinct keys as the capacity.
//
// Linux only: resident memory is read from /proc/self/status.
//
// Usage: memory_footprint <v1|v2|v3> <capacity>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "runner.hpp"

namespace {

std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_bytes{0};

void* counted_alloc(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* counted_aligned_alloc(std::size_t size, std::align_val_t alignment) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    const auto align = static_cast<std::size_t>(alignment);
    const std::size_t rounded = size == 0 ? align : (size + align - 1) / align * align;
    if (void* p = std::aligned_alloc(align, rounded)) {
        return p;
    }
    throw std::bad_alloc();
}

// VmRSS in KiB, read with raw file I/O into a stack buffer so the measurement itself never allocates.
long resident_kib() {
    const int fd = ::open("/proc/self/status", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    char buffer[8192];
    const ssize_t length = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (length <= 0) {
        return -1;
    }
    buffer[length] = '\0';
    const char* field = std::strstr(buffer, "VmRSS:");
    return field != nullptr ? std::strtol(field + 6, nullptr, 10) : -1;
}

struct Snapshot {
    std::uint64_t allocations;
    std::uint64_t bytes;
    long rss_kib;
};

Snapshot snapshot() {
    return {g_allocations.load(std::memory_order_relaxed), g_bytes.load(std::memory_order_relaxed), resident_kib()};
}

template <typename Design>
int measure(std::size_t capacity) {
    const Snapshot before = snapshot();
    auto cache = Design::make(capacity);
    const Snapshot constructed = snapshot();

    const std::uint64_t one_percent = capacity / 100;
    for (std::uint64_t key = 0; key < one_percent; ++key) {
        cache->put(key, key);
    }
    const Snapshot filled_one_percent = snapshot();

    for (std::uint64_t key = one_percent; key < capacity; ++key) {
        cache->put(key, key);
    }
    const Snapshot filled = snapshot();

    std::printf("{\"design\": \"%s\", \"capacity\": %zu, "
                "\"construct_bytes\": %" PRIu64 ", \"construct_allocations\": %" PRIu64 ", \"construct_rss_kib\": %ld, "
                "\"fill_1pct_bytes\": %" PRIu64 ", \"fill_1pct_allocations\": %" PRIu64 ", \"rss_after_1pct_kib\": %ld, "
                "\"fill_full_bytes\": %" PRIu64 ", \"fill_full_allocations\": %" PRIu64 ", \"rss_after_full_kib\": %ld, "
                "\"size_after_full\": %zu}\n",
                Design::name().c_str(), capacity,
                constructed.bytes - before.bytes, constructed.allocations - before.allocations,
                constructed.rss_kib - before.rss_kib,
                filled_one_percent.bytes - constructed.bytes, filled_one_percent.allocations - constructed.allocations,
                filled_one_percent.rss_kib - before.rss_kib,
                filled.bytes - filled_one_percent.bytes, filled.allocations - filled_one_percent.allocations,
                filled.rss_kib - before.rss_kib,
                cache->size());
    return 0;
}

}  // namespace

void* operator new(std::size_t size) { return counted_alloc(size); }
void* operator new[](std::size_t size) { return counted_alloc(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return counted_aligned_alloc(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return counted_aligned_alloc(size, alignment); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return counted_alloc(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return counted_alloc(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return counted_aligned_alloc(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return counted_aligned_alloc(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <v1|v2|v3> <capacity>\n", argv[0]);
        return 2;
    }
    const std::string design = argv[1];
    const auto capacity = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    if (design == "v1") {
        return measure<lru_bench::V1Design>(capacity);
    }
    if (design == "v2") {
        return measure<lru_bench::V2Design>(capacity);
    }
    if (design == "v3") {
        return measure<lru_bench::V3Design>(capacity);
    }
    std::fprintf(stderr, "unknown design '%s'\n", design.c_str());
    return 2;
}
