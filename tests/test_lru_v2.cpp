#include "factories.hpp"
#include "test_correctness.hpp"

namespace lru_test {
namespace {

using V2Types = ::testing::Types<V2Factory>;

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(V2, LruContractTest, V2Types);
INSTANTIATE_TYPED_TEST_SUITE_P(V2, LruStrictOrderTest, V2Types);

}  // namespace lru_test
