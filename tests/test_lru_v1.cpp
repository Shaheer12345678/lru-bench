#include "factories.hpp"
#include "test_correctness.hpp"

namespace lru_test {
namespace {

using V1Types = ::testing::Types<V1Factory>;

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(V1, LruContractTest, V1Types);
INSTANTIATE_TYPED_TEST_SUITE_P(V1, LruStrictOrderTest, V1Types);

}  // namespace lru_test
