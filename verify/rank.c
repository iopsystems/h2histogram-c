/* CBMC harness: for every finite p in [0,1] and every total >= 1,
 * rank_for returns a rank in [1, total], with exact endpoints at p=0 and p=1.
 * Floating point is bit-blasted, so this is a real proof, not sampling. */
#include "../src/h2histogram.c"

double nondet_double(void);
uint64_t nondet_u64(void);

void harness(void) {
    double p = nondet_double();
    __CPROVER_assume(__CPROVER_isfinited(p) && p >= 0 && p <= 1);
    uint64_t total = nondet_u64();
    __CPROVER_assume(total >= 1);
    uint64_t r = rank_for(p, total);
    __CPROVER_assert(r >= 1, "rank at least 1");
    __CPROVER_assert(r <= total, "rank at most total");
    if (p == 0)
        __CPROVER_assert(r == 1, "p=0 selects first");
    if (p == 1)
        __CPROVER_assert(r == total, "p=1 selects last");
}
