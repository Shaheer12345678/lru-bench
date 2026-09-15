#include <cstddef>

#include "lru/LruCache.hpp"
#include "test_correctness.hpp"

namespace lru_test {
namespace {

struct V1Factory {
    template <typename K, typename V>
    using cache = lru::LruCache<K, V>;

    template <typename K, typename V>
    static cache<K, V> make(std::size_t capacity) {
        return cache<K, V>(capacity);
    }
};

using V1Types = ::testing::Types<V1Factory>;

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(V1, LruContractTest, V1Types);
INSTANTIATE_TYPED_TEST_SUITE_P(V1, LruStrictOrderTest, V1Types);

}  // namespace lru_test
