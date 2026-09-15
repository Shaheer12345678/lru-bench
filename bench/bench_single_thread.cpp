// One thread, no lock contention: differences between designs here come from data layout, allocation
// and hashing alone.

#include "matrix.hpp"

int main(int argc, char** argv) {
    return lru_bench::run_matrix(argc, argv, {1});
}
