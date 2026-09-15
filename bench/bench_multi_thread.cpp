// Several threads sharing one cache: v1 and v2 serialise every operation on a single mutex, while v3
// spreads threads across independent shard locks, so this is where the designs diverge on scaling.
//
// Registers 2, 4 and 8 threads by default. Pass --lru_threads=1,2,4,8 to register every thread count in this
// one process, so random interleaving mixes thread counts as well as designs and the scaling curve does not
// depend on which cells happened to run first.

#include "matrix.hpp"

int main(int argc, char** argv) {
    return lru_bench::run_matrix(argc, argv, {2, 4, 8});
}
